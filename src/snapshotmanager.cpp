#include <snapshotmanager.h>
#include <util/system.h>
#include <logging.h>
#include <chainparams.h>
#include <key_io.h>
#include <serialize.h>
#include <shutdown.h>
#include <script/standard.h>
#include <fstream>
#include <rpc/server_util.h>
#include <util/moneystr.h>

// std::map<CScript, std::vector<fCoinEntry>> foreignSnapshotByScript;
std::map<std::string, std::vector<fCoinEntry>> foreignSnapshotByAddress;

bool LoadSnapshotOnStartup(const ArgsManager& args) {
    const fs::path path = args.GetDataDirNet() / "peercoin_utxos.dat";
    if (fs::exists(path)) {
        LogPrintf("LoadSnapshotOnStartup: found default snapshot at %s\n", fs::PathToString(path));
    }

    if (path.empty()) {
        LogPrintf("LoadSnapshotOnStartup: no snapshot file provided and none found.\n");
        return true;
    }

    return LoadSnapshotFromFile(path);
}

bool LoadSnapshotFromFile(const fs::path& path) {
    if (!fs::exists(path)) {
        LogPrintf("LoadSnapshotFromFile: file not found: %s\n", fs::PathToString(path));
        return false;
    }

    AutoFile coins_file(fsbridge::fopen(path, "rb"));

    node::SnapshotMetadata metadata;
    try {
        coins_file >> metadata;
    } catch (const std::ios_base::failure& e) {
        LogPrintf("LoadSnapshotFromFile: Unable to parse metadata: %s\n", fs::PathToString(path));
        return false;
    }

    if (!PopulateAndValidateSnapshotForeign(coins_file, metadata)) {
        LogPrintf("LoadSnapshotFromFile: Validation failed for snapshot: %s\n", fs::PathToString(path));
        return false;
    }
    coins_file.fclose();

    LogPrintf("LoadSnapshotFromFile: Successfully loaded snapshot from %s\n", fs::PathToString(path));
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

bool LookupPeercoinAddress(const std::string& address, CAmount& balance, CAmount& eligible) {
    const auto it = foreignSnapshotByAddress.find(address);
    if (it == foreignSnapshotByAddress.end())
        return false;

    CAmount sum = 0;
    for (const fCoinEntry& entry : it->second)
        sum += entry.coin.out.nValue;

    if (!MoneyRange(sum))
        return false;

    balance = sum;
    eligible = std::min(balance * 10, 5000 * COIN);

    if (!MoneyRange(eligible)) {
        eligible = 0;
        return false;
    }

    return true;
}

std::string FormatCustomMoney(CAmount amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << static_cast<double>(amount) / COIN;
    return oss.str();
}

void ExportSnapshotToCSV(const fs::path& path) {
    std::ofstream csvFile(path);
    if (!csvFile.is_open()) {
        LogPrintf("ExportSnapshotToCSV: Unable to open file: %s\n", fs::PathToString(path));
        return;
    }

    csvFile << "Address,Balance,Eligible\n";

    std::vector<std::tuple<std::string, CAmount, CAmount>> snapshotData;

    for (const auto& [address, entries] : foreignSnapshotByAddress) {
        CAmount balance = 0;
        CAmount eligible = 0;

        if (LookupPeercoinAddress(address, balance, eligible))
            snapshotData.emplace_back(address, balance, eligible);
        else
            LogPrintf("ExportSnapshotToCSV: Unable to lookup peercoin address: %s\n", address);
    }

    std::sort(snapshotData.begin(), snapshotData.end(), [](const auto& a, const auto& b) {
        return std::get<1>(a) > std::get<1>(b);
    });

    for (const auto& [address, balance, eligible] : snapshotData) {
        csvFile << address << "," << FormatCustomMoney(balance) << "," << FormatCustomMoney(eligible) << "\n";
    }

    csvFile.close();
    LogPrintf("ExportSnapshotToCSV: Snapshot exported to %s\n", fs::PathToString(path));
}
