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
    std::map<CScript, std::vector<std::pair<COutPoint, Coin>>> m_incompatible_scripts;

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

    void SetNull();

    uint256 GetHash() const;

    void UpdateAllScriptPubKeys(std::map<CScript, CAmount>& valid, std::map<CScript, std::vector<std::pair<COutPoint, Coin>>>& incompatible);

    std::map<CScript, CAmount>& GetScriptPubKeys() {
        return m_valid_scripts;
    }
    std::map<CScript, std::vector<std::pair<COutPoint, Coin>>>& GetIncompatibleScriptPubKeys() {
        return m_incompatible_scripts;
    }

    const uint256& GetHashScripts() const
    {
        return m_hash_scripts;
    }

    // template<typename Stream>
    // void Serialize(Stream& s) const {
    //     ::Serialize(s, VARINT(m_valid_scripts.size()));
    //     for (const auto& [script, amount] : m_valid_scripts) {
    //         ::Serialize(s, Using<ScriptCompression>(script));
    //         ::Serialize(s, Using<AmountCompression>(amount));
    //     }
    //     ::Serialize(s, VARINT(m_incompatible_scripts.size()));
    //     for (const auto& [script, coins] : m_incompatible_scripts) {
    //         ::Serialize(s, Using<ScriptCompression>(script));
    //         ::Serialize(s, VARINT(coins.size()));
    //         for (const auto& [outpoint, coin] : coins) {
    //             ::Serialize(s, outpoint);
    //             coin.Serialize(s);
    //         }
    //     }
    // }
    //
    // template<typename Stream>
    // void Unserialize(Stream& s) {
    //     m_valid_scripts.clear();
    //     m_incompatible_scripts.clear();
    //     size_t valid_size = 0;
    //     ::Unserialize(s, VARINT(valid_size));
    //     for (size_t i = 0; i < valid_size; i++) {
    //         CScript script;
    //         CAmount amount;
    //         ::Unserialize(s, Using<ScriptCompression>(script));
    //         ::Unserialize(s, Using<AmountCompression>(amount));
    //         m_valid_scripts.emplace(std::move(script), amount);
    //     }
    //     size_t incompatible_size = 0;
    //     ::Unserialize(s, VARINT(incompatible_size));
    //     for (size_t i = 0; i < incompatible_size; i++) {
    //         CScript script;
    //         ::Unserialize(s, Using<ScriptCompression>(script));
    //         size_t coins_size = 0;
    //         ::Unserialize(s, VARINT(coins_size));
    //         std::vector<std::pair<COutPoint, Coin>> coins;
    //         coins.reserve(coins_size);
    //         for (size_t j = 0; j < coins_size; j++) {
    //             COutPoint outpoint;
    //             Coin coin;
    //             ::Unserialize(s, outpoint);
    //             coin.Unserialize(s);
    //             coins.emplace_back(std::move(outpoint), std::move(coin));
    //         }
    //         m_incompatible_scripts.emplace(std::move(script), std::move(coins));
    //     }
    // }

    SERIALIZE_METHODS(SnapshotManager, obj)
    {
        READWRITE(obj.m_valid_scripts, obj.m_incompatible_scripts);
    }
};

#endif // PATCHCOIN_SNAPSHOTMANAGER_H
