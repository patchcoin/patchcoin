#ifndef PATCHCOIN_SNAPSHOTMANAGER_H
#define PATCHCOIN_SNAPSHOTMANAGER_H

#include <coins.h>
#include <primitives/transaction.h>
#include <node/context.h>
#include <node/utxo_snapshot.h>
#include <streams.h>
#include <map>
#include <set>
#include <string>

namespace wallet {
    class CWallet;
} // namespace wallet

extern RecursiveMutex m_snapshot_mutex; // patchcoin todo
inline CAmount MAX_CLAIM_REWARD = 50000 * COIN;

class SnapshotManager
{
    SnapshotManager() = default;
    ~SnapshotManager() = default;

    bool PopulateAndValidateSnapshotForeign(AutoFile& coins_file, const node::SnapshotMetadata& metadata);

    bool LoadSnapshotFromFile(const fs::path& path);

    bool ReadPermittedScriptPubKeys();


    std::string FormatCustomMoney(CAmount amount);

    std::map<CScript, CAmount> m_scripts;
    node::SnapshotMetadata     m_metadata;
    uint256                    m_hash_scripts;

    std::set<std::string> m_invalid;

    // RecursiveMutex m_snapshot_mutex;
public:
    static SnapshotManager& Peercoin();

    SnapshotManager(const SnapshotManager&) = delete;
    SnapshotManager& operator=(const SnapshotManager&) = delete;
    SnapshotManager(SnapshotManager&&) = delete;
    SnapshotManager& operator=(SnapshotManager&&) = delete;

    bool LoadSnapshotOnStartup(const ArgsManager& args);

    bool LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible);

    void ExportSnapshotToCSV(const fs::path& path);

    bool CalculateEligible(const CAmount& balance, CAmount& eligible);

    bool CalculateReceived(const wallet::CWallet* pwallet, const CScript& target, CAmount& nTotalReceived);

    void DumpPermittedScriptPubKeys();

    bool StorePermanentSnapshot(const fs::path& path);

    bool LoadPermanentSnapshot(const fs::path& path);

    uint256 GetHash(std::map<CScript, CAmount>& scripts);

    void UpdateScriptPubKeys(const std::map<CScript, CAmount>& scripts)
    {
        LOCK(m_snapshot_mutex);
        m_scripts = scripts;
        m_hash_scripts = GetHash(m_scripts);
    }

    std::map<CScript, CAmount>& GetScriptPubKeys() {
        return m_scripts;
    }

    const uint256& Hash() const {
        return m_hash_scripts;
    }
};

#endif // PATCHCOIN_SNAPSHOTMANAGER_H
