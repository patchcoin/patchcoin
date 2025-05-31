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

RecursiveMutex m_snapshot_mutex;

uint256 SnapshotManager::GetHash() const
{
    const SnapshotManager tmp(*this);
    return SerializeHash(tmp);
}

void SnapshotManager::SetNull()
{
    LOCK(m_snapshot_mutex);
    m_valid_scripts.clear();
    m_incompatible_scripts.clear();
    m_hash_scripts = uint256();
}

void SnapshotManager::UpdateAllScriptPubKeys(const std::map<CScript, CAmount>& valid, const std::map<CScript, std::vector<std::pair<COutPoint, Coin>>>& incompatible)
{
    LOCK(m_snapshot_mutex);
    m_valid_scripts = valid;
    m_incompatible_scripts = incompatible;
    m_hash_scripts = GetHash();
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

    std::map<CScript, CAmount> valid_scripts;
    std::map<CScript, std::vector<std::pair<COutPoint, Coin>>> incompatible_scripts;

    CAmount compatible_amount_total = 0;
    CAmount incompatible_amount_total = 0;

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

            const PKHash* pkhash = std::get_if<PKHash>(&dest);
            if (pkhash) {
                valid_scripts[script] += coin.out.nValue;
                compatible_amount_total += coin.out.nValue;
                // assert(MoneyRange(compatible_amount_total));
            } else {
                incompatible_scripts[script].push_back(std::make_pair(outpoint, coin));
                incompatible_amount_total += coin.out.nValue;
                // assert(MoneyRange(incompatible_amount_total));
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

    LogPrintf("[snapshot] loaded %d coins from snapshot %s\n", coins_processed, base_blockhash.ToString());
    LogPrintf("[snapshot]   compatible peercoin addresses=%s amount=%s\n", valid_scripts.size(), FormatMoney(compatible_amount_total));
    LogPrintf("[snapshot]   incompatible peercoin addresses=%s amount=%s\n", incompatible_scripts.size(), FormatMoney(incompatible_amount_total));
    LogPrintf("[snapshot]   overall addresses=%s amount=%s\n",
              valid_scripts.size() + incompatible_scripts.size(),
              FormatMoney(compatible_amount_total + incompatible_amount_total));

    UpdateAllScriptPubKeys(valid_scripts, incompatible_scripts);
    LogPrintf("[snapshot] hash of peercoin scripts: want=%s, got=%s\n",
              Params().GetConsensus().hashPeercoinSnapshot.ToString(),
              m_hash_scripts.ToString());

    assert(m_hash_scripts == Params().GetConsensus().hashPeercoinSnapshot);

    return true;
}

bool SnapshotManager::LoadPeercoinUTXOSFromDisk()
{
    LOCK(m_snapshot_mutex);
    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), peercoinUTXOSPath);
    if (!fs::exists(path)) {
        LogPrintf("LoadPeercoinUTXOSFromDisk: file not found: %s\n", fs::PathToString(path));
        return false;
    }

    AutoFile coins_file(fsbridge::fopen(path, "rb"));
    node::SnapshotMetadata metadata;

    try {
        coins_file >> metadata; // read snapshot metadata
    } catch (const std::ios_base::failure&) {
        LogPrintf("LoadPeercoinUTXOSFromDisk: Unable to parse metadata: %s\n", fs::PathToString(path));
        return false;
    }

    if (!PopulateAndValidateSnapshotForeign(coins_file, metadata)) {
        LogPrintf("LoadPeercoinUTXOSFromDisk: Validation failed for snapshot: %s\n", fs::PathToString(path));
        return false;
    }
    coins_file.fclose();

    if (!StoreSnapshotToDisk())
        return false;

    LogPrintf("LoadPeercoinUTXOSFromDisk: Successfully loaded snapshot from %s\n", fs::PathToString(path));
    return true;
}

bool SnapshotManager::StoreSnapshotToDisk() const
{
    LOCK(m_snapshot_mutex);
    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), snapshotPath);
    FILE* file{fsbridge::fopen(path, "wb")};
    AutoFile afile{file};
    if (afile.IsNull()) {
        LogPrintf("StoreSnapshotToDisk: failed to open file %s\n", fs::PathToString(path));
        return false;
    }

    try {
        afile << *this;
        if (afile.fclose() != 0) {
            LogPrintf("StoreSnapshotToDisk: failed to close file %s\n", fs::PathToString(path));
            return false;
        }
        LogPrintf("StoreSnapshotToDisk: snapshot stored to %s\n", fs::PathToString(path));
        return true;

    } catch (const std::exception& e) {
        LogPrintf("StoreSnapshotToDisk: serialization error: %s\n", e.what());
        return false;
    }
}

bool SnapshotManager::LoadSnapshotFromDisk()
{
    LOCK(m_snapshot_mutex);
    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), snapshotPath);
    if (!fs::exists(path)) {
        LogPrintf("LoadSnapshotFromDisk: file not found: %s\n", fs::PathToString(path));
        return false;
    }

    FILE* fp = fsbridge::fopen(path, "rb");
    if (!fp) {
        LogPrintf("LoadSnapshotFromDisk: failed to open file %s\n", fs::PathToString(path));
        return false;
    }
    AutoFile afile{fp};

    try {
        SnapshotManager temp;
        afile >> temp;
        afile.fclose();

        const uint256 hash = temp.GetHash();
        if (hash != Params().GetConsensus().hashPeercoinSnapshot) {
            LogPrintf("LoadSnapshotFromDisk: hash mismatch. got=%s\n", hash.ToString());
            return false;
        }

        UpdateAllScriptPubKeys(temp.GetScriptPubKeys(), temp.GetIncompatibleScriptPubKeys());

        LogPrintf("LoadSnapshotFromDisk: snapshot loaded from %s\n", fs::PathToString(path));
        return true;

    } catch (const std::exception& e) {
        LogPrintf("LoadSnapshotFromDisk: deserialization error: %s\n", e.what());
        return false;
    }
}

bool SnapshotManager::LoadSnapshotOnStartup()
{
    return LoadSnapshotFromDisk() || LoadPeercoinUTXOSFromDisk();
}

bool SnapshotManager::CalculateEligible(const CAmount& balance, CAmount& eligible)
{
    if (!MoneyRange(balance)) return false;

    eligible = std::min(balance * 10, MAX_CLAIM_REWARD);
    if (!MoneyRange(eligible)) {
        eligible = 0;
        return false;
    }
    return true;
}


bool SnapshotManager::CalculateEligibleBTC(const CAmount& balance, CAmount& eligible)
{
    static constexpr CAmount COIN_BTC = 100000000;
    static constexpr CAmount MAX_BTC_SATOSHI = 21000000 * COIN_BTC;
    if (balance < 0 || balance > MAX_BTC_SATOSHI) {
        eligible = 0;
        return false;
    }

    constexpr CAmount kMaxMultBalance = MAX_CLAIM_REWARD / 50;
    if (balance > kMaxMultBalance) {
        eligible = MAX_CLAIM_REWARD;
    } else {
        eligible = balance * 50;
    }

    if (!MoneyRange(eligible)) {
        eligible = 0;
        return false;
    }

    return true;
}

bool SnapshotManager::CalculateReceived(const wallet::CWallet* pwallet, const CScript& target, CAmount& nTotalReceived)
{
    if (!pwallet) return false;

    LOCK(m_snapshot_mutex);
    nTotalReceived = 0;
    for (const auto& [_, wtx] : pwallet->mapWallet) {
        if (wtx.isAbandoned()) continue;
        for (const CTxOut& txout : wtx.tx->vout) {
            if (txout.scriptPubKey == target) {
                if (!MoneyRange(nTotalReceived + txout.nValue)) return false;
                nTotalReceived += txout.nValue;
            }
        }
    }
    return true;
}

bool SnapshotManager::LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible)
{
    LOCK(m_snapshot_mutex);

    const auto it_valid = m_valid_scripts.find(scriptPubKey);
    if (it_valid != m_valid_scripts.end()) {
        balance = it_valid->second;
        return CalculateEligible(balance, eligible);
    }

    /*
     *
    const auto it_incompat = m_incompatible_scripts.find(scriptPubKey);
    if (it_incompat != m_incompatible_scripts.end()) {
        balance = it_incompat->second;
        eligible = 0;
        return true;
    }

    */

    return false;
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

        for (const auto& [scriptPubKey, balance] : m_valid_scripts) {
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
