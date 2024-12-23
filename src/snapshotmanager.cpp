#include <snapshotmanager.h>
#include <util/system.h>
#include <logging.h>
#include <chainparams.h>
#include <key_io.h>
#include <shutdown.h>
#include <script/standard.h>

// std::map<CScript, std::vector<fCoinEntry>> foreignSnapshotByScript;
std::map<std::string, std::vector<fCoinEntry>> foreignSnapshotByAddress;

bool LoadSnapshotOnStartup(const ArgsManager& args) {
    std::string snapshotPath = args.GetArg("-snapshotfile", "");

    if (snapshotPath.empty()) {
        fs::path defaultPath = args.GetDataDirNet() / "peercoin_utxos.dat";
        if (fs::exists(defaultPath)) {
            snapshotPath = defaultPath;
            LogPrintf("LoadSnapshotOnStartup: found default snapshot at %s\n", snapshotPath);
        }
    }

    if (snapshotPath.empty()) {
        LogPrintf("LoadSnapshotOnStartup: no snapshot file provided and none found.\n");
        return true;
    }

    return LoadSnapshotFromFile(snapshotPath);
}

bool LoadSnapshotFromFile(const std::string& filePath) {
    fs::path snapFilePath(fs::u8path(filePath));
    if (!fs::exists(snapFilePath)) {
        LogPrintf("LoadSnapshotFromFile: file not found: %s\n", filePath);
        return false;
    }

    AutoFile coins_file(fsbridge::fopen(snapFilePath, "rb"));

    node::SnapshotMetadata metadata;
    try {
        coins_file >> metadata;
    } catch (const std::ios_base::failure& e) {
        LogPrintf("LoadSnapshotFromFile: Unable to parse metadata: %s\n", filePath);
        return false;
    }

    if (!PopulateAndValidateSnapshotForeign(coins_file, metadata)) {
        LogPrintf("LoadSnapshotFromFile: Validation failed for snapshot: %s\n", filePath);
        return false;
    }

    LogPrintf("LoadSnapshotFromFile: Successfully loaded snapshot from %s\n", filePath);
    return true;
}

bool GetAddressFromScriptPubKey(const CScript& scriptPubKey, std::string& outAddress) {
    outAddress.clear();

    CTxDestination dest;
    if (ExtractDestination(scriptPubKey, dest)) {
        outAddress = EncodeDestination(dest);
        return true;
    }

    std::vector<std::vector<unsigned char>> solutions;
    TxoutType whichType = Solver(scriptPubKey, solutions);
    if (whichType == TxoutType::PUBKEY && solutions.size() == 1) {
        CPubKey pubkey(solutions[0]);
        if (pubkey.IsFullyValid()) {
            auto keyid = pubkey.GetID();
            CTxDestination fallbackDest = PKHash(keyid);
            outAddress = EncodeDestination(fallbackDest);
            return true;
        }
    }

    LogPrintf("GetAddressFromScriptPubKey: Could not parse scriptPubKey: %s\n", HexStr(scriptPubKey));
    return false;
}

bool PopulateAndValidateSnapshotForeign(
    AutoFile& coins_file,
    const node::SnapshotMetadata& metadata) {
    uint256 base_blockhash = metadata.m_base_blockhash;
    const uint64_t coins_count = metadata.m_coins_count;
    uint64_t coins_left = coins_count;

    LogPrintf("[snapshot] loading coins from snapshot %s\n", base_blockhash.ToString());

    // foreignSnapshotByScript.clear();
    foreignSnapshotByAddress.clear();

    COutPoint outpoint;
    Coin coin;
    int64_t coins_processed = 0;

    while (coins_left > 0) {
        try {
            coins_file >> outpoint;
            coins_file >> coin;
        } catch (const std::ios_base::failure&) {
            LogPrintf("[snapshot] bad snapshot format or truncated snapshot after deserializing %d coins\n", coins_count - coins_left);
            return false;
        }

        if (outpoint.n >= std::numeric_limits<decltype(outpoint.n)>::max()) {
            LogPrintf("[snapshot] bad snapshot data after deserializing %d coins\n", coins_count - coins_left);
            return false;
        }

        if (coin.out.nValue > 0) {
            // foreignSnapshotByScript[coin.out.scriptPubKey].emplace_back(outpoint, coin);

            std::string addressStr;
            if (GetAddressFromScriptPubKey(coin.out.scriptPubKey, addressStr)) {
                foreignSnapshotByAddress[addressStr].emplace_back(std::move(outpoint), std::move(coin));
            } else {
                foreignSnapshotByAddress["unrecognized_script"].emplace_back(std::move(outpoint), std::move(coin));
            }
        }

        --coins_left;
        ++coins_processed;

        if (coins_processed % 1000000 == 0) {
            LogPrintf("[snapshot] %d coins loaded (%.2f%%)\n",
                      coins_processed,
                      static_cast<float>(coins_processed) * 100 / static_cast<float>(coins_count));
        }

        if (coins_processed % 120000 == 0) {
            if (ShutdownRequested()) {
                return false;
            }
        }
    }

    bool out_of_coins = false;
    try {
        coins_file >> outpoint;
    } catch (const std::ios_base::failure&) {
        out_of_coins = true;
    }
    if (!out_of_coins) {
        LogPrintf("[snapshot] bad snapshot - coins left over after "
                  "deserializing %d coins\n", coins_count);
        return false;
    }

    LogPrintf("[snapshot] loaded %d coins from snapshot %s\n",
              coins_processed, base_blockhash.ToString());

    return true;
}

CAmount LookupPeercoinAddress(const std::string& address) {
    const auto it = foreignSnapshotByAddress.find(address);
    if (it == foreignSnapshotByAddress.end()) {
        return 0;
    }

    CAmount sum = 0;
    for (const fCoinEntry& entry : it->second) {
        sum += entry.coin.out.nValue;
    }
    assert(MoneyRange(sum));
    return sum;
}
