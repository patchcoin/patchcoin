#include <snapshotmanager.h>
#include <util/system.h>
#include <logging.h>
#include <chainparams.h>
#include <key_io.h>
#include <serialize.h>
#include <shutdown.h>
#include <script/standard.h>
#include <fstream>
#include <index/claimindex.h>
#include <util/message.h>
#include <wallet/wallet.h>

namespace wallet {
class CWallet;
} // namespace wallet

std::set<std::string> invalid;

uint256 HashScriptPubKeysOfPeercoinSnapshot(std::map<CScript, CAmount> scripts) {
    CHashWriter ss(SER_GETHASH, 0);
    for (const auto& script : scripts) {
        ss << script;
    }
    return ss.GetHash();
}

std::map<CScript, CAmount> GetScriptPubKeysOfPeercoinSnapshot()
{
    std::map<CScript, CAmount> result;

    for (const auto& [_, entries] : foreignSnapshotByAddress) {
        if (entries.empty()) continue;
        CAmount balance = 0;
        CAmount eligible;
        CalculateBalanceAndEligible(entries, balance, eligible);
        result[entries[0].coin.out.scriptPubKey] = balance;
    }

    return result;
}

bool GetAddressFromScriptPubKey(const CScript& scriptPubKey, std::string& outAddress) {
    outAddress.clear();

    CTxDestination dest;
    if (ExtractDestination(scriptPubKey, dest)) {
        outAddress = EncodeDestination(dest);
    } else {
        return false;
    }

    if (outAddress.rfind("pc1qcanvas0000000000000000000000000000000000000", 0) == 0) {
        return false;
    }

    return true;
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
    scriptPubKeysOfPeercoinSnapshot.clear();

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
            foreignSnapshotByScriptPubKey[coin.out.scriptPubKey].emplace_back(std::move(outpoint), std::move(coin));
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

    CTxDestination dest;
    for (const auto& [scriptPubKey, entries] : foreignSnapshotByScriptPubKey) {
        if (ExtractDestination(scriptPubKey, dest)) {
            std::string addressStr = EncodeDestination(dest);
            if (addressStr.rfind("pc1qcanvas0000000000000000000000000000000000000", 0) == 0)
                continue;
            foreignSnapshotByAddress[addressStr] = entries;
        } else {
            foreignSnapshotByScriptPubKey.erase(scriptPubKey);
        }
    }

    LogPrintf("[snapshot] loaded %d coins from snapshot %s\n",
              coins_processed, base_blockhash.ToString());

    scriptPubKeysOfPeercoinSnapshot = GetScriptPubKeysOfPeercoinSnapshot();
    hashScriptPubKeysOfPeercoinSnapshot = HashScriptPubKeysOfPeercoinSnapshot();
    LogPrintf("[snapshot] hash of peercoin scripts: want=%s, got=%s\n", Params().GetConsensus().hashPeercoinSnapshot.ToString(), hashScriptPubKeysOfPeercoinSnapshot.ToString());
    assert(hashScriptPubKeysOfPeercoinSnapshot == Params().GetConsensus().hashPeercoinSnapshot);

    return true;
}

bool LoadSnapshotFromFile(const fs::path& path) {
    if (!fs::exists(path)) {
        LogPrintf("LoadSnapshotFromFile: file not found: %s\n", fs::PathToString(path));
        return false;
    }

    AutoFile coins_file(fsbridge::fopen(path, "rb"));

    try {
        coins_file >> peercoinSnapshotMetadata;
    } catch (const std::ios_base::failure& e) {
        LogPrintf("LoadSnapshotFromFile: Unable to parse metadata: %s\n", fs::PathToString(path));
        return false;
    }

    if (!PopulateAndValidateSnapshotForeign(coins_file, peercoinSnapshotMetadata)) {
        LogPrintf("LoadSnapshotFromFile: Validation failed for snapshot: %s\n", fs::PathToString(path));
        return false;
    }
    coins_file.fclose();
    DumpPermittedScriptPubKeys();

    LogPrintf("LoadSnapshotFromFile: Successfully loaded snapshot from %s\n", fs::PathToString(path));
    return true;
}

bool LoadSnapshotOnStartup(const ArgsManager& args) {
    const fs::path path = args.GetDataDirNet() / "peercoin_utxos.dat";
    if (fs::exists(path)) {
        LogPrintf("LoadSnapshotOnStartup: found default snapshot at %s\n", fs::PathToString(path));
    }

    if (path.empty()) {
        LogPrintf("LoadSnapshotOnStartup: no snapshot file provided and none found\n");
        return true;
    }

    return LoadSnapshotFromFile(path) || ReadPermittedScriptPubKeys();
}

bool CalculateBalanceAndEligible(const CAmount& balance, CAmount& eligible) {
    if (!MoneyRange(balance))
        return false;

    eligible = std::min(balance * 10, 50000 * COIN);
    if (!MoneyRange(eligible)) {
        eligible = 0;
        return false;
    }

    return true;
}

bool CalculateBalanceAndEligible(const std::vector<fCoinEntry>& entries, CAmount& balance, CAmount& eligible) {
    balance = std::accumulate(entries.begin(), entries.end(), CAmount{0},
        [](const CAmount& amount, const fCoinEntry& entry) {
            return amount + entry.coin.out.nValue;
        });
    if (!MoneyRange(balance))
        return false;

    CalculateBalanceAndEligible(balance, eligible);

    return true;
}

bool CalculateBalanceAndEligible(const CWallet* pwallet, CScript target, const std::vector<fCoinEntry>& entries, CAmount& balance, CAmount& eligible, CAmount& nTotalReceived) {
    if (!pwallet) return false;

    for (const auto& [_, wtx] : pwallet->mapWallet) {
        for (const CTxOut& txout : wtx.tx->vout) {
            if (txout.scriptPubKey == target) {
                // patchcoin todo add to stats
                if (!MoneyRange(nTotalReceived += txout.nValue))
                    return false;
            }
        }
    }

    return CalculateBalanceAndEligible(entries, balance, eligible);
}

bool LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible)
{
    const auto it = scriptPubKeysOfPeercoinSnapshot.find(scriptPubKey);
    if (it == scriptPubKeysOfPeercoinSnapshot.end())
        return false;

    balance = it->second;
    return CalculateBalanceAndEligible(balance, eligible);
}

bool LookupPeercoinAddress(const std::string& address, CAmount& balance, CAmount& eligible) {
    const auto it = foreignSnapshotByAddress.find(address);
    if (it == foreignSnapshotByAddress.end())
        return false;

    return CalculateBalanceAndEligible(it->second, balance, eligible);
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

    // patchcoin todo add more stats?
    // output potentially faulty addresses
    // https://chainz.cryptoid.info/ppc/address.dws?PXbK5MbYmcj778AgJYUobigoUfDGnVFLGz.htm
    // https://chainz.cryptoid.info/ppc/address.dws?PPCoinsHDXLFmAiwjs4NstpZ43pqixEzQj.htm
    for (const auto& [address, entries] : foreignSnapshotByAddress) {
        CAmount balance = 0;
        CAmount eligible = 0;

        if (CalculateBalanceAndEligible(entries, balance, eligible)) {
            snapshotData.emplace_back(address, balance, eligible);
        } else {
            LogPrintf("ExportSnapshotToCSV: Invalid data for address: %s\n", address);
        }
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

void DumpPermittedScriptPubKeys()
{
    std::map<CScript, CAmount> scriptPubKeys = scriptPubKeysOfPeercoinSnapshot.empty() ? GetScriptPubKeysOfPeercoinSnapshot() : scriptPubKeysOfPeercoinSnapshot;
    uint256 hash{HashScriptPubKeysOfPeercoinSnapshot(scriptPubKeys)};
    assert(hash == Params().GetConsensus().hashPeercoinSnapshot);
    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), fs::u8path("peercoin_snapshot.dat"));
    if (fs::exists(path)) {
        return;
    }
    FILE* file{fsbridge::fopen(path, "wb")};
    AutoFile afile{file};
    if (afile.IsNull()) {
        return;
    }
    WriteCompactSize(afile, scriptPubKeys.size());
    for (const auto& script : scriptPubKeys) {
        afile << script;
    }
    // patchcoin todo check for errors, possibly written size
}

bool ReadPermittedScriptPubKeys()
{
    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), fs::u8path("peercoin_snapshot.dat"));
    if (!fs::exists(path)) {
        return false;
    }
    FILE* file{fsbridge::fopen(path, "rb")};
    AutoFile afile{file};
    if (afile.IsNull()) {
        return false;
    }
    scriptPubKeysOfPeercoinSnapshot.clear();
    afile >> scriptPubKeysOfPeercoinSnapshot;

    // patchcoin todo, we could re-fetch the snapshot
    if (std::fgetc(afile.Get()) != EOF) {
        LogPrintf("[snapshot] warning: unexpected trailing data\n");
    } else if (std::ferror(afile.Get())) {
        LogPrintf("[snapshot] warning: i/o error\n");
    }
    afile.fclose();
    hashScriptPubKeysOfPeercoinSnapshot = HashScriptPubKeysOfPeercoinSnapshot();
    LogPrintf("[snapshot] hash of peercoin scripts:  want=%s, got=%s\n", Params().GetConsensus().hashPeercoinSnapshot.ToString(), hashScriptPubKeysOfPeercoinSnapshot.ToString());
    assert(hashScriptPubKeysOfPeercoinSnapshot == Params().GetConsensus().hashPeercoinSnapshot);
    return true;
}

bool IsClaimValid(const CScript& source, const std::vector<unsigned char>& signature, const CScript& target)
{
    if (hashScriptPubKeysOfPeercoinSnapshot != Params().GetConsensus().hashPeercoinSnapshot) {
        LogPrintf("[claim] warning: snapshot hash does not match consensus hash\n");
        return false;
    }

    try {
        CTxDestination sourceDest;
        CTxDestination targetDest;

        if (!ExtractDestination(source, sourceDest)) {
            LogPrintf("[claim] error: failed to extract destination from source script\n");
            return false;
        }

        if (!ExtractDestination(target, targetDest)) {
            LogPrintf("[claim] error: failed to extract destination from target script\n");
            return false;
        }

        std::string sourceAddress = EncodeDestination(sourceDest);
        std::string targetAddress = EncodeDestination(targetDest);

        CScript sourceScript = GetScriptForDestination(sourceDest);

        auto it = scriptPubKeysOfPeercoinSnapshot.find(sourceScript);

        if (it != scriptPubKeysOfPeercoinSnapshot.end()) {
            auto res = MessageVerify(sourceAddress, EncodeBase64(signature), targetAddress, PEERCOIN_MESSAGE_MAGIC);

            if (res == MessageVerificationResult::OK) {
                return true;
            }
            return false;
        }

        LogPrintf("[claim] warning: source script not found in snapshot for address %s\n", sourceAddress);
    }
    catch (const std::exception& e) {
        LogPrintf("[claim] exception: %s\n", e.what());
        return false;
    }
    catch (...) {
        LogPrintf("[claim] unknown exception encountered during claim validation\n");
        return false;
    }

    return false;
}
