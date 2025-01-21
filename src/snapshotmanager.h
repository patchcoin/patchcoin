#ifndef PATCHCOIN_SNAPSHOTMANAGER_H
#define PATCHCOIN_SNAPSHOTMANAGER_H

#include <coins.h>
#include <primitives/transaction.h>
#include <node/context.h>
#include <node/utxo_snapshot.h>
#include <streams.h>
#include <sync.h>
#include <map>
#include <string>
#include <util/system.h>

namespace wallet {
    class CWallet;
} // namespace wallet

extern RecursiveMutex m_snapshot_mutex; // patchcoin todo
static constexpr inline CAmount MAX_CLAIM_REWARD = 50000 * COIN;

class SnapshotManager
{
    fs::path peercoinUTXOSPath = fs::u8path("peercoin_utxos.dat");
    fs::path snapshotPath = fs::u8path("peercoin_snapshot.dat");

    bool PopulateAndValidateSnapshotForeign(AutoFile& coins_file, const node::SnapshotMetadata& metadata);

    std::string FormatCustomMoney(CAmount amount);

    std::map<CScript, CAmount> m_valid_scripts;
    std::map<CScript, CAmount> m_incompatible_scripts;

    uint256 m_hash_scripts;

public:
    SnapshotManager() = default;
    ~SnapshotManager() = default;

    static SnapshotManager& Peercoin() {
        static SnapshotManager peercoinInstance;
        return peercoinInstance;
    }

    SnapshotManager(const SnapshotManager&) = default;
    SnapshotManager& operator=(const SnapshotManager&) = default;
    SnapshotManager(SnapshotManager&&) = default;
    SnapshotManager& operator=(SnapshotManager&&) = default;

    bool LoadSnapshotOnStartup();
    bool LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible);
    void ExportSnapshotToCSV(const fs::path& path);
    bool CalculateEligible(const CAmount& balance, CAmount& eligible);
    bool CalculateReceived(const wallet::CWallet* pwallet, const CScript& target, CAmount& nTotalReceived);

    bool LoadPeercoinUTXOSFromDisk();

    bool StoreSnapshotToDisk() const;

    bool LoadSnapshotFromDisk();

    uint256 GetHash() const;

    void UpdateAllScriptPubKeys(std::map<CScript, CAmount>& valid, std::map<CScript, CAmount>& incompatible);

    std::map<CScript, CAmount>& GetScriptPubKeys() {
        return m_valid_scripts;
    }
    std::map<CScript, CAmount>& GetIncompatibleScriptPubKeys() {
        return m_incompatible_scripts;
    }

    const uint256& GetHashScripts() const
    {
        return m_hash_scripts;
    }

    SERIALIZE_METHODS(SnapshotManager, obj)
    {
        READWRITE(obj.m_valid_scripts, obj.m_incompatible_scripts);
    }
};

#endif // PATCHCOIN_SNAPSHOTMANAGER_H
