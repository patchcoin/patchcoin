// Copyright (c) 2017-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <kernel.h>
#include <sendclaimset.h>
#include <validation.h>   // GetCoinAge()

#include <util/moneystr.h>

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;

    // Even if tx.nLockTime isn't satisfied by nBlockHeight/nBlockTime, a
    // transaction is still considered final if all inputs' nSequence ==
    // SEQUENCE_FINAL (0xffffffff), in which case nLockTime is ignored.
    //
    // Because of this behavior OP_CHECKLOCKTIMEVERIFY/CheckLockTime() will
    // also check that the spending input's nSequence != SEQUENCE_FINAL,
    // ensuring that an unsatisfied nLockTime value will actually cause
    // IsFinalTx() to return false here:
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>& prevHeights, const CBlockIndex& block)
{
    assert(prevHeights.size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            prevHeights[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = prevHeights[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            const int64_t nCoinTime{Assert(block.GetAncestor(std::max(nCoinHeight - 1, 0)))->GetMedianTimePast()};
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>& prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto& txin : tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto& txout : tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, uint32_t flags)
{
    int64_t nSigOps = GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR;

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        nSigOps += CountWitnessSigOps(tx.vin[i].scriptSig, prevout.scriptPubKey, &tx.vin[i].scriptWitness, flags);
    }
    return nSigOps;
}

bool CheckGenesisKeyInput(TxValidationState& state, const CScript& scriptPubKey, const Consensus::Params& params, const CTransaction& tx, const unsigned int nTimeTx)
{
    if (scriptPubKey != params.genesisPubKey)
        return true;

    if (tx.IsCoinStake())
        return true;

    if (nTimeTx - params.genesisNTime < params.nStakeGenesisLockTime) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-genesis-input-not-coinstake",
            strprintf("%s: genesis key tried to spend outside stake transaction at day %i, within %i days of genesis tx", __func__,
                static_cast<int>(nTimeTx - params.genesisNTime) / (24 * 60 * 60),
                static_cast<int>(params.nStakeGenesisLockTime / (24 * 60 * 60)))
        );
    }

    return true;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, TxValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee, const Consensus::Params& params, unsigned int nTimeTx, uint64_t nMoneySupply)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.Invalid(TxValidationResult::TX_MISSING_INPUTS, "bad-txns-inputs-missingorspent",
                         strprintf("%s: inputs missing/spent", __func__));
    }

    CAmount nValueIn = 0;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        const bool is_genesis_coin = prevout.hash == params.hashGenesisTx;

        if (is_genesis_coin && coin.out.nValue != params.genesisValue / params.genesisOutputs) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-genesis-coin-value-too-large");
        }

        if (coin.IsCoinBase() && !is_genesis_coin) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-coinbase-as-input",
                strprintf("%s: tried to use coinbase other than genesis tx as input", __func__));
        }

        // If prev is coinstake, check that it's matured
        const int requiredMaturity = is_genesis_coin ? 0 : params.nCoinbaseMaturity;

        if (coin.IsCoinStake() && nSpendHeight - coin.nHeight < requiredMaturity) {
            return state.Invalid(TxValidationResult::TX_PREMATURE_SPEND, "bad-txns-premature-spend-of-coinbase/coinstake",
                strprintf("%s: tried to spend coinstake at depth %d", __func__, nSpendHeight - coin.nHeight));
        }

        // peercoin: check transaction timestamp
        if (coin.nTime > nTimeTx)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-spent-too-early", strprintf("%s: transaction timestamp earlier than input transaction", __func__));

        if (!CheckGenesisKeyInput(state, coin.out.scriptPubKey, params, tx, nTimeTx))
            return false;

        // Check for negative or overflow input values
        nValueIn += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn)) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-inputvalues-outofrange");
        }
    }

    if (tx.IsCoinStake())
    {
        // peercoin: coin stake tx earns reward instead of paying fee
        uint64_t nCoinAge;
        if (!GetCoinAge(tx, inputs, nCoinAge, nTimeTx))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "unable to get coin age for coinstake");
    }
    else
    {
        const CAmount value_out = tx.GetValueOut();
        if (nValueIn < value_out) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-in-belowout",
                strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(value_out)));
        }
        // Tally transaction fees
        const CAmount txfee_aux = nValueIn - value_out;
        if (!MoneyRange(txfee_aux)) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-fee-outofrange");
        }
        // peercoin: enforce transaction fees for every block
        if (txfee_aux < GetMinFee(tx, nTimeTx))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-fee-not-enough");
        txfee = txfee_aux;
    }
    return true;
}

bool IsOpReturn(const CScript& script)
{
    CScript::const_iterator it = script.begin();
    opcodetype opcode;
    if (it != script.end() && script.GetOp(it, opcode))
    {
        return opcode == OP_RETURN;
    }
    return false;
}

bool IsBtcAccepted(const Consensus::Params& params, const unsigned int nTimeTx)
{
    return nTimeTx - params.genesisNTime > (params.nStakeGenesisLockTime / 2);
}

bool CheckClaimOutputFormat(TxValidationState& state, const Consensus::Params& params,
                            const CScript& scriptPubKey, const Claim* claim)
{
    // patchcoin todo: re-add format verification. the entries that existed here during development were mostly done for sanity checking before committing a block, not for consensus reasons
    if (scriptPubKey.empty() || claim != nullptr || scriptPubKey == params.genesisPubKey || IsOpReturn(scriptPubKey)) {
        return true;
    }
    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-format");
}

bool CheckClaimEligibility(TxValidationState& state, const CTxOut& txout, const Claim* claim, const CBlockIndex* pindex,
                              const std::map<const CScript, std::pair<Claim*, CAmount>>& claims, CAmount& nTotalReceived, unsigned int& nOutputs)
{
    assert(claim != nullptr);

    if (claims.count(claim->GetSource()) > 0) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-dup-block",
                             strprintf("%s: multiple outputs per claim in a single block are not allowed", __func__));
    }

    ScriptError serror;
    if (claim->IsValid(&serror) != Claim::ClaimVerificationResult::OK) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-invalid",
                             strprintf("%s: invalid claim found %s", __func__, ScriptErrorString(serror)));
    }

    if (!(claim->m_is_btc && !claim->m_electrum_result) && !claim->GetTotalReceived(pindex->pprev, nTotalReceived, nOutputs)) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-total-check",
                             strprintf("%s: failed to get claim total received amount", __func__));
    }

    nOutputs++;
    // patchcoin todo maxOutputs currently has no look-ahead, and could lead to breakage
    if (nOutputs > claim->MAX_POSSIBLE_OUTPUTS || (genesis_key_held && (nOutputs > claim->MAX_OUTPUTS || nOutputs > claim->GetMaxOutputs()))) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-out-limit",
                             strprintf("%s: claim outputs would exceed allowed outputs", __func__));
    }

    nTotalReceived += txout.nValue;
    if (!MoneyRange(nTotalReceived) || nTotalReceived > 50000 * COIN) { // ref snapshotmanager.h
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-out-of-range",
                             strprintf("%s: claim total received out of range", __func__));
    }

    if (claim->m_is_btc && !claim->m_electrum_result) {
        return true;
    }

    if (nTotalReceived > claim->GetEligible()) { // this is the real check
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-exceed-eligible",
                             strprintf("%s: claim would exceed eligible eligible=%s would_receive=%s", __func__, FormatMoney(claim->GetEligible()), FormatMoney(nTotalReceived)));
    }

    return true;
}

bool CheckClaims(TxValidationState& state, const CBlockIndex* pindex, const CCoinsViewCache& view, const Consensus::Params& params,
    const CTransaction& tx, const unsigned int nTimeTx, const std::vector<CClaim>& vClaim, std::map<const CScript, std::pair<Claim*, CAmount>>& claims, std::vector<uint16_t>& queued_claims)
{
    if (vClaim.empty()) {
        return true;
    }

    // patchcoin todo claim period is done, stop tracking
    if (nTimeTx - params.genesisNTime > params.nStakeGenesisLockTime) {
        if (!vClaim.empty()) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-outside-claim-period");
        }
        return true;
    }

    bool isAnyFromGenesis = false;
    for (const auto& input : tx.vin) {
        const COutPoint& prevout = input.prevout;
        const Coin& coin = view.AccessCoin(prevout);
        if (!CheckGenesisKeyInput(state, coin.out.scriptPubKey, params, tx, nTimeTx)) {
            return false;
        }
        isAnyFromGenesis |= coin.out.scriptPubKey == params.genesisPubKey;
    }

    if (!isAnyFromGenesis) {
        if (!vClaim.empty()) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claims-not-empty",
                strprintf("%s: claims must only originate from genesis", __func__));
        }
        return true;
    }

    if (vClaim.size() > (tx.vout.size() - 2)) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claims-too-many",
            strprintf("%s: claim size must not exceed usable txout size", __func__));
    }

    LOCK(g_claims_mutex);

    for (const CClaim& claim : vClaim) {
        Claim maybe_valid{claim};
        ScriptError serror;
        if (maybe_valid.IsValid(&serror) != Claim::ClaimVerificationResult::OK) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-invalid",
                strprintf("%s: invalid claim found %s", __func__, ScriptErrorString(serror)));
        }
        if (maybe_valid.IsUnique() && !maybe_valid.Insert()) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-no-insert");
        }
        if (maybe_valid.m_is_btc && !IsBtcAccepted(params, nTimeTx)) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-btc-time");
        }
    }

    std::map<CScript, CAmount> total_received;
    std::map<CScript, unsigned int> outputs;

    for (unsigned int pos = 0; pos < tx.vout.size(); pos++) {
        const CTxOut& txout = tx.vout[pos];
        Claim* claim = nullptr;
        for (auto& [_, fClaim] : g_claims) {
            if (txout.scriptPubKey == fClaim.GetTarget()) {
                claim = &fClaim;
                total_received[claim->GetSource()] = 0;
                outputs.try_emplace(claim->GetSource(), 0);
                break;
            }
        }

        if (!CheckClaimOutputFormat(state, params, txout.scriptPubKey, claim)) {
            return false;
        }

        if (claim == nullptr) {
            if (IsOpReturn(txout.scriptPubKey)) {
                if (txout.nValue != 0) {
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-op-return-not-zero");
                }
                if (IsBtcAccepted(params, nTimeTx)) {
                    continue;
                }
                std::vector<unsigned char> payload;
                {
                    opcodetype opcode;
                    std::vector<unsigned char> data;
                    CScript::const_iterator it = txout.scriptPubKey.begin();

                    if (!txout.scriptPubKey.GetOp(it, opcode, data) || opcode != OP_RETURN) {
                        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-last-out",
                        strprintf("%s: OP_RETURN not found", __func__));
                    }
                    if (!txout.scriptPubKey.GetOp(it, opcode, data)) {
                        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-last-out",
                        strprintf("%s: no payload found after OP_RETURN", __func__));
                    }
                    payload = data;
                }

                CDataStream ds(payload, SER_NETWORK, PROTOCOL_VERSION);
                ds >> queued_claims;
                for (const CBlockIndex* pindexWalk = pindex->pprev; pindexWalk != nullptr; pindexWalk = pindexWalk->pprev) {
                    for (const uint16_t& claim_pos : queued_claims) {
                        if (std::find(pindexWalk->queuedClaims.begin(), pindexWalk->queuedClaims.end(), claim_pos) != pindexWalk->queuedClaims.end()) {
                            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-claim-already-queued");
                        }
                    }
                }
            }
            continue;
        }

        CAmount &nTotalReceived = total_received[claim->GetSource()];
        unsigned int &nOutputs = outputs[claim->GetSource()];
        if (!CheckClaimEligibility(state, txout, claim, pindex, claims, nTotalReceived, nOutputs)) {
            return false;
        }

        claims[claim->GetSource()] = std::make_pair(claim, txout.nValue);

        LogPrintf("%s: Claim processed: source=%s target=%s amount=%s total=%s/%s outputs=%u%s\n", __func__,
                  claim->m_is_btc ? claim->GetBtcSourceAddress() : claim->GetSourceAddress(),
                  claim->GetTargetAddress(),
                  FormatMoney(txout.nValue),
                  FormatMoney(nTotalReceived),
                  FormatMoney(claim->GetEligible()),
                  nOutputs - 1,
                  genesis_key_held ? strprintf("/%u", claim->GetMaxOutputs()) : "");
    }

    return true;
}

CAmount GetMinFee(const CTransaction& tx, unsigned int nTimeTx)
{
    size_t nBytes = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    return GetMinFee(nBytes, nTimeTx);
}

CAmount GetMinFee(size_t nBytes, uint32_t nTime)
{
    CAmount nMinFee;
    if (IsProtocolV07(nTime) || !nTime) // RFC-0007
        nMinFee = (nBytes < 100) ? MIN_TX_FEE : (CAmount)(nBytes * (PERKB_TX_FEE / 1000));
    else
        nMinFee = (1 + (CAmount)nBytes / 1000) * PERKB_TX_FEE;

    if (!MoneyRange(nMinFee))
        nMinFee = MAX_MONEY;
    return nMinFee;
}
