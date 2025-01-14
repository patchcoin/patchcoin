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

#include <algorithm>
#include <cassert>
#include <sstream>
#include <iomanip>

RecursiveMutex m_snapshot_mutex;

SnapshotManager& SnapshotManager::Peercoin()
{
    static SnapshotManager peercoin;
    return peercoin;
}

/*
static std::unique_ptr<const SnapshotManager> globalPeercoinSnapshotManager = SnapshotManager::Peercoin();

const SnapshotManager& PeercoinSnapshot()
{
    assert(globalPeercoinSnapshotManager);
    return *globalPeercoinSnapshotManager;
}
*/

uint256 SnapshotManager::GetHash(std::map<CScript, CAmount>& scripts)
{
    HashWriter ss{};
    size_t position = 0;
    for (const auto& [script, amount] : scripts) {
        ss << position++;
        ss << script;
        ss << amount;
    }
    return ss.GetHash();
}

bool SnapshotManager::PopulateAndValidateSnapshotForeign(
    AutoFile& coins_file,
    const node::SnapshotMetadata& metadata)
{
    LOCK(m_snapshot_mutex);

    uint256 base_blockhash = metadata.m_base_blockhash;
    const uint64_t coins_count = metadata.m_coins_count;
    uint64_t coins_left = coins_count;

    LogPrintf("[snapshot] loading coins from snapshot %s\n", base_blockhash.ToString());

    std::map<CScript, CAmount> scripts;

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
            if (!ExtractDestination(coin.out.scriptPubKey, dest)) {
                return false;
            }
            outAddress = EncodeDestination(dest);

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
            scripts[script] += coin.out.nValue;
            assert(MoneyRange(scripts[script]));
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

    uint256 hashScripts = GetHash(scripts);
    LogPrintf("[snapshot] hash of peercoin scripts: want=%s, got=%s\n",
              Params().GetConsensus().hashPeercoinSnapshot.ToString(), hashScripts.ToString());
    assert(hashScripts == Params().GetConsensus().hashPeercoinSnapshot);
    UpdateScriptPubKeys(scripts);

    return true;
}

bool SnapshotManager::LoadSnapshotFromFile(const fs::path& path)
{
    if (!fs::exists(path)) {
        LogPrintf("LoadSnapshotFromFile: file not found: %s\n", fs::PathToString(path));
        return false;
    }

    AutoFile coins_file(fsbridge::fopen(path, "rb"));
    node::SnapshotMetadata metadata;

    try {
        coins_file >> metadata; // read snapshot metadata
    } catch (const std::ios_base::failure&) {
        LogPrintf("LoadSnapshotFromFile: Unable to parse metadata: %s\n", fs::PathToString(path));
        return false;
    }

    {
        LOCK(m_snapshot_mutex);
        m_metadata = metadata;
    }

    if (!PopulateAndValidateSnapshotForeign(coins_file, metadata)) {
        LogPrintf("LoadSnapshotFromFile: Validation failed for snapshot: %s\n", fs::PathToString(path));
        return false;
    }
    coins_file.fclose();

    DumpPermittedScriptPubKeys();

    LogPrintf("LoadSnapshotFromFile: Successfully loaded snapshot from %s\n", fs::PathToString(path));
    return true;
}

bool SnapshotManager::ReadPermittedScriptPubKeys()
{
    LOCK(m_snapshot_mutex);

    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), fs::u8path("peercoin_snapshot.dat"));
    if (!fs::exists(path)) {
        return false;
    }
    FILE* file{fsbridge::fopen(path, "rb")};
    AutoFile afile{file};
    if (afile.IsNull()) {
        return false;
    }

    m_scripts.clear();
    afile >> m_scripts;

    // Check for trailing data / I/O error
    if (std::fgetc(afile.Get()) != EOF) {
        LogPrintf("[snapshot] warning: unexpected trailing data\n");
    } else if (std::ferror(afile.Get())) {
        LogPrintf("[snapshot] warning: i/o error\n");
    }
    afile.fclose();

    m_hash_scripts = GetHash(m_scripts);
    LogPrintf("[snapshot] hash of peercoin scripts: want=%s, got=%s\n",
              Params().GetConsensus().hashPeercoinSnapshot.ToString(),
              m_hash_scripts.ToString());
    assert(m_hash_scripts == Params().GetConsensus().hashPeercoinSnapshot);

    return true;
}

void SnapshotManager::DumpPermittedScriptPubKeys()
{
    LOCK(m_snapshot_mutex);

    uint256 hash{GetHash(m_scripts)};
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

    WriteCompactSize(afile, m_scripts.size());
    for (const auto& script : m_scripts) {
        afile << script;
    }
}

bool SnapshotManager::LoadSnapshotOnStartup(const ArgsManager& args)
{
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

bool SnapshotManager::CalculateEligible(const CAmount& balance, CAmount& eligible)
{
    if (!MoneyRange(balance)) return false;

    eligible = std::min(balance * 10, 50000 * COIN);
    if (!MoneyRange(eligible)) {
        eligible = 0;
        return false;
    }
    return true;
}

bool SnapshotManager::CalculateReceived(const wallet::CWallet* pwallet, const CScript& target, CAmount& nTotalReceived)
{
    if (!pwallet) return false;

    nTotalReceived = 0;
    {
        LOCK(m_snapshot_mutex);
        for (const auto& [_, wtx] : pwallet->mapWallet) {
            for (const CTxOut& txout : wtx.tx->vout) {
                if (txout.scriptPubKey == target) {
                    if (!MoneyRange(nTotalReceived + txout.nValue)) return false;
                    nTotalReceived += txout.nValue;
                }
            }
        }
    }
    return true;
}

bool SnapshotManager::LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible)
{
    LOCK(m_snapshot_mutex);

    const auto it = m_scripts.find(scriptPubKey);
    if (it == m_scripts.end()) {
        return false;
    }
    balance = it->second;
    return CalculateEligible(balance, eligible);
}

std::string SnapshotManager::FormatCustomMoney(CAmount amount)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << static_cast<double>(amount) / COIN;
    return oss.str();
}

void SnapshotManager::ExportSnapshotToCSV(const fs::path& path)
{
    std::ofstream csvFile(path);
    if (!csvFile.is_open()) {
        LogPrintf("ExportSnapshotToCSV: Unable to open file: %s\n", fs::PathToString(path));
        return;
    }

    csvFile << "Address,Balance,Eligible\n";

    std::vector<std::tuple<std::string, CAmount, CAmount>> snapshotData;
    {
        LOCK(m_snapshot_mutex);

        for (const auto& [scriptPubKey, balance] : m_scripts) {
            CTxDestination dest;
            if (!ExtractDestination(scriptPubKey, dest)) {
                continue;
            }
            std::string address = EncodeDestination(dest);
            CAmount eligible = 0;
            if (CalculateEligible(balance, eligible)) {
                snapshotData.emplace_back(address, balance, eligible);
            } else {
                LogPrintf("ExportSnapshotToCSV: Invalid data for address: %s\n", address);
            }
        }
    }

    std::sort(snapshotData.begin(), snapshotData.end(),
              [](const auto& a, const auto& b) {
                  return std::get<1>(a) > std::get<1>(b);
              });

    for (const auto& [address, balance, eligible] : snapshotData) {
        csvFile << address << ","
                << FormatCustomMoney(balance) << ","
                << FormatCustomMoney(eligible) << "\n";
    }

    csvFile.close();
    LogPrintf("ExportSnapshotToCSV: Snapshot exported to %s\n", fs::PathToString(path));
}

bool SnapshotManager::StorePermanentSnapshot(const fs::path& path)
{
    LOCK(m_snapshot_mutex);

    FILE* file{fsbridge::fopen(path, "wb")};
    if (!file) {
        LogPrintf("StorePermanentSnapshot: failed to open file: %s\n", fs::PathToString(path));
        return false;
    }
    AutoFile afile{file};

    afile << m_metadata;
    afile << m_scripts;
    afile << m_hash_scripts;

    afile.fclose();
    LogPrintf("StorePermanentSnapshot: stored snapshot to %s\n", fs::PathToString(path));
    return true;
}

bool SnapshotManager::LoadPermanentSnapshot(const fs::path& path)
{
    if (!fs::exists(path)) {
        LogPrintf("LoadPermanentSnapshot: file not found: %s\n", fs::PathToString(path));
        return false;
    }

    FILE* file{fsbridge::fopen(path, "rb")};
    if (!file) {
        LogPrintf("LoadPermanentSnapshot: failed to open file: %s\n", fs::PathToString(path));
        return false;
    }
    AutoFile afile{file};

    node::SnapshotMetadata metadata;
    std::map<CScript, CAmount> tempMap;
    uint256 tempHash;

    try {
        afile >> metadata;
        afile >> tempMap;
        afile >> tempHash;
    } catch (const std::ios_base::failure&) {
        LogPrintf("LoadPermanentSnapshot: parse error for file: %s\n", fs::PathToString(path));
        return false;
    }
    afile.fclose();

    uint256 testHash = GetHash(tempMap);
    if (testHash != tempHash) {
        LogPrintf("LoadPermanentSnapshot: snapshot hash mismatch. expected=%s, got=%s\n",
                  tempHash.ToString(), testHash.ToString());
        return false;
    }

    {
        LOCK(m_snapshot_mutex);
        m_metadata = metadata;
        m_scripts = std::move(tempMap);
        m_hash_scripts = std::move(tempHash);
    }
    LogPrintf("LoadPermanentSnapshot: loaded snapshot from %s\n", fs::PathToString(path));
    return true;
}
