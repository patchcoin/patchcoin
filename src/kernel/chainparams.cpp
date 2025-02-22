// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Copyright (c) 2011-2025 The Peercoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <hash.h>
#include <chainparamsbase.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <algorithm>
#include <arith_uint256.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <thread>

void MineGenesisBlock(CBlock& genesis)
{
    std::cout << "Mining genesis block..." << '\n';

    arith_uint256 target;
    target.SetCompact(genesis.nBits);

    const unsigned int num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    std::atomic<bool> found(false);
    std::mutex solution_mutex;
    std::mutex cout_mutex;

    struct Solution {
        uint32_t nonce;
        uint256 hash;
    };
    Solution solution;
    bool solution_found = false;

    const uint64_t total_nonces = static_cast<uint64_t>(UINT32_MAX);
    const uint64_t per_thread = total_nonces / num_threads;
    const uint64_t remainder = total_nonces % num_threads;

    for (unsigned int i = 0; i < num_threads; ++i) {
        uint64_t start = i * per_thread;
        uint64_t end = start + per_thread - 1;

        if (i == num_threads - 1) {
            end += remainder;
        }

        if (end >= UINT32_MAX) {
            end = UINT32_MAX - 1;
        }

        threads.emplace_back([&, start, end]() {
            CBlock local_block = genesis;
            for (uint32_t nonce = static_cast<uint32_t>(start); nonce <= static_cast<uint32_t>(end); ++nonce) {
                if (found) {
                    return;
                }

                local_block.nNonce = nonce;
                uint256 hash = local_block.GetHash();

                if (UintToArith256(hash) <= target) {
                    std::lock_guard<std::mutex> lock(solution_mutex);
                    if (!solution_found) {
                        solution.nonce = nonce;
                        solution.hash = hash;
                        solution_found = true;
                        found = true;
                    }
                    return;
                }

                if (nonce % 1000000 == 0) {
                    std::lock_guard<std::mutex> cout_lock(cout_mutex);
                    std::cout << "Progress: Nonce = " << nonce << ", Hash = " << hash.ToString() << '\n';
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    if (solution_found) {
        genesis.nNonce = solution.nonce;
        std::cout << "Genesis block found!" << '\n';
        std::cout << "Hash: " << solution.hash.ToString() << '\n';
        std::cout << "Merkle Root: " << genesis.hashMerkleRoot.ToString() << '\n';
        std::cout << "Time: " << genesis.nTime << '\n';
        std::cout << "Nonce: " << genesis.nNonce << '\n';
        std::cout << "nBits: " << genesis.nBits << '\n';
        std::cout << "Version: " << genesis.nVersion << '\n' << '\n';
    } else {
        std::cerr << "Failed to find a valid genesis block within nonce range!" << '\n';
    }
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTimeBlock, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& nReward, const int& nOutputs)
{
    CMutableTransaction txNew;
    txNew.nVersion = 3;
    txNew.vin.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(9999) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    CAmount outValue = nReward / nOutputs;
    for (int i=0; i<nOutputs; i++) {
        txNew.vout.push_back(CTxOut(outValue, genesisOutputScript));
    }

    CBlock genesis;
    genesis.nTime    = nTimeBlock;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTimeBlock, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& nReward, const int& nOutputs)
{
    const char* pszTimestamp = "In the blockchain’s fog, coins drift, a patchwork of dreams — miners rest their picks.";
    const CScript genesisOutputScript = CScript() << ParseHex("031dd4243e7298ab47d869386f80ce9df58d42bd8d6e8d2180d3a83c8dc252fdd5") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTimeBlock, nNonce, nBits, nVersion, nReward, nOutputs);
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
/*
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.script_flag_exceptions.emplace( // BIP16 exception
            uint256S("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22"), SCRIPT_VERIFY_NONE);
        consensus.script_flag_exceptions.emplace( // Taproot exception
            uint256S("0x0000000000000000000f14c35b2d841e986ab5441de8c585d5ffe55ea1e395ad"), SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS);
        consensus.BIP34Height = 227931;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = 388381; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 363725; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
        consensus.CSVHeight = 419328; // 000000000000000004a1b34462cb8aeebd5799177f7a29cf28f2d1961716b5b5
        consensus.SegwitHeight = 481824; // 0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893
        consensus.MinBIP9WarningHeight = 483840; // segwit activation height + miner confirmation window
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 7 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = 1619222400; // April 24th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = 1628640000; // August 11th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 709632; // Approximately November 12th, 2021

        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000044a50fe819c39ad624021859");
        consensus.defaultAssumeValid = uint256S("0x000000000000000000035c3f0d31e71a5ee24c5aaf3354689f65bd7b07dee632"); // 784000
*/
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256{};
        consensus.powLimit =            uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 32;
        consensus.bnInitialHashTarget = uint256S("0000000001b4e81b4e81b4e81b4e81b4e81b4e81b4e81b4e81b4e81b4e81b4e8");

        consensus.nTargetTimespan = 7 * 24 * 60 * 60;  // one week
        consensus.nStakeTargetSpacing = 10 * 60; // 10-minute block spacing
        consensus.nTargetSpacingWorkMax = 12 * consensus.nStakeTargetSpacing; // 2-hour
        consensus.nPowTargetSpacing = consensus.nStakeTargetSpacing;
        consensus.nStakeMinAge = 60 * 60 * 24 * 30; // minimum age for coin age
        consensus.nStakeMaxAge = 60 * 60 * 24 * 90;
        consensus.nStakeGenesisLockTime = 60 * 60 * 24 * 180 + consensus.nStakeMaxAge;
        consensus.nModifierInterval = 6 * 60 * 60; // Modifier interval: time to elapse before new modifier is computed
        consensus.nCoinbaseMaturity = 500;

        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing

        consensus.SegwitHeight = 0;

        consensus.nMinimumChainWork = uint256S("0x"); // 750000
        consensus.defaultAssumeValid = uint256S("0x");  // 750000

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xe2;
        pchMessageStart[1] = 0xf4;
        pchMessageStart[2] = 0xd4;
        pchMessageStart[3] = 0xdd;
        nDefaultPort = 7801;
        m_assumed_blockchain_size = 1;

        consensus.genesisValue = 21000000 * COIN;
        consensus.genesisOutputs = 8750;
        genesis = CreateGenesisBlock(1740006000 - consensus.nStakeMaxAge /* 1732230000 */, 244683519u,  0x1d00ffff, 6, consensus.genesisValue, consensus.genesisOutputs);
        // MineGenesisBlock(genesis);
        assert(consensus.genesisValue == genesis.vtx[0]->GetValueOut());
        assert(consensus.genesisOutputs == static_cast<int>(genesis.vtx[0]->vout.size()));
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.hashGenesisTx = genesis.vtx[0]->GetHash();
        consensus.genesisPubKey = genesis.vtx[0]->vout[0].scriptPubKey;
        consensus.genesisNTime = genesis.nTime;
        assert(consensus.hashGenesisBlock == uint256S("0x00000000ac42d8edb3d3998251a90bde339d0d715c0987e2947a8d7e47de7280"));
        assert(genesis.hashMerkleRoot == uint256S("0x5758f1c6ff152187c10509e5ac4a9ac3c19c5e178e89463f0a2b4bba17e778bd"));
        consensus.hashPeercoinSnapshot = uint256S("0xf482e77541bb103674f1d53bd6fd634e00411f563e864648999597114c38d0c9");

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as an addrfetch if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        // vSeeds.emplace_back("seed.peercoin.net");
        // vSeeds.emplace_back("seed2.peercoin.net");
        // vSeeds.emplace_back("seed.peercoin-library.org");
        // vSeeds.emplace_back("seed.ppcoin.info");
        vSeeds.emplace_back("seed.patchcoin.org");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,55);  // peercoin: addresses begin with 'P'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,117); // peercoin: addresses begin with 'p'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,183);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // human readable prefix to bench32 address
        bech32_hrp = "pc";

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {     0, uint256S("0x40024c648fa673e6fbb0bc49a2c76964c7bae667af6c742b3ba22df8ec14834d")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
         // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data as of block 0fc7bf7f0e830eea0bc367c76f9dcfc70d42d5625d93b056354dc23049de6e29 (height 770396).
            0, // * UNIX timestamp of last known number of transactions
            0,    // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the ChainStateFlushed debug.log lines)
            0.0 // * estimated number of transactions per second after that timestamp
                        //   2551705/(1727128008-1345400356) = 0.006684622
        };
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256{};
        consensus.powLimit =            uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 28;
        consensus.bnInitialHashTarget = uint256S("00000007ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 29;

        consensus.nTargetTimespan = 7 * 24 * 60 * 60;  // one week
        consensus.nStakeTargetSpacing = 10 * 60;  // 10-minute block spacing
        consensus.nTargetSpacingWorkMax = 12 * consensus.nStakeTargetSpacing; // 2-hour
        consensus.nPowTargetSpacing = consensus.nStakeTargetSpacing;
        consensus.nStakeMinAge = 60 * 60 * 24; // test net min age is 1 day
        consensus.nStakeMaxAge = 60 * 60 * 24 * 90;
        consensus.nStakeGenesisLockTime = 60 * 60 * 24 * 180 + consensus.nStakeMaxAge;
        consensus.nModifierInterval = 60 * 20; // Modifier interval: time to elapse before new modifier is computed
        consensus.nCoinbaseMaturity = 60;

        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing

        consensus.SegwitHeight = 0;

        consensus.nMinimumChainWork = uint256S("0x");  // 500000
        consensus.defaultAssumeValid = uint256S("0x"); // 500000

        pchMessageStart[0] = 0xa9;
        pchMessageStart[1] = 0x85;
        pchMessageStart[2] = 0xd8;
        pchMessageStart[3] = 0xf0;
        nDefaultPort = 7803;
        m_assumed_blockchain_size = 1;

        consensus.genesisValue = 21000000 * COIN;
        consensus.genesisOutputs = 8750;
        genesis = CreateGenesisBlock(1740006000 - consensus.nStakeMaxAge /* 1732230000 */, 244683519u,  0x1d00ffff, 6, consensus.genesisValue, consensus.genesisOutputs);
        assert(consensus.genesisValue == genesis.vtx[0]->GetValueOut());
        assert(consensus.genesisOutputs == static_cast<int>(genesis.vtx[0]->vout.size()));
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.hashGenesisTx = genesis.vtx[0]->GetHash();
        consensus.genesisPubKey = genesis.vtx[0]->vout[0].scriptPubKey;
        consensus.genesisNTime = genesis.nTime;
        assert(consensus.hashGenesisBlock == uint256S("0x00000000ac42d8edb3d3998251a90bde339d0d715c0987e2947a8d7e47de7280"));
        assert(genesis.hashMerkleRoot == uint256S("0x5758f1c6ff152187c10509e5ac4a9ac3c19c5e178e89463f0a2b4bba17e778bd"));
        consensus.hashPeercoinSnapshot = uint256S("0xf482e77541bb103674f1d53bd6fd634e00411f563e864648999597114c38d0c9");

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        // vSeeds.emplace_back("tseed.peercoin.net");
        // vSeeds.emplace_back("tseed2.peercoin.net");
        // vSeeds.emplace_back("tseed.peercoin-library.org");
        // vSeeds.emplace_back("testseed.ppcoin.info");
        vSeeds.emplace_back("tseed.patchcoin.org");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // human readable prefix to bench32 address
        bech32_hrp = "tpc";

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {     0, uint256S("0x43ac9fa872333bd6d6e09488926237fdb73903d2155a34b5b8e4a0493e17fd7c")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data as of block c41259778268fe3bba597c7707fe400d9a7d66b4d6d2a9593c898fd69fb56a5f (height 573702)
            1710721794, // * UNIX timestamp of last known number of transactions
            1143673,    // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.003135995 // * estimated number of transactions per second after that timestamp
                        //   1143673/(1710721794-1346029522) = 0.003135995

        };
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        if (!options.challenge) {
            bin = ParseHex("512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae");
            vSeeds.emplace_back("seed.signet.bitcoin.sprovoost.nl.");

            // Hardcoded nodes can be removed once there are more DNS seeds
            vSeeds.emplace_back("178.128.221.177");
            vSeeds.emplace_back("v7ajjeirttkbnt32wpy3c6w3emwnfr3fkla7hpxcfokr3ysd3kqtzmqd.onion:38333");

            consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000001291fc22898");
            consensus.defaultAssumeValid = uint256S("0x000000d1a0e224fa4679d2fb2187ba55431c284fa1b74cbc8cfda866fd4d2c09"); // 105495
            m_assumed_blockchain_size = 1;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 000000d1a0e224fa4679d2fb2187ba55431c284fa1b74cbc8cfda866fd4d2c09
                .nTime    = 1661702566,
                .nTxCount = 1903567,
                .dTxRate  = 0.02336701143027275,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        strNetworkID = CBaseChainParams::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        //consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        //consensus.nTargetTimespan = 7 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("00000377ae000000000000000000000000000000000000000000000000000000");
/*
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay
*/
        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        memcpy(pchMessageStart, hash.begin(), 4);

        nDefaultPort = 38333;

        consensus.genesisValue = 21000000 * COIN;
        consensus.genesisOutputs = 8750;
        genesis = CreateGenesisBlock(1732230000, 244683519u,  0x1d00ffff, 6, consensus.genesisValue, consensus.genesisOutputs);
        assert(consensus.genesisValue == genesis.vtx[0]->GetValueOut());
        assert(consensus.genesisOutputs == static_cast<int>(genesis.vtx[0]->vout.size()));
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.hashGenesisTx = genesis.vtx[0]->GetHash();
        consensus.genesisPubKey = genesis.vtx[0]->vout[0].scriptPubKey;
        consensus.genesisNTime = genesis.nTime;
        assert(consensus.hashGenesisBlock == uint256S("0x00000000ac42d8edb3d3998251a90bde339d0d715c0987e2947a8d7e47de7280"));
        assert(genesis.hashMerkleRoot == uint256S("0x5758f1c6ff152187c10509e5ac4a9ac3c19c5e178e89463f0a2b4bba17e778bd"));
        consensus.hashPeercoinSnapshot = uint256S("0xf482e77541bb103674f1d53bd6fd634e00411f563e864648999597114c38d0c9");

        vFixedSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = false;
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        //consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.bnInitialHashTarget = uint256S("00000007ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 29;

        consensus.nTargetTimespan = 7 * 24 * 60 * 60; // two weeks
        consensus.nStakeTargetSpacing = 10 * 60; // 10-minute block spacing
        consensus.nTargetSpacingWorkMax = 12 * consensus.nStakeTargetSpacing; // 2-hour
        consensus.nPowTargetSpacing = consensus.nStakeTargetSpacing;

        consensus.nStakeMinAge = 60 * 60 * 24; // test net min age is 1 day
        consensus.nStakeMaxAge = 60 * 60 * 24 * 90;
        consensus.nStakeGenesisLockTime = 60 * 60 * 24 * 180 + consensus.nStakeMaxAge;
        consensus.nModifierInterval = 60 * 20; // Modifier interval: time to elapse before new modifier is computed
        consensus.nCoinbaseMaturity = 60;

        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0x91;
        pchMessageStart[1] = 0xce;
        pchMessageStart[2] = 0x91;
        pchMessageStart[3] = 0xa6;
        nDefaultPort = 7803;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        consensus.genesisValue = 21000000 * COIN;
        consensus.genesisOutputs = 8750;
        genesis = CreateGenesisBlock(1740006000 - consensus.nStakeMaxAge /* 1732230000 */, 244683519u,  0x1d00ffff, 6, consensus.genesisValue, consensus.genesisOutputs);
        assert(consensus.genesisValue == genesis.vtx[0]->GetValueOut());
        assert(consensus.genesisOutputs == static_cast<int>(genesis.vtx[0]->vout.size()));
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.hashGenesisTx = genesis.vtx[0]->GetHash();
        consensus.genesisPubKey = genesis.vtx[0]->vout[0].scriptPubKey;
        consensus.genesisNTime = genesis.nTime;
        assert(consensus.hashGenesisBlock == uint256S("0x00000000ac42d8edb3d3998251a90bde339d0d715c0987e2947a8d7e47de7280"));
        assert(genesis.hashMerkleRoot == uint256S("0x5758f1c6ff152187c10509e5ac4a9ac3c19c5e178e89463f0a2b4bba17e778bd"));
        consensus.hashPeercoinSnapshot = uint256S("0xf482e77541bb103674f1d53bd6fd634e00411f563e864648999597114c38d0c9");

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = true;

        fMiningRequiresPeers = false;

        checkpointData = {
            {
                {0, uint256S("0x6e5e8651e6e85c0d820c1e6ec716489dde2c9368aeba741d24e3dd68f720a7a4")}
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
            {
                110,
                {AssumeutxoHash{uint256S("0x1ebbf5850204c0bdb15bf030f47c7fe91d45c44c712697e4509ba67adb01c618")}, 110},
            },
            {
                200,
                {AssumeutxoHash{uint256S("0x51c8d11d8b5c1de51543c579736e786aa2736206d1e11e627568029ce092cf62")}, 200},
            },
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "pcrt";
    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
}
