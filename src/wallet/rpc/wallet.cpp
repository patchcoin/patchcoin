// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
//#include <interfaces/wallet.h>
#include <claimset.h>
#include <key_io.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <timedata.h>
#include <util/system.h>
#include <util/translation.h>
#include <wallet/context.h>
#include <wallet/receive.h>
#include <wallet/rpc/wallet.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>

#include <optional>

#include <univalue.h>

#include <kernelrecord.h>
#include <node/miner.h>
#include <boost/lexical_cast.hpp>
#include <index/claimindex.h>

using wallet::WalletContext;

namespace wallet {

static const std::map<uint64_t, std::string> WALLET_FLAG_CAVEATS{
    {WALLET_FLAG_AVOID_REUSE,
     "You need to rescan the blockchain in order to correctly mark used "
     "destinations in the past. Until this is done, some destinations may "
     "be considered unused, even if the opposite is the case."},
};

/** Checks if a CKey is in the given CWallet compressed or otherwise*/
bool HaveKey(const SigningProvider& wallet, const CKey& key)
{
    CKey key2;
    key2.Set(key.begin(), key.end(), !key.IsCompressed());
    return wallet.HaveKey(key.GetPubKey().GetID()) || wallet.HaveKey(key2.GetPubKey().GetID());
}

static RPCHelpMan getwalletinfo()
{
    return RPCHelpMan{"getwalletinfo",
                "Returns an object containing various wallet state info.\n",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {
                        {RPCResult::Type::STR, "walletname", "the wallet name"},
                        {RPCResult::Type::NUM, "walletversion", "the wallet version"},
                        {RPCResult::Type::STR, "format", "the database format (bdb or sqlite)"},
                        {RPCResult::Type::STR_AMOUNT, "balance", "DEPRECATED. Identical to getbalances().mine.trusted"},
                        {RPCResult::Type::STR_AMOUNT, "unconfirmed_balance", "DEPRECATED. Identical to getbalances().mine.untrusted_pending"},
                        {RPCResult::Type::STR_AMOUNT, "immature_balance", "DEPRECATED. Identical to getbalances().mine.immature"},
                        {RPCResult::Type::NUM, "txcount", "the total number of transactions in the wallet"},
                        {RPCResult::Type::NUM_TIME, "keypoololdest", /*optional=*/true, "the " + UNIX_EPOCH_TIME + " of the oldest pre-generated key in the key pool. Legacy wallets only."},
                        {RPCResult::Type::NUM, "keypoolsize", "how many new keys are pre-generated (only counts external keys)"},
                        {RPCResult::Type::NUM, "keypoolsize_hd_internal", /*optional=*/true, "how many new keys are pre-generated for internal use (used for change outputs, only appears if the wallet is using this feature, otherwise external keys are used)"},
                        {RPCResult::Type::NUM_TIME, "unlocked_until", /*optional=*/true, "the " + UNIX_EPOCH_TIME + " until which the wallet is unlocked for transfers, or 0 if the wallet is locked (only present for passphrase-encrypted wallets)"},
                        {RPCResult::Type::STR_HEX, "hdseedid", /*optional=*/true, "the Hash160 of the HD seed (only present when HD is enabled)"},
                        {RPCResult::Type::BOOL, "private_keys_enabled", "false if privatekeys are disabled for this wallet (enforced watch-only wallet)"},
                        {RPCResult::Type::BOOL, "unlocked_minting_only", /*optional=*/true, "true if wallet is unlocked for minting only"},
                        {RPCResult::Type::BOOL, "avoid_reuse", "whether this wallet tracks clean/dirty coins in terms of reuse"},
                        {RPCResult::Type::OBJ, "scanning", "current scanning details, or false if no scan is in progress",
                        {
                            {RPCResult::Type::NUM, "duration", "elapsed seconds since scan start"},
                            {RPCResult::Type::NUM, "progress", "scanning progress percentage [0.0, 1.0]"},
                        }, /*skip_type_check=*/true},
                        {RPCResult::Type::BOOL, "descriptors", "whether this wallet uses descriptors for scriptPubKey management"},
                        {RPCResult::Type::BOOL, "external_signer", "whether this wallet is configured to use an external signer such as a hardware wallet"},
                    }},
                },
                RPCExamples{
                    HelpExampleCli("getwalletinfo", "")
            + HelpExampleRpc("getwalletinfo", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    UniValue obj(UniValue::VOBJ);

    size_t kpExternalSize = pwallet->KeypoolCountExternalKeys();
    const auto bal = GetBalance(*pwallet);
    obj.pushKV("walletname", pwallet->GetName());
    obj.pushKV("walletversion", pwallet->GetVersion());
    obj.pushKV("format", pwallet->GetDatabase().Format());
    obj.pushKV("balance", ValueFromAmount(bal.m_mine_trusted));
    obj.pushKV("unconfirmed_balance", ValueFromAmount(bal.m_mine_untrusted_pending));
    obj.pushKV("immature_balance", ValueFromAmount(bal.m_mine_immature));
    obj.pushKV("txcount",       (int)pwallet->mapWallet.size());
    const auto kp_oldest = pwallet->GetOldestKeyPoolTime();
    if (kp_oldest.has_value()) {
        obj.pushKV("keypoololdest", kp_oldest.value());
    }
    obj.pushKV("keypoolsize", (int64_t)kpExternalSize);

    LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();
    if (spk_man) {
        CKeyID seed_id = spk_man->GetHDChain().seed_id;
        if (!seed_id.IsNull()) {
            obj.pushKV("hdseedid", seed_id.GetHex());
        }
    }

    if (pwallet->CanSupportFeature(FEATURE_HD_SPLIT)) {
        obj.pushKV("keypoolsize_hd_internal",   (int64_t)(pwallet->GetKeyPoolSize() - kpExternalSize));
    }
    if (pwallet->IsCrypted()) {
        obj.pushKV("unlocked_until", pwallet->nRelockTime);
        obj.pushKV("unlocked_minting_only", fWalletUnlockMintOnly);
    }
    obj.pushKV("private_keys_enabled", !pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    obj.pushKV("avoid_reuse", pwallet->IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE));
    if (pwallet->IsScanning()) {
        UniValue scanning(UniValue::VOBJ);
        scanning.pushKV("duration", pwallet->ScanningDuration() / 1000);
        scanning.pushKV("progress", pwallet->ScanningProgress());
        obj.pushKV("scanning", scanning);
    } else {
        obj.pushKV("scanning", false);
    }
    obj.pushKV("descriptors", pwallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS));
    obj.pushKV("external_signer", pwallet->IsWalletFlagSet(WALLET_FLAG_EXTERNAL_SIGNER));
    return obj;
},
    };
}

static RPCHelpMan listwalletdir()
{
    return RPCHelpMan{"listwalletdir",
                "Returns a list of wallets in the wallet directory.\n",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::ARR, "wallets", "",
                        {
                            {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR, "name", "The wallet name"},
                            }},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listwalletdir", "")
            + HelpExampleRpc("listwalletdir", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue wallets(UniValue::VARR);
    for (const auto& path : ListDatabases(GetWalletDir())) {
        UniValue wallet(UniValue::VOBJ);
        wallet.pushKV("name", path.u8string());
        wallets.push_back(wallet);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("wallets", wallets);
    return result;
},
    };
}

static RPCHelpMan listwallets()
{
    return RPCHelpMan{"listwallets",
                "Returns a list of currently loaded wallets.\n"
                "For full information on the wallet, use \"getwalletinfo\"\n",
                {},
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::STR, "walletname", "the wallet name"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listwallets", "")
            + HelpExampleRpc("listwallets", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue obj(UniValue::VARR);

    WalletContext& context = EnsureWalletContext(request.context);
    for (const std::shared_ptr<CWallet>& wallet : GetWallets(context)) {
        LOCK(wallet->cs_wallet);
        obj.push_back(wallet->GetName());
    }

    return obj;
},
    };
}

static RPCHelpMan loadwallet()
{
    return RPCHelpMan{"loadwallet",
                "\nLoads a wallet from a wallet file or directory."
                "\nNote that all wallet command-line options used when starting peercoind will be"
                "\napplied to the new wallet.\n",
                {
                    {"filename", RPCArg::Type::STR, RPCArg::Optional::NO, "The wallet directory or .dat file."},
                    {"load_on_startup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "name", "The wallet name if loaded successfully."},
                        {RPCResult::Type::STR, "warning", /*optional=*/true, "Warning messages, if any, related to loading the wallet. Multiple messages will be delimited by newlines. (DEPRECATED, returned only if config option -deprecatedrpc=walletwarningfield is passed.)"},
                        {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Warning messages, if any, related to loading the wallet.",
                        {
                            {RPCResult::Type::STR, "", ""},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("loadwallet", "\"test.dat\"")
            + HelpExampleRpc("loadwallet", "\"test.dat\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    WalletContext& context = EnsureWalletContext(request.context);
    const std::string name(request.params[0].get_str());

    DatabaseOptions options;
    DatabaseStatus status;
    ReadDatabaseArgs(*context.args, options);
    options.require_existing = true;
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    std::optional<bool> load_on_start = request.params[1].isNull() ? std::nullopt : std::optional<bool>(request.params[1].get_bool());

    {
        LOCK(context.wallets_mutex);
        if (std::any_of(context.wallets.begin(), context.wallets.end(), [&name](const auto& wallet) { return wallet->GetName() == name; })) {
            throw JSONRPCError(RPC_WALLET_ALREADY_LOADED, "Wallet \"" + name + "\" is already loaded.");
        }
    }

    std::shared_ptr<CWallet> const wallet = LoadWallet(context, name, load_on_start, options, status, error, warnings);

    HandleWalletError(wallet, status, error);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("name", wallet->GetName());
    if (wallet->chain().rpcEnableDeprecated("walletwarningfield")) {
        obj.pushKV("warning", Join(warnings, Untranslated("\n")).original);
    }
    PushWarnings(warnings, obj);

    return obj;
},
    };
}

static RPCHelpMan setwalletflag()
{
            std::string flags;
            for (auto& it : WALLET_FLAG_MAP)
                if (it.second & MUTABLE_WALLET_FLAGS)
                    flags += (flags == "" ? "" : ", ") + it.first;

    return RPCHelpMan{"setwalletflag",
                "\nChange the state of the given wallet flag for a wallet.\n",
                {
                    {"flag", RPCArg::Type::STR, RPCArg::Optional::NO, "The name of the flag to change. Current available flags: " + flags},
                    {"value", RPCArg::Type::BOOL, RPCArg::Default{true}, "The new state."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "flag_name", "The name of the flag that was modified"},
                        {RPCResult::Type::BOOL, "flag_state", "The new state of the flag"},
                        {RPCResult::Type::STR, "warnings", /*optional=*/true, "Any warnings associated with the change"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("setwalletflag", "avoid_reuse")
                  + HelpExampleRpc("setwalletflag", "\"avoid_reuse\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    std::string flag_str = request.params[0].get_str();
    bool value = request.params[1].isNull() || request.params[1].get_bool();

    if (!WALLET_FLAG_MAP.count(flag_str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Unknown wallet flag: %s", flag_str));
    }

    auto flag = WALLET_FLAG_MAP.at(flag_str);

    if (!(flag & MUTABLE_WALLET_FLAGS)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet flag is immutable: %s", flag_str));
    }

    UniValue res(UniValue::VOBJ);

    if (pwallet->IsWalletFlagSet(flag) == value) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet flag is already set to %s: %s", value ? "true" : "false", flag_str));
    }

    res.pushKV("flag_name", flag_str);
    res.pushKV("flag_state", value);

    if (value) {
        pwallet->SetWalletFlag(flag);
    } else {
        pwallet->UnsetWalletFlag(flag);
    }

    if (flag && value && WALLET_FLAG_CAVEATS.count(flag)) {
        res.pushKV("warnings", WALLET_FLAG_CAVEATS.at(flag));
    }

    return res;
},
    };
}

static RPCHelpMan createwallet()
{
    return RPCHelpMan{
        "createwallet",
        "\nCreates and loads a new wallet.\n",
        {
            {"wallet_name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name for the new wallet. If this is a path, the wallet will be created at the path location."},
            {"disable_private_keys", RPCArg::Type::BOOL, RPCArg::Default{false}, "Disable the possibility of private keys (only watchonlys are possible in this mode)."},
            {"blank", RPCArg::Type::BOOL, RPCArg::Default{false}, "Create a blank wallet. A blank wallet has no keys or HD seed. One can be set using sethdseed."},
            {"passphrase", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Encrypt the wallet with this passphrase."},
            {"avoid_reuse", RPCArg::Type::BOOL, RPCArg::Default{false}, "Keep track of coin reuse, and treat dirty and clean coins differently with privacy considerations in mind."},
            {"descriptors", RPCArg::Type::BOOL, RPCArg::Default{true}, "Create a native descriptor wallet. The wallet will use descriptors internally to handle address creation."
                                                                       " Setting to \"false\" will create a legacy wallet; however, the legacy wallet type is being deprecated and"
                                                                       " support for creating and opening legacy wallets will be removed in the future."},
            {"load_on_startup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
            {"external_signer", RPCArg::Type::BOOL, RPCArg::Default{false}, "Use an external signer such as a hardware wallet. Requires -signer to be configured. Wallet creation will fail if keys cannot be fetched. Requires disable_private_keys and descriptors set to true."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "name", "The wallet name if created successfully. If the wallet was created using a full path, the wallet_name will be the full path."},
                {RPCResult::Type::STR, "warning", /*optional=*/true, "Warning messages, if any, related to creating the wallet. Multiple messages will be delimited by newlines. (DEPRECATED, returned only if config option -deprecatedrpc=walletwarningfield is passed.)"},
                {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Warning messages, if any, related to creating the wallet.",
                {
                    {RPCResult::Type::STR, "", ""},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("createwallet", "\"testwallet\"")
            + HelpExampleRpc("createwallet", "\"testwallet\"")
            + HelpExampleCliNamed("createwallet", {{"wallet_name", "descriptors"}, {"avoid_reuse", true}, {"descriptors", true}, {"load_on_startup", true}})
            + HelpExampleRpcNamed("createwallet", {{"wallet_name", "descriptors"}, {"avoid_reuse", true}, {"descriptors", true}, {"load_on_startup", true}})
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    WalletContext& context = EnsureWalletContext(request.context);
    uint64_t flags = 0;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        flags |= WALLET_FLAG_DISABLE_PRIVATE_KEYS;
    }

    if (!request.params[2].isNull() && request.params[2].get_bool()) {
        flags |= WALLET_FLAG_BLANK_WALLET;
    }
    SecureString passphrase;
    passphrase.reserve(100);
    std::vector<bilingual_str> warnings;
    if (!request.params[3].isNull()) {
        passphrase = std::string_view{request.params[3].get_str()};
        if (passphrase.empty()) {
            // Empty string means unencrypted
            warnings.emplace_back(Untranslated("Empty string given as passphrase, wallet will not be encrypted."));
        }
    }

    if (!request.params[4].isNull() && request.params[4].get_bool()) {
        flags |= WALLET_FLAG_AVOID_REUSE;
    }
    if (request.params[5].isNull() || request.params[5].get_bool()) {
#ifndef USE_SQLITE
        throw JSONRPCError(RPC_WALLET_ERROR, "Compiled without sqlite support (required for descriptor wallets)");
#endif
        flags |= WALLET_FLAG_DESCRIPTORS;
    }
    if (!request.params[7].isNull() && request.params[7].get_bool()) {
#ifdef ENABLE_EXTERNAL_SIGNER
        flags |= WALLET_FLAG_EXTERNAL_SIGNER;
#else
        throw JSONRPCError(RPC_WALLET_ERROR, "Compiled without external signing support (required for external signing)");
#endif
    }

#ifndef USE_BDB
    if (!(flags & WALLET_FLAG_DESCRIPTORS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Compiled without bdb support (required for legacy wallets)");
    }
#endif

    DatabaseOptions options;
    DatabaseStatus status;
    ReadDatabaseArgs(*context.args, options);
    options.require_create = true;
    options.create_flags = flags;
    options.create_passphrase = passphrase;
    bilingual_str error;
    std::optional<bool> load_on_start = request.params[6].isNull() ? std::nullopt : std::optional<bool>(request.params[6].get_bool());
    const std::shared_ptr<CWallet> wallet = CreateWallet(context, request.params[0].get_str(), load_on_start, options, status, error, warnings);
    if (!wallet) {
        RPCErrorCode code = status == DatabaseStatus::FAILED_ENCRYPT ? RPC_WALLET_ENCRYPTION_FAILED : RPC_WALLET_ERROR;
        throw JSONRPCError(code, error.original);
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("name", wallet->GetName());
    if (wallet->chain().rpcEnableDeprecated("walletwarningfield")) {
        obj.pushKV("warning", Join(warnings, Untranslated("\n")).original);
    }
    PushWarnings(warnings, obj);

    return obj;
},
    };
}

static RPCHelpMan unloadwallet()
{
    return RPCHelpMan{"unloadwallet",
                "Unloads the wallet referenced by the request endpoint otherwise unloads the wallet specified in the argument.\n"
                "Specifying the wallet name on a wallet endpoint is invalid.",
                {
                    {"wallet_name", RPCArg::Type::STR, RPCArg::DefaultHint{"the wallet name from the RPC endpoint"}, "The name of the wallet to unload. If provided both here and in the RPC endpoint, the two must be identical."},
                    {"load_on_startup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
                },
                RPCResult{RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::STR, "warning", /*optional=*/true, "Warning messages, if any, related to unloading the wallet. Multiple messages will be delimited by newlines. (DEPRECATED, returned only if config option -deprecatedrpc=walletwarningfield is passed.)"},
                    {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Warning messages, if any, related to unloading the wallet.",
                    {
                        {RPCResult::Type::STR, "", ""},
                    }},
                }},
                RPCExamples{
                    HelpExampleCli("unloadwallet", "wallet_name")
            + HelpExampleRpc("unloadwallet", "wallet_name")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::string wallet_name;
    if (GetWalletNameFromJSONRPCRequest(request, wallet_name)) {
        if (!(request.params[0].isNull() || request.params[0].get_str() == wallet_name)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "RPC endpoint wallet and wallet_name parameter specify different wallets");
        }
    } else {
        wallet_name = request.params[0].get_str();
    }

    WalletContext& context = EnsureWalletContext(request.context);
    std::shared_ptr<CWallet> wallet = GetWallet(context, wallet_name);
    if (!wallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Requested wallet does not exist or is not loaded");
    }

    std::vector<bilingual_str> warnings;
    {
        WalletRescanReserver reserver(*wallet);
        if (!reserver.reserve()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
        }

        // Release the "main" shared pointer and prevent further notifications.
        // Note that any attempt to load the same wallet would fail until the wallet
        // is destroyed (see CheckUniqueFileid).
        std::optional<bool> load_on_start = request.params[1].isNull() ? std::nullopt : std::optional<bool>(request.params[1].get_bool());
        if (!RemoveWallet(context, wallet, load_on_start, warnings)) {
            throw JSONRPCError(RPC_MISC_ERROR, "Requested wallet already unloaded");
        }
    }
    UniValue result(UniValue::VOBJ);
    if (wallet->chain().rpcEnableDeprecated("walletwarningfield")) {
        result.pushKV("warning", Join(warnings, Untranslated("\n")).original);
    }
    PushWarnings(warnings, result);

    UnloadWallet(std::move(wallet));
    return result;
},
    };
}

static RPCHelpMan importcoinstake()
{
    return RPCHelpMan{"importcoinstake",
                "Import presigned coinstake for use in minting.\n",
                {
                    {"coinstake", RPCArg::Type::STR_HEX, RPCArg::DefaultHint{"signed coinstake"}, "signed coinstake transaction as hex."},
                    {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "timestamp when this coinstake will be valid."},
                },
                RPCResult{RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::STR, "txid", "transaction id if import is successful."},
                    {RPCResult::Type::NUM, "nTime", "timestamp when coinstake is due to mint."},
                }},
                RPCExamples{
                    HelpExampleCli("importcoinstake", "03000000")
            + HelpExampleRpc("importcoinstake", "03000000")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    // parse hex string from parameter
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));

    std::string err_string;
    AssertLockNotHeld(cs_main);

    {
        int timestamp;
        if (!request.params[1].isNull())
            timestamp = request.params[1].getInt<int>();
        else
            timestamp = tx->nTime;

        if (timestamp < GetTime()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Expired coinstake");
        }

        // check if we have the key to vout[1]
        std::set<ScriptPubKeyMan*> spk_mans = pwallet->GetScriptPubKeyMans(tx->vout[1].scriptPubKey);
        if (spk_mans.size() == 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No keys for vout[1]");
        }

        // add to in memory structure
        pwallet->m_coinstakes[timestamp] = tx;
    }
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", tx->GetHash().GetHex());
    result.pushKV("nTime", int(tx->nTime));
    return result;
},
    };
}


static RPCHelpMan listminting()
{
    return RPCHelpMan{"listminting",
                "Return all mintable outputs and provide details for each of them.\n",
                {
                    {"count", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "maximum number of outputs to be returned."},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "",
                        {
                            {RPCResult::Type::STR, "address", "Address of the output"},
                            {RPCResult::Type::STR, "input-txid", /*optional=*/true, "Transaction id"},
                            {RPCResult::Type::NUM, "time", "Time of transaction"},
                            {RPCResult::Type::NUM, "amount", "Amount of transaction output"},
                            {RPCResult::Type::STR, "status", "Status of transaction output"},
                            {RPCResult::Type::NUM, "age-in-day", /*optional=*/true, "Age of transaction in days"},
                            {RPCResult::Type::NUM, "coin-day-weight", /*optional=*/true, "Weight of transaction output"},
                            {RPCResult::Type::NUM, "proof-of-stake-difficulty", /*optional=*/true, "Current proof of stake difficulty"},
                            {RPCResult::Type::NUM, "minting-probability-10min", /*optional=*/true, "Probability of minting in next 10 minutes"},
                            {RPCResult::Type::NUM, "minting-probability-24h", /*optional=*/true, "Probability of minting in next 24 hours"},
                            {RPCResult::Type::NUM, "minting-probability-30d", /*optional=*/true, "Probability of minting in next 30 days"},
                            {RPCResult::Type::NUM, "minting-probability-90d", /*optional=*/true, "Probability of minting in next 90 days"},
                            {RPCResult::Type::NUM, "search-interval-in-sec", /*optional=*/true, "Interval between last minting attempts"},
                            {RPCResult::Type::NUM, "attempts", /*optional=*/true, "Number of seconds since maturity"},
                            {RPCResult::Type::NUM, "due-in-seconds", /*optional=*/true, "Number of seconds since maturity"},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listminting", "10")
            + HelpExampleRpc("listminting", "10")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    WalletContext& context = EnsureWalletContext(request.context);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    int64_t count=-1;
    if (!request.params[0].isNull())
        count = request.params[0].getInt<int>();

    UniValue ret(UniValue::VARR);

    double difficulty;
    {
        LOCK(cs_main);
        const CBlockIndex *p = GetLastBlockIndex(context.chain->chainman().ActiveChain().Tip(), true);
        difficulty = p->GetBlockDifficulty();
    }
    int64_t nStakeMinAge = Params().GetConsensus().nStakeMinAge;

    std::unique_ptr<interfaces::Wallet> iwallet = interfaces::MakeWallet(context,wallet);
    const auto& vwtx = iwallet->getWalletTxs();
    unsigned int nTime = TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime());

    for(const auto& wtx : vwtx) {
        std::vector<KernelRecord> txList = KernelRecord::decomposeOutput(*iwallet, wtx);

        int64_t minAge = nStakeMinAge / 60 / 60 / 24;


        for (auto& kr : txList) {
            if(!kr.spent) {

                if(count > 0 && (int32_t)ret.size() >= count) {
                    break;
                }

                std::string strTime = boost::lexical_cast<std::string>(kr.nTime);
                std::string strAmount = boost::lexical_cast<std::string>(kr.nValue);
                std::string strAge = boost::lexical_cast<std::string>(kr.getAge());
                std::string strCoinAge = boost::lexical_cast<std::string>(kr.getCoinAge());

//                JSONRPCRequest request2;
//                request2.params = UniValue(UniValue::VARR);
//                request2.params.push_back(kr.address);
//                std::string account = AccountFromValue(getaccount(request2));

                std::string status = "immature";
                int searchInterval = 0;
                int attemps = 0;
                if(kr.getAge() >=  minAge)
                {
                    status = "mature";
                    searchInterval = (int)nLastCoinStakeSearchInterval;
                    attemps = nTime - kr.nTime - nStakeMinAge;
                }

                UniValue obj(UniValue::VOBJ);
//                obj.push_back(Pair("account",                   account));
                obj.pushKV("address",                   kr.address);
                obj.pushKV("input-txid",                kr.hash.ToString());
                obj.pushKV("time",                      kr.nTime);
                obj.pushKV("amount",                    kr.nValue);
                obj.pushKV("status",                    status);
                obj.pushKV("age-in-day",                kr.getAge());
                obj.pushKV("coin-day-weight",           kr.getCoinAge());
                obj.pushKV("proof-of-stake-difficulty", difficulty);
                obj.pushKV("minting-probability-10min", kr.getProbToMintWithinNMinutes(difficulty, 10));
                obj.pushKV("minting-probability-24h",   kr.getProbToMintWithinNMinutes(difficulty, 60*24));
                obj.pushKV("minting-probability-30d",   kr.getProbToMintWithinNMinutes(difficulty, 60*24*30));
                obj.pushKV("minting-probability-90d",   kr.getProbToMintWithinNMinutes(difficulty, 60*24*90));
                obj.pushKV("search-interval-in-sec",    searchInterval);
                obj.pushKV("attempts",                  attemps);
                ret.push_back(obj);
            }
        }
    }

    if (pwallet->m_coinstakes.size()) {
        for (const auto& [timestamp, txn] : pwallet->m_coinstakes) {
            UniValue obj(UniValue::VOBJ);
            CTxDestination address;
            ExtractDestination(txn->vout[1].scriptPubKey, address);
            obj.pushKV("address", EncodeDestination(address));
            obj.pushKV("amount",  ValueFromAmount(txn->vout[1].nValue));
            obj.pushKV("status", "imported");
            obj.pushKV("time", (uint64_t)txn->nTime);
            obj.pushKV("due-in-seconds", (uint64_t)(txn->nTime - nTime));
            ret.push_back(obj);
        }
    }
    return ret;
},
    };
}

static RPCHelpMan reservebalance()
{
    return RPCHelpMan{"reservebalance",
                "Set reserve amount not participating in network protection.\n",
                {
                    {"reserve", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "turn balance reserve on or off."},
                    {"amount", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "amount of peercoin to be reserved."},
                },
                RPCResult{RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::BOOL, "reserve", "status of reserve."},
                    {RPCResult::Type::NUM, "amount", "amount of peercoin reserved."},
                }},
                RPCExamples{
                    HelpExampleCli("reservebalance", "true 10")
            + HelpExampleRpc("reservebalance", "true 10")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    WalletContext& context = EnsureWalletContext(request.context);
    ArgsManager& args = *Assert(context.args);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsUnlocked(*pwallet);

    if (request.params.size() > 0)
    {
        bool fReserve = request.params[0].get_bool();
        if (fReserve)
        {
            if (request.params.size() == 1)
                throw std::runtime_error("must provide amount to reserve balance.\n");
            int64_t nAmount = AmountFromValue(request.params[1]);
            nAmount = (nAmount / CENT) * CENT;  // round to cent
            if (nAmount < 0)
                throw std::runtime_error("amount cannot be negative.\n");
            args.ForceSetArg("-reservebalance", FormatMoney(nAmount));
        }
        else
        {
            if (request.params.size() > 1)
                throw std::runtime_error("cannot specify amount to turn off reserve.\n");
            args.ForceSetArg("-reservebalance", "0");
        }
    }

    UniValue result(UniValue::VOBJ);
    std::optional<CAmount> nReserveBalance = ParseMoney(args.GetArg("-reservebalance", ""));
    if (args.IsArgSet("-reservebalance") && !nReserveBalance)
        throw std::runtime_error("invalid reserve balance amount\n");
    result.pushKV("reserve", (nReserveBalance > 0));
    result.pushKV("amount", nReserveBalance ? nReserveBalance.value() : 0);
    return result;
},
    };
}

static RPCHelpMan sethdseed()
{
    return RPCHelpMan{"sethdseed",
                "\nSet or generate a new HD wallet seed. Non-HD wallets will not be upgraded to being a HD wallet. Wallets that are already\n"
                "HD will have a new HD seed set so that new keys added to the keypool will be derived from this new seed.\n"
                "\nNote that you will need to MAKE A NEW BACKUP of your wallet after setting the HD wallet seed." +
        HELP_REQUIRING_PASSPHRASE,
                {
                    {"newkeypool", RPCArg::Type::BOOL, RPCArg::Default{true}, "Whether to flush old unused addresses, including change addresses, from the keypool and regenerate it.\n"
                                         "If true, the next address from getnewaddress and change address from getrawchangeaddress will be from this new seed.\n"
                                         "If false, addresses (including change addresses if the wallet already had HD Chain Split enabled) from the existing\n"
                                         "keypool will be used until it has been depleted."},
                    {"seed", RPCArg::Type::STR, RPCArg::DefaultHint{"random seed"}, "The WIF private key to use as the new HD seed.\n"
                                         "The seed value can be retrieved using the dumpwallet command. It is the private key marked hdseed=1"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("sethdseed", "")
            + HelpExampleCli("sethdseed", "false")
            + HelpExampleCli("sethdseed", "true \"wifkey\"")
            + HelpExampleRpc("sethdseed", "true, \"wifkey\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LegacyScriptPubKeyMan& spk_man = EnsureLegacyScriptPubKeyMan(*pwallet, true);

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot set a HD seed to a wallet with private keys disabled");
    }

    LOCK2(pwallet->cs_wallet, spk_man.cs_KeyStore);

    // Do not do anything to non-HD wallets
    if (!pwallet->CanSupportFeature(FEATURE_HD)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot set an HD seed on a non-HD wallet. Use the upgradewallet RPC in order to upgrade a non-HD wallet to HD");
    }

    EnsureWalletIsUnlocked(*pwallet);

    bool flush_key_pool = true;
    if (!request.params[0].isNull()) {
        flush_key_pool = request.params[0].get_bool();
    }

    CPubKey master_pub_key;
    if (request.params[1].isNull()) {
        master_pub_key = spk_man.GenerateNewSeed();
    } else {
        CKey key = DecodeSecret(request.params[1].get_str());
        if (!key.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
        }

        if (HaveKey(spk_man, key)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Already have this key (either as an HD seed or as a loose private key)");
        }

        master_pub_key = spk_man.DeriveNewSeed(key);
    }

    spk_man.SetHDSeed(master_pub_key);
    if (flush_key_pool) spk_man.NewKeyPool();

    return UniValue::VNULL;
},
    };
}

static RPCHelpMan upgradewallet()
{
    return RPCHelpMan{"upgradewallet",
        "\nUpgrade the wallet. Upgrades to the latest version if no version number is specified.\n"
        "New keys may be generated and a new wallet backup will need to be made.",
        {
            {"version", RPCArg::Type::NUM, RPCArg::Default{int{FEATURE_LATEST}}, "The version number to upgrade to. Default is the latest wallet version."}
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "wallet_name", "Name of wallet this operation was performed on"},
                {RPCResult::Type::NUM, "previous_version", "Version of wallet before this operation"},
                {RPCResult::Type::NUM, "current_version", "Version of wallet after this operation"},
                {RPCResult::Type::STR, "result", /*optional=*/true, "Description of result, if no error"},
                {RPCResult::Type::STR, "error", /*optional=*/true, "Error message (if there is one)"}
            },
        },
        RPCExamples{
            HelpExampleCli("upgradewallet", "169900")
            + HelpExampleRpc("upgradewallet", "169900")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    EnsureWalletIsUnlocked(*pwallet);

    int version = 0;
    if (!request.params[0].isNull()) {
        version = request.params[0].getInt<int>();
    }
    bilingual_str error;
    const int previous_version{pwallet->GetVersion()};
    const bool wallet_upgraded{pwallet->UpgradeWallet(version, error)};
    const int current_version{pwallet->GetVersion()};
    std::string result;

    if (wallet_upgraded) {
        if (previous_version == current_version) {
            result = "Already at latest version. Wallet version unchanged.";
        } else {
            result = strprintf("Wallet upgraded successfully from version %i to version %i.", previous_version, current_version);
        }
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("wallet_name", pwallet->GetName());
    obj.pushKV("previous_version", previous_version);
    obj.pushKV("current_version", current_version);
    if (!result.empty()) {
        obj.pushKV("result", result);
    } else {
        CHECK_NONFATAL(!error.empty());
        obj.pushKV("error", error.original);
    }
    return obj;
},
    };
}

RPCHelpMan simulaterawtransaction()
{
    return RPCHelpMan{"simulaterawtransaction",
        "\nCalculate the balance change resulting in the signing and broadcasting of the given transaction(s).\n",
        {
            {"rawtxs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED, "An array of hex strings of raw transactions.\n",
                {
                    {"rawtx", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, ""},
                },
            },
            {"options", RPCArg::Type::OBJ_USER_KEYS, RPCArg::Optional::OMITTED, "Options",
                {
                    {"include_watchonly", RPCArg::Type::BOOL, RPCArg::DefaultHint{"true for watch-only wallets, otherwise false"}, "Whether to include watch-only addresses (see RPC importaddress)"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_AMOUNT, "balance_change", "The wallet balance change (negative means decrease)."},
            }
        },
        RPCExamples{
            HelpExampleCli("simulaterawtransaction", "[\"myhex\"]")
            + HelpExampleRpc("simulaterawtransaction", "[\"myhex\"]")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> rpc_wallet = GetWalletForJSONRPCRequest(request);
    if (!rpc_wallet) return UniValue::VNULL;
    const CWallet& wallet = *rpc_wallet;

    LOCK(wallet.cs_wallet);

    UniValue include_watchonly(UniValue::VNULL);
    if (request.params[1].isObject()) {
        UniValue options = request.params[1];
        RPCTypeCheckObj(options,
            {
                {"include_watchonly", UniValueType(UniValue::VBOOL)},
            },
            true, true);

        include_watchonly = options["include_watchonly"];
    }

    isminefilter filter = ISMINE_SPENDABLE;
    if (ParseIncludeWatchonly(include_watchonly, wallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    const auto& txs = request.params[0].get_array();
    CAmount changes{0};
    std::map<COutPoint, CAmount> new_utxos; // UTXO:s that were made available in transaction array
    std::set<COutPoint> spent;

    for (size_t i = 0; i < txs.size(); ++i) {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, txs[i].get_str(), /* try_no_witness */ true, /* try_witness */ true)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction hex string decoding failure.");
        }

        // Fetch previous transactions (inputs)
        std::map<COutPoint, Coin> coins;
        for (const CTxIn& txin : mtx.vin) {
            coins[txin.prevout]; // Create empty map entry keyed by prevout.
        }
        wallet.chain().findCoins(coins);

        // Fetch debit; we are *spending* these; if the transaction is signed and
        // broadcast, we will lose everything in these
        for (const auto& txin : mtx.vin) {
            const auto& outpoint = txin.prevout;
            if (spent.count(outpoint)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Transaction(s) are spending the same output more than once");
            }
            if (new_utxos.count(outpoint)) {
                changes -= new_utxos.at(outpoint);
                new_utxos.erase(outpoint);
            } else {
                if (coins.at(outpoint).IsSpent()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "One or more transaction inputs are missing or have been spent already");
                }
                changes -= wallet.GetDebit(txin, filter);
            }
            spent.insert(outpoint);
        }

        // Iterate over outputs; we are *receiving* these, if the wallet considers
        // them "mine"; if the transaction is signed and broadcast, we will receive
        // everything in these
        // Also populate new_utxos in case these are spent in later transactions

        const auto& hash = mtx.GetHash();
        for (size_t i = 0; i < mtx.vout.size(); ++i) {
            const auto& txout = mtx.vout[i];
            bool is_mine = 0 < (wallet.IsMine(txout) & filter);
            changes += new_utxos[COutPoint(hash, i)] = is_mine ? txout.nValue : 0;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("balance_change", ValueFromAmount(changes));

    return result;
}
    };
}

static RPCHelpMan migratewallet()
{
    return RPCHelpMan{"migratewallet",
        "EXPERIMENTAL warning: This call may not work as expected and may be changed in future releases\n"
        "\nMigrate the wallet to a descriptor wallet.\n"
        "A new wallet backup will need to be made.\n"
        "\nThe migration process will create a backup of the wallet before migrating. This backup\n"
        "file will be named <wallet name>-<timestamp>.legacy.bak and can be found in the directory\n"
        "for this wallet. In the event of an incorrect migration, the backup can be restored using restorewallet."
        "\nEncrypted wallets must have the passphrase provided as an argument to this call.",
        {
            {"wallet_name", RPCArg::Type::STR, RPCArg::DefaultHint{"the wallet name from the RPC endpoint"}, "The name of the wallet to migrate. If provided both here and in the RPC endpoint, the two must be identical."},
            {"passphrase", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The wallet passphrase"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "wallet_name", "The name of the primary migrated wallet"},
                {RPCResult::Type::STR, "watchonly_name", /*optional=*/true, "The name of the migrated wallet containing the watchonly scripts"},
                {RPCResult::Type::STR, "solvables_name", /*optional=*/true, "The name of the migrated wallet containing solvable but not watched scripts"},
                {RPCResult::Type::STR, "backup_path", "The location of the backup of the original wallet"},
            }
        },
        RPCExamples{
            HelpExampleCli("migratewallet", "")
            + HelpExampleRpc("migratewallet", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::string wallet_name;
            if (GetWalletNameFromJSONRPCRequest(request, wallet_name)) {
                if (!(request.params[0].isNull() || request.params[0].get_str() == wallet_name)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "RPC endpoint wallet and wallet_name parameter specify different wallets");
                }
            } else {
                if (request.params[0].isNull()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Either RPC endpoint wallet or wallet_name parameter must be provided");
                }
                wallet_name = request.params[0].get_str();
            }

            SecureString wallet_pass;
            wallet_pass.reserve(100);
            if (!request.params[1].isNull()) {
                wallet_pass = std::string_view{request.params[1].get_str()};
            }

            WalletContext& context = EnsureWalletContext(request.context);
            util::Result<MigrationResult> res = MigrateLegacyToDescriptor(wallet_name, wallet_pass, context);
            if (!res) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(res).original);
            }

            UniValue r{UniValue::VOBJ};
            r.pushKV("wallet_name", res->wallet_name);
            if (res->watchonly_wallet) {
                r.pushKV("watchonly_name", res->watchonly_wallet->GetName());
            }
            if (res->solvables_wallet) {
                r.pushKV("solvables_name", res->solvables_wallet->GetName());
            }
            r.pushKV("backup_path", res->backup_path.u8string());

            return r;
        },
    };
}

bool PopulateClaimAmounts(std::vector<CClaim>& claims)
{
    for (auto& claim : claims) {
        CTxDestination sourceDest;
        if (!ExtractDestination(claim.sourceScriptPubKey, sourceDest)) {
            claim.nEligible = 0;
            claim.nTotalReceived = 0;
            return false;
        }

        std::string addr = EncodeDestination(sourceDest);

        CAmount balance = 0;
        CAmount eligible = 0;
        if (LookupPeercoinScriptPubKey(claim.sourceScriptPubKey, balance, eligible)) {
            claim.nTotalReceived = balance;
            claim.nEligible = eligible;
        } else {
            claim.nTotalReceived = 0;
            claim.nEligible = 0;
        }
    }

    return true;
}

static RPCHelpMan buildclaimset()
{
    return RPCHelpMan{
        "buildclaimset",
        "\nGather all claims from g_claimindex, retrieve their eligible/original amounts, build a ClaimSet, and sign.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "claimset_hex", "Hex-encoded ClaimSet data"},
                {RPCResult::Type::STR_HEX, "signature",    "Signature over the ClaimSet"},
                {RPCResult::Type::NUM,     "num_claims",   "Number of claims included"},
                {RPCResult::Type::ARR,     "claims",       "Array of claims with addresses, amounts, etc.",
                    {
                        {RPCResult::Type::OBJ, "claim", "",
                            {
                                {RPCResult::Type::STR,       "source_address",   "Decoded address for source"},
                                {RPCResult::Type::STR,       "target_address",   "Decoded address for target"},
                                {RPCResult::Type::NUM,       "nTime",            "Claim timestamp"},
                                {RPCResult::Type::STR_AMOUNT,"coins_eligible",   "Eligible amount for distribution"},
                                {RPCResult::Type::STR_AMOUNT,"original_amount",  "Original or total amount tracked"},
                                {RPCResult::Type::STR_HEX,   "source_script",    "Hex-encoded sourceScriptPubKey"},
                                {RPCResult::Type::STR_HEX,   "target_script",    "Hex-encoded targetScriptPubKey"},
                                {RPCResult::Type::STR_HEX,   "signature",        "Hex-encoded signature"},
                            }
                        }
                    }
                }
            }
        },
        RPCExamples{
            HelpExampleCli("buildclaimset", "") +
            HelpExampleRpc("buildclaimset", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!g_claimindex) {
                throw JSONRPCError(RPC_MISC_ERROR, "ClaimIndex not available");
            }
            std::vector<CClaim> allClaims;
            if (!g_claimindex->GetAllClaims(allClaims)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Failed to retrieve claims from index");
            }
            if (allClaims.empty()) {
                throw JSONRPCError(RPC_MISC_ERROR, "No claims found");
            }

            PopulateClaimAmounts(allClaims);

            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            EnsureWalletIsUnlocked(*pwallet);

            CClaimSet claimset;
            try {
                claimset = BuildAndSignClaimSet(allClaims, *pwallet);
            } catch (const std::runtime_error& e) {
                claimset = BuildClaimSet(allClaims);
            }

            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << claimset;
            std::string claimsetHex = HexStr(ss);


            UniValue claimsArr(UniValue::VARR);
            for (const auto& c : claimset.claims) {
                UniValue claimObj(UniValue::VOBJ);

                {
                    CTxDestination srcDest;
                    if (ExtractDestination(c.sourceScriptPubKey, srcDest)) {
                        claimObj.pushKV("source_address", EncodeDestination(srcDest));
                    } else {
                        claimObj.pushKV("source_address", "unrecognized");
                    }
                }
                {
                    CTxDestination tgtDest;
                    if (ExtractDestination(c.targetScriptPubKey, tgtDest)) {
                        claimObj.pushKV("target_address", EncodeDestination(tgtDest));
                    } else {
                        claimObj.pushKV("target_address", "unrecognized");
                    }
                }

                claimObj.pushKV("nTime", (uint64_t)c.nTime);

                claimObj.pushKV("coins_eligible", ValueFromAmount(c.nEligible));
                claimObj.pushKV("original_amount", ValueFromAmount(c.nTotalReceived));

                claimObj.pushKV("source_script", HexStr(c.sourceScriptPubKey));
                claimObj.pushKV("target_script", HexStr(c.targetScriptPubKey));

                claimObj.pushKV("signature", HexStr(c.signature));

                claimsArr.push_back(claimObj);
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("claimset_hex", claimsetHex);
            result.pushKV("signature", HexStr(claimset.vchSig));
            result.pushKV("num_claims", (uint64_t)claimset.claims.size());
            result.pushKV("claims", claimsArr);

            return result;
        }
    };
}


// addresses
RPCHelpMan getaddressinfo();
RPCHelpMan getnewaddress();
RPCHelpMan getrawchangeaddress();
RPCHelpMan setlabel();
RPCHelpMan listaddressgroupings();
RPCHelpMan addmultisigaddress();
RPCHelpMan keypoolrefill();
RPCHelpMan newkeypool();
RPCHelpMan getaddressesbylabel();
RPCHelpMan listlabels();
#ifdef ENABLE_EXTERNAL_SIGNER
RPCHelpMan walletdisplayaddress();
#endif // ENABLE_EXTERNAL_SIGNER

// backup
RPCHelpMan dumpprivkey();
RPCHelpMan importprivkey();
RPCHelpMan importaddress();
RPCHelpMan importpubkey();
RPCHelpMan dumpwallet();
RPCHelpMan importwallet();
RPCHelpMan importmulti();
RPCHelpMan importdescriptors();
RPCHelpMan listdescriptors();
RPCHelpMan backupwallet();
RPCHelpMan restorewallet();

// coins
RPCHelpMan getreceivedbyaddress();
RPCHelpMan getreceivedbylabel();
RPCHelpMan getbalance();
RPCHelpMan getunconfirmedbalance();
RPCHelpMan lockunspent();
RPCHelpMan listlockunspent();
RPCHelpMan getbalances();
RPCHelpMan listunspent();

// encryption
RPCHelpMan walletpassphrase();
RPCHelpMan walletpassphrasechange();
RPCHelpMan walletlock();
RPCHelpMan encryptwallet();

// spend
RPCHelpMan sendtoaddress();
RPCHelpMan sendmany();
RPCHelpMan fundrawtransaction();
RPCHelpMan send();
RPCHelpMan sendall();
RPCHelpMan walletprocesspsbt();
RPCHelpMan walletcreatefundedpsbt();
RPCHelpMan signrawtransactionwithwallet();
RPCHelpMan settxfee();
RPCHelpMan optimizeutxoset();

// signmessage
RPCHelpMan signmessage();

// transactions
RPCHelpMan listreceivedbyaddress();
RPCHelpMan listreceivedbylabel();
RPCHelpMan listtransactions();
RPCHelpMan listsinceblock();
RPCHelpMan gettransaction();
RPCHelpMan abandontransaction();
RPCHelpMan rescanblockchain();
RPCHelpMan abortrescan();

Span<const CRPCCommand> GetWalletRPCCommands()
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category              actor (function)
  //  ------------------    ------------------------
    { "rawtransactions",    &fundrawtransaction,             },
    { "wallet",             &abandontransaction,             },
    { "wallet",             &abortrescan,                    },
    { "wallet",             &addmultisigaddress,             },
    { "wallet",             &backupwallet,                   },
    { "wallet",             &createwallet,                   },
    { "wallet",             &restorewallet,                  },
    { "wallet",             &dumpprivkey,                    },
    { "wallet",             &dumpwallet,                     },
    { "wallet",             &encryptwallet,                  },
    { "wallet",             &getaddressesbylabel,            },
    { "wallet",             &getaddressinfo,                 },
    { "wallet",             &getbalance,                     },
    { "wallet",             &getnewaddress,                  },
    { "wallet",             &getrawchangeaddress,            },
    { "wallet",             &getreceivedbyaddress,           },
    { "wallet",             &getreceivedbylabel,             },
    { "wallet",             &gettransaction,                 },
    { "wallet",             &getunconfirmedbalance,          },
    { "wallet",             &getbalances,                    },
    { "wallet",             &getwalletinfo,                  },
    { "wallet",             &importaddress,                  },
    { "wallet",             &importdescriptors,              },
    { "wallet",             &importmulti,                    },
    { "wallet",             &importprivkey,                  },
    { "wallet",             &importpubkey,                   },
    { "wallet",             &importwallet,                   },
    { "wallet",             &keypoolrefill,                  },
    { "wallet",             &listaddressgroupings,           },
    { "wallet",             &listdescriptors,                },
    { "wallet",             &listlabels,                     },
    { "wallet",             &listlockunspent,                },
    { "wallet",             &listreceivedbyaddress,          },
    { "wallet",             &listreceivedbylabel,            },
    { "wallet",             &listsinceblock,                 },
    { "wallet",             &listtransactions,               },
    { "wallet",             &listunspent,                    },
    { "wallet",             &listwalletdir,                  },
    { "wallet",             &listwallets,                    },
    { "wallet",             &loadwallet,                     },
    { "wallet",             &lockunspent,                    },
    { "wallet",             &migratewallet,                  },
    { "wallet",             &newkeypool,                     },
    { "wallet",             &optimizeutxoset,                },
    { "wallet",             &rescanblockchain,               },
    { "wallet",             &send,                           },
    { "wallet",             &sendmany,                       },
    { "wallet",             &sendtoaddress,                  },
    { "wallet",             &sethdseed,                      },
    { "wallet",             &setlabel,                       },
    { "wallet",             &setwalletflag,                  },
    { "wallet",             &settxfee,                       },
    { "wallet",             &signmessage,                    },
    { "wallet",             &signrawtransactionwithwallet,   },
    { "wallet",             &sendall,                        },
    { "wallet",             &unloadwallet,                   },
    { "wallet",             &upgradewallet,                  },
    { "wallet",             &walletcreatefundedpsbt,         },
#ifdef ENABLE_EXTERNAL_SIGNER
    { "wallet",             &walletdisplayaddress,           },
#endif // ENABLE_EXTERNAL_SIGNER
    { "wallet",             &walletlock,                     },
    { "wallet",             &walletpassphrase,               },
    { "wallet",             &walletpassphrasechange,         },
    { "wallet",             &walletprocesspsbt,              },
    // peercoin commands
    { "wallet",             &importcoinstake,                },
    { "wallet",             &listminting,                    },
    { "wallet",             &reservebalance,                 },
    { "wallet",             &buildclaimset,                  },
};
// clang-format on
    return commands;
}
} // namespace wallet
