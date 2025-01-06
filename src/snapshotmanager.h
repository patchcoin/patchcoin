#ifndef PATCHCOIN_SNAPSHOTMANAGER_H
#define PATCHCOIN_SNAPSHOTMANAGER_H

#include <coins.h>
#include <primitives/transaction.h>
#include <node/context.h>
#include <node/utxo_snapshot.h>

namespace wallet
{
    class CWallet;
}

inline std::map<CScript, CAmount> scriptPubKeysOfPeercoinSnapshot;
inline node::SnapshotMetadata peercoinSnapshotMetadata;
inline uint256 hashScriptPubKeysOfPeercoinSnapshot;
uint256 HashScriptPubKeysOfPeercoinSnapshot(std::map<CScript, CAmount> scripts = scriptPubKeysOfPeercoinSnapshot);
bool LoadSnapshotOnStartup(const ArgsManager& args);
bool LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible);
void ExportSnapshotToCSV(const fs::path& path);
void DumpPermittedScriptPubKeys();
bool ReadPermittedScriptPubKeys();
bool CalculateEligible(const CAmount& balance, CAmount& eligible);
bool CalculateEligible(const wallet::CWallet* pwallet, CScript target, CAmount& balance, CAmount& eligible, CAmount& nTotalReceived);

#endif // PATCHCOIN_SNAPSHOTMANAGER_H
