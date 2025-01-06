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

bool PopulateAndValidateSnapshotForeign(
    AutoFile& coins_file,
    const node::SnapshotMetadata& metadata) {
    uint256 base_blockhash = metadata.m_base_blockhash;
    const uint64_t coins_count = metadata.m_coins_count;
    uint64_t coins_left = coins_count;

    LogPrintf("[snapshot] loading coins from snapshot %s\n", base_blockhash.ToString());

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
            CTxDestination dest;
            std::string outAddress;
            if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                outAddress = EncodeDestination(dest);
            } else {
                return false;
            }
            if (outAddress.rfind("pc1qcanvas0000000000000000000000000000000000000", 0) == 0) {
                --coins_left;
                ++coins_processed;
                continue;
            }
            CTxDestination normalized = DecodeDestination(outAddress);
            if (!IsValidDestination(normalized)) {
                return false;
            }
            CScript script = GetScriptForDestination(normalized);
            scriptPubKeysOfPeercoinSnapshot[script] += coin.out.nValue;

            assert(MoneyRange(scriptPubKeysOfPeercoinSnapshot[script]));
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

    return ReadPermittedScriptPubKeys() || LoadSnapshotFromFile(path);
}

bool CalculateEligible(const CAmount& balance, CAmount& eligible) {
    if (!MoneyRange(balance))
        return false;

    eligible = std::min(balance * 10, 50000 * COIN);
    if (!MoneyRange(eligible)) {
        eligible = 0;
        return false;
    }

    return true;
}

bool CalculateEligible(const CWallet* pwallet, CScript& target, CAmount& balance, CAmount& eligible, CAmount& nTotalReceived) {
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

    return CalculateEligible(balance, eligible);
}

bool LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible)
{
    const auto it = scriptPubKeysOfPeercoinSnapshot.find(scriptPubKey);
    if (it == scriptPubKeysOfPeercoinSnapshot.end())
        return false;

    balance = it->second;
    return CalculateEligible(balance, eligible);
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
    for (const auto& [scriptPubKey, balance] : scriptPubKeysOfPeercoinSnapshot) {
        CTxDestination dest;
        ExtractDestination(scriptPubKey, dest);
        std::string address = EncodeDestination(dest);
        CAmount eligible = 0;

        if (CalculateEligible(balance, eligible)) {
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
    uint256 hash{HashScriptPubKeysOfPeercoinSnapshot(scriptPubKeysOfPeercoinSnapshot)};
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
    WriteCompactSize(afile, scriptPubKeysOfPeercoinSnapshot.size());
    for (const auto& script : scriptPubKeysOfPeercoinSnapshot) {
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
    LogPrintf("[snapshot] hash of peercoin scripts: want=%s, got=%s\n", Params().GetConsensus().hashPeercoinSnapshot.ToString(), hashScriptPubKeysOfPeercoinSnapshot.ToString());
    assert(hashScriptPubKeysOfPeercoinSnapshot == Params().GetConsensus().hashPeercoinSnapshot);
    return true;
}
