// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/miner.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <rpc/blockchain.h>
#include <timedata.h>
#include <rpc/blockchain.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <validation.h>
#include <kernel.h>
#include <net.h>
#include <interfaces/chain.h>
#include <node/context.h>
#include <node/interface_ui.h>
#include <util/exception.h>
#include <util/thread.h>
#include <validation.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <warnings.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <claimset.h>
#include <sendclaimset.h>
#include <utility>

#include <boost/thread.hpp>

using wallet::CWallet;
using wallet::COutput;
using wallet::CCoinControl;
using wallet::ReserveDestination;

int64_t nLastCoinStakeSearchInterval = 0;
std::thread m_minter_thread;
std::thread m_cspub_thread;

namespace node {
int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime{std::max<int64_t>(pindexPrev->GetMedianTimePast() + 1, TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime()))};

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextTargetRequired(pindexPrev, false, consensusParams);
    }

    return nNewTime - nOldTime;
}

void RegenerateCommitments(CBlock& block, ChainstateManager& chainman)
{
    CMutableTransaction tx{*block.vtx.at(0)};
    tx.vout.erase(tx.vout.begin() + GetWitnessCommitmentIndex(block));
    block.vtx.at(0) = MakeTransactionRef(tx);

    const CBlockIndex* prev_block = WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock));
    chainman.GenerateCoinbaseCommitment(block, prev_block);

    block.hashMerkleRoot = BlockMerkleRoot(block);
}

static BlockAssembler::Options ClampOptions(BlockAssembler::Options options)
{
    // Limit weight to between 4K and DEFAULT_BLOCK_MAX_WEIGHT for sanity:
    options.nBlockMaxWeight = std::clamp<size_t>(options.nBlockMaxWeight, 4000, DEFAULT_BLOCK_MAX_WEIGHT);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const CTxMemPool* mempool, const Options& options)
    : chainparams{chainstate.m_chainman.GetParams()},
      m_mempool{mempool},
      m_chainstate{chainstate},
      m_options{ClampOptions(options)}
{
}

void ApplyArgsManOptions(const ArgsManager& args, BlockAssembler::Options& options)
{
    // Block resource limits
    // If -blockmaxweight is not given, limit to DEFAULT_BLOCK_MAX_WEIGHT
    options.nBlockMaxWeight = gArgs.GetIntArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
}
static BlockAssembler::Options ConfiguredOptions()
{
    BlockAssembler::Options options;
    ApplyArgsManOptions(gArgs, options);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const CTxMemPool* mempool)
    : BlockAssembler(chainstate, mempool, ConfiguredOptions()) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

// peercoin: if pwallet != NULL it will attempt to create coinstake
std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool* pfPoSCancel, NodeContext* m_node, CTxDestination destination)
{
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if (!pblocktemplate.get()) {
        return nullptr;
    }
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    pblock->nTime = TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime());

    LOCK(::cs_main);

    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime());
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    bool shouldAddPackageTxs = false;

    if (m_mempool) {
        if (genesis_key_held) {
            if (pindexPrev->pprev && pindexPrev->nClaims.size() && pindexPrev->pprev->nClaims.size()) {
                shouldAddPackageTxs = true;
            } else {
                bool allClaimsProcessed = true;
                for (const auto& [_, claim] : g_claims) {
                    if (claim.nTotalReceived < claim.GetEligible()) {
                        allClaimsProcessed = false;
                        break;
                    }
                }
                shouldAddPackageTxs = allClaimsProcessed;
            }
        } else {
            shouldAddPackageTxs = true;
        }
    }

    if (shouldAddPackageTxs) {
        LOCK(m_mempool->cs);
        addPackageTxs(*m_mempool, nPackagesSelected, nDescendantsUpdated, pblock->nTime);
    }

    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;

    if (pwallet == nullptr) {
        pblock->nBits = GetNextTargetRequired(pindexPrev, false, chainparams.GetConsensus());
        coinbaseTx.vout[0].nValue = nFees;
    }

    // peercoin: if coinstake available add coinstake tx
    static int64_t nLastCoinStakeSearchTime = pblock->nTime;  // only initialized at startup

#ifdef ENABLE_WALLET
    if (pwallet)  // attemp to find a coinstake
    {
        *pfPoSCancel = true;
        pblock->nBits = GetNextTargetRequired(pindexPrev, true, chainparams.GetConsensus());
        std::vector<Claim> vClaim;
        CMutableTransaction txCoinStake;
        int64_t nSearchTime = txCoinStake.nTime; // search to current time
        if (nSearchTime > nLastCoinStakeSearchTime)
        {
            if (pwallet->CreateCoinStake(*m_node->chainman, pwallet, pblock->nBits, nSearchTime-nLastCoinStakeSearchTime, txCoinStake, vClaim, destination, nFees, nBlockTx))
            {
                if (txCoinStake.nTime >= std::max(pindexPrev->GetMedianTimePast()+1, pindexPrev->GetBlockTime() - (IsProtocolV09(pindexPrev->GetBlockTime()) ? MAX_FUTURE_BLOCK_TIME : MAX_FUTURE_BLOCK_TIME_PREV9)))
                {   // make sure coinstake would meet timestamp protocol
                    // as it would be the same as the block timestamp
                    coinbaseTx.vout[0].SetEmpty();
                    coinbaseTx.nTime = txCoinStake.nTime;
                    pblock->vtx.insert(pblock->vtx.begin() + 1, MakeTransactionRef(CTransaction(txCoinStake)));
                    *pfPoSCancel = false;
                    if (genesis_key_held)
                    {
                        for (const Claim& claim : vClaim) {
                            pblock->vClaim.push_back(CClaim(claim.GetClaim()));
                        }
                    }
                }
            }
            nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
            nLastCoinStakeSearchTime = nSearchTime;
        }
        if (*pfPoSCancel)
            return nullptr; // peercoin: there is no point to continue if we failed to create coinstake
        pblock->nFlags = CBlockIndex::BLOCK_PROOF_OF_STAKE;
    }
#endif

    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vchCoinbaseCommitment = m_chainstate.m_chainman.GenerateCoinbaseCommitment(*pblock, pindexPrev);
    pblocktemplate->vTxFees[0] = -nFees;

    LogPrintf("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    if (pblock->IsProofOfStake())
        pblock->nTime      = pblock->vtx[1]->nTime; //same as coinstake timestamp
    pblock->nTime          = std::max(pindexPrev->GetMedianTimePast()+1, pblock->GetMaxTransactionTime());
    pblock->nTime          = std::max(pblock->GetBlockTime(), pindexPrev->GetBlockTime() - (IsProtocolV09(pindexPrev->GetBlockTime()) ? MAX_FUTURE_BLOCK_TIME : MAX_FUTURE_BLOCK_TIME_PREV9));
    if (pblock->IsProofOfWork())
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nNonce         = 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    BlockValidationState state;
    if (m_options.test_block_validity && !TestBlockValidity(state, chainparams, m_chainstate, *pblock, pindexPrev, GetAdjustedTime, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
    }
    const auto time_2{SteadyClock::now()};

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start), nPackagesSelected, nDescendantsUpdated,
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= m_options.nBlockMaxWeight) {
        return false;
    }
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST) {
        return false;
    }
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package, uint32_t nTime) const
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, m_lock_time_cutoff)) {
            return false;
        }
        // peercoin: timestamp limit
        if (it->GetTx().nTime > TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime()) || (nTime && it->GetTx().nTime > nTime))
            return false;
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblocktemplate->block.vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %d satoshi txid %s\n",
                  iter->GetModifiedFee(),
                  iter->GetTx().GetHash().ToString());
    }
}

/** Add descendants of given transactions to mapModifiedTx with ancestor
 * state updated assuming given transactions are inBlock. Returns number
 * of updated descendants. */
static int UpdatePackagesForAdded(const CTxMemPool& mempool,
                                  const CTxMemPool::setEntries& alreadyAdded,
                                  indexed_modified_transaction_set& mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs)
{
    AssertLockHeld(mempool.cs);

    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc)) {
                continue;
            }
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                mit = mapModifiedTx.insert(modEntry).first;
            }
            mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
        }
    }
    return nDescendantsUpdated;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(const CTxMemPool& mempool, int& nPackagesSelected, int& nDescendantsUpdated, uint32_t nTime)
{
    AssertLockHeld(mempool.cs);

    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        //
        // Skip entries in mapTx that are already in a block or are present
        // in mapModifiedTx (which implies that the mapTx ancestor state is
        // stale due to ancestor inclusion in the block)
        // Also skip transactions that we've already failed to add. This can happen if
        // we consider a transaction in mapModifiedTx and it fails: we can then
        // potentially consider it again while walking mapTx.  It's currently
        // guaranteed to fail again, but as a belt-and-suspenders check we put it in
        // failedTx and avoid re-evaluation, since the re-evaluation would be using
        // cached size/sigops/fee values that are not actually correct.
        /** Return true if given transaction from mapTx has already been evaluated,
         * or if the transaction's cached data in mapTx is incorrect. */
        if (mi != mempool.mapTx.get<ancestor_score>().end()) {
            auto it = mempool.mapTx.project<0>(mi);
            assert(it != mempool.mapTx.end());
            if (mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it)) {
                ++mi;
                continue;
            }
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    m_options.nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        auto ancestors{mempool.AssumeCalculateMemPoolAncestors(__func__, *iter, CTxMemPool::Limits::NoLimits(), /*fSearchForParents=*/false)};

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors,nTime)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(mempool, ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce));
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

bool PublishClaimset(const CWallet& pwallet, const CConnman *connman, CClaimSet& sendThis)
{
    if (g_claims.size() == 0)
        return true;
    {
        LOCK2(pwallet.cs_wallet, cs_main);
        if (!SignClaimSet(pwallet, sendThis)) {
            LogPrintf("BuildAndSignClaimSet() failed: Invalid claim(s) found\n");
            return false;
        }
        send_claimset = sendThis;
    }
    return true;
}


static bool ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams, NodeContext& m_node)
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0]->vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != m_node.chainman->ActiveChain().Tip()->GetBlockHash())
            return error("PeercoinMiner: generated block is stale");
    }

    // Process this block the same as if we had received it from another node
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!m_node.chainman->ProcessNewBlock(shared_pblock, true, true, NULL))
        return error("ProcessNewBlock, block not accepted");

    return true;
}

void PoSMiner(NodeContext& m_node)
{
    std::string strMintMessage = _("Info: Minting suspended due to locked wallet.").translated;
    std::string strMintSyncMessage = _("Info: Minting suspended while synchronizing wallet.").translated;
    std::string strMintDisabledMessage = _("Info: Minting disabled by 'nominting' option.").translated;
    std::string strMintBlockMessage = _("Info: Minting suspended due to block creation failure.").translated;
    std::string strMintDelayMessage = _("Minting suspended - waiting for minimum time since last block").translated;
    std::string strMintEmpty = "";
    if (!gArgs.GetBoolArg("-minting", true) || !gArgs.GetBoolArg("-staking", true))
    {
        strMintWarning = strMintDisabledMessage;
        LogPrintf("proof-of-stake minter disabled\n");
        return;
    }

    int64_t nBlockDelayMin    = gArgs.GetIntArg("-blockdelay", 0);
    int64_t nBlockDelaySec    = nBlockDelayMin * 60;

    CConnman* connman = m_node.connman.get();
    CWallet* pwallet;
    // ppctodo: deal with multiple wallets better
    if (m_node.wallet_loader->getWallets().size() && gArgs.GetBoolArg("-minting", true))
        pwallet = m_node.wallet_loader->getWallets()[0]->wallet();
    else
        return;

    LogPrintf("CPUMiner started for proof-of-stake\n");
    util::ThreadRename("peercoin-stake-minter");

    unsigned int nExtraNonce = 0;

    CTxDestination dest;
    // Compute timeout for pos as sqrt(numUTXO)
    unsigned int pos_timio;
    {
        LOCK2(pwallet->cs_wallet, cs_main);
        const std::string label = "mintkey";
        pwallet->ForEachAddrBookEntry([&](const CTxDestination& _dest, const std::string& _label, bool _is_change, const std::optional<wallet::AddressPurpose>& _purpose) {
            if (_is_change) return;
            if (_label == label)
                dest = _dest;
        });

        if (std::get_if<CNoDestination>(&dest)) {
            // create mintkey address
            auto op_dest = pwallet->GetNewDestination(OutputType::LEGACY, label);
            if (!op_dest)
                throw std::runtime_error("Error: Keypool ran out, please call keypoolrefill first.");
            dest = *op_dest;
        }

        wallet::CoinsResult availableCoins;
        CCoinControl coincontrol;
        availableCoins = AvailableCoins(*pwallet, &coincontrol);
        pos_timio = 500 + 30 * sqrt(availableCoins.Size());
        LogPrintf("Set proof-of-stake timeout: %ums for %u UTXOs\n", pos_timio, availableCoins.Size());
    }

    try {
        bool fNeedToClear = false;
        while (true) {
            while (pwallet->IsLocked()) {
                if (strMintWarning != strMintMessage) {
                    strMintWarning = strMintMessage;
                    uiInterface.NotifyAlertChanged();
                }
                fNeedToClear = true;
                if (!connman->interruptNet.sleep_for(std::chrono::seconds(3)))
                    return;
            }

            if (gArgs.GetBoolArg("-miningrequirespeers", Params().MiningRequiresPeers())) {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                while(connman == nullptr || connman->GetNodeCount(ConnectionDirection::Both) == 0 || m_node.chainman->ActiveChainstate().IsInitialBlockDownload()) {
                    while(connman == nullptr) {UninterruptibleSleep(1s);}
                    if (!connman->interruptNet.sleep_for(std::chrono::seconds(10)))
                        return;
                    }
            }
            if (genesis_key_held && g_claims.size() < 5) { // patchcoin todo count coins being output
                while (g_claims.size() < 5) {
                    if (!connman->interruptNet.sleep_for(std::chrono::seconds(10)))
                        return;
                }
            }
            CBlockIndex* pindexPrev;
            {
                LOCK(cs_main);
                pindexPrev = m_node.chainman->ActiveChain().Tip();

                while (GuessVerificationProgress(Params().TxData(), pindexPrev) < 0.996)
                {
                    LogPrintf("Minter thread sleeps while sync at %f\n", GuessVerificationProgress(Params().TxData(), pindexPrev));
                    if (strMintWarning != strMintSyncMessage) {
                        strMintWarning = strMintSyncMessage;
                        uiInterface.NotifyAlertChanged();
                    }
                    fNeedToClear = true;
                    if (!connman->interruptNet.sleep_for(std::chrono::seconds(10)))
                            return;
                }

                if (nBlockDelaySec > 0) {
                    while (true) {
                        int64_t now = GetTime();
                        int64_t lastTime = pindexPrev->GetBlockTime();
                        int64_t age = now - lastTime;
                        if (age >= nBlockDelaySec) break;
                        int64_t wait = nBlockDelaySec - age;
                        LogPrintf("Delaying PoS mining: last block %llds old, waiting %llds\n", age, wait);
                        if (!connman->interruptNet.sleep_for(std::chrono::seconds(wait)))
                            return;
                    }
                }
            }
            if (fNeedToClear) {
                strMintWarning = strMintEmpty;
                uiInterface.NotifyAlertChanged();
                fNeedToClear = false;
            }

            //
            // Create new block
            //
            bool fPoSCancel = false;
            CBlock *pblock;
            std::unique_ptr<CBlockTemplate> pblocktemplate;

            {
                LOCK2(pwallet->cs_wallet, cs_main);
                try {
                    pblocktemplate = BlockAssembler(m_node.chainman->ActiveChainstate(), m_node.mempool.get()).CreateNewBlock(GetScriptForDestination(dest), pwallet, &fPoSCancel, &m_node, dest);
                }
                catch (const std::runtime_error &e)
                {
                    LogPrintf("PeercoinMiner runtime error: %s\n", e.what());
                    continue;
                }
            }

            if (!pblocktemplate.get())
            {
                if (fPoSCancel == true)
                {
                    if (!connman->interruptNet.sleep_for(std::chrono::milliseconds(pos_timio)))
                        return;
                    continue;
                }
                strMintWarning = strMintBlockMessage;
                uiInterface.NotifyAlertChanged();
                LogPrintf("Error in PeercoinMiner: Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                if (!connman->interruptNet.sleep_for(std::chrono::seconds(10)))
                   return;

                return;
            }
            pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

            // peercoin: if proof-of-stake block found then process block
            if (pblock->IsProofOfStake())
            {
                {
                    LOCK2(pwallet->cs_wallet, cs_main);
                    if (!SignBlock(*pblock, *pwallet))
                    {
                        LogPrintf("PoSMiner(): failed to sign PoS block\n");
                        continue;
                    }
                }
                LogPrintf("CPUMiner : proof-of-stake block found %s\n", pblock->GetHash().ToString());
                try {
                    ProcessBlockFound(pblock, Params(), m_node);
                    }
                catch (const std::runtime_error &e)
                {
                    LogPrintf("PeercoinMiner runtime error: %s\n", e.what());
                    continue;
                }
                // Rest for ~3 minutes after successful block to preserve close quick
                if (!connman->interruptNet.sleep_for(std::chrono::seconds(60 + GetRand(4))))
                    return;
            }
            if (!connman->interruptNet.sleep_for(std::chrono::milliseconds(pos_timio)))
                return;

            continue;
        }
    }
    catch (::boost::thread_interrupted)
    {
        LogPrintf("PeercoinMiner terminated\n");
        return;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("PeercoinMiner runtime error: %s\n", e.what());
        return;
    }
}

// peercoin: stake minter thread
void static ThreadStakeMinter(NodeContext& m_node)
{
    LogPrintf("ThreadStakeMinter started\n");
    while(true) {
        try
        {
            PoSMiner(m_node);
            break;
        }
        catch (std::exception& e) {
            PrintExceptionContinue(&e, "ThreadStakeMinter()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ThreadStakeMinter()");
        }
    }
    LogPrintf("ThreadStakeMinter exiting\n");
}

void static ThreadCsPub(NodeContext& m_node)
{
    // patchcoin todo possibly timestamp sends
    LogPrintf("ThreadCsPub started\n");

    CConnman* connman = m_node.connman.get();
    CWallet* pwallet;
    if (m_node.wallet_loader->getWallets().size() && gArgs.GetBoolArg("-minting", true))
        pwallet = m_node.wallet_loader->getWallets()[0]->wallet();
    else
        return;

    {
        LOCK2(pwallet->cs_wallet, cs_main);
        CClaimSet empty;
        if (!SignClaimSet(*pwallet, empty))
            return;
        genesis_key_held = true;
    }

    try {
        int64_t last_publish_time = 0;
        Mutex cs_sending;

        while (true) {
            while (g_claims.empty()) {
                if (!connman->interruptNet.sleep_for(std::chrono::seconds(5)))
                    return;
            }
            {
                LOCK(cs_sending);

                const int64_t now = GetTime();
                const bool timeout_occurred = now - last_publish_time >= 60;

                bool has_new_claims = false;
                std::vector<Claim> new_claims;

                for (const auto& [script, claim] : g_claims) {
                    if (claim.nTime > last_publish_time) {
                        new_claims.push_back(claim);
                        has_new_claims = true;
                    }
                }

                if (has_new_claims/* || timeout_occurred*/) {
                    CClaimSet sendThis;
                    bool success = true;

                    if (has_new_claims) {
                        std::sort(new_claims.begin(), new_claims.end(),
                            [](const Claim& a, const Claim& b) { return a.nTime > b.nTime; });

                        for (const auto& claim : new_claims) {
                            if (!sendThis.AddClaim(claim)) {
                                LogPrintf("BuildClaimSet failed\n");
                                success = false;
                                break;
                            }
                        }
                    } else {
                        if (!BuildClaimSet(sendThis)) {
                            LogPrintf("BuildClaimSet failed\n");
                            success = false;
                        }
                    }

                    if (success && !PublishClaimset(*pwallet, connman, sendThis)) {
                        success = false;
                    }

                    if (success) {
                        last_publish_time = now;
                    } else {
                        break;
                    }
                }
            }
            if (!connman->interruptNet.sleep_for(std::chrono::seconds(1))) {
                break;
            }
        }
    } catch (::boost::thread_interrupted)
    {
        LogPrintf("ThreadCsPub terminated\n");
        return;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("ThreadCsPub runtime error: %s\n", e.what());
        return;
    }

    LogPrintf("ThreadCsPub exiting\n");
}

// peercoin: stake minter
void MintStake(NodeContext& m_node)
{
    m_minter_thread = std::thread([&] { util::TraceThread("minter", [&] { ThreadStakeMinter(m_node); }); });
}

void PublishClaimset(NodeContext& m_node)
{
    m_cspub_thread = std::thread([&] { util::TraceThread("cspub", [&] { ThreadCsPub(m_node); }); });
}
} // namespace node
