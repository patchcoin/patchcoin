#ifndef PATCHCOIN_SNAPSHOTMANAGER_H
#define PATCHCOIN_SNAPSHOTMANAGER_H

#include <coins.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <node/context.h>
#include <node/utxo_snapshot.h>

namespace wallet
{
    class CWallet;
}

struct fCoinEntry {
    COutPoint outpoint;
    Coin coin;
    fCoinEntry(const COutPoint& outpointIn, const Coin& coinIn)
        : outpoint(outpointIn), coin(coinIn) {}
};

inline std::map<std::string, std::vector<fCoinEntry>> foreignSnapshotByAddress;
inline std::map<CScript, std::vector<fCoinEntry>> foreignSnapshotByScriptPubKey;
inline std::map<CScript, CAmount> scriptPubKeysOfPeercoinSnapshot;
inline node::SnapshotMetadata peercoinSnapshotMetadata;
inline uint256 hashScriptPubKeysOfPeercoinSnapshot;
uint256 HashScriptPubKeysOfPeercoinSnapshot(std::map<CScript, CAmount> scripts = scriptPubKeysOfPeercoinSnapshot);
bool LoadSnapshotOnStartup(const ArgsManager& args);
bool LookupPeercoinScriptPubKey(const CScript& scriptPubKey, CAmount& balance, CAmount& eligible);
bool LookupPeercoinAddress(const std::string& address, CAmount& balance, CAmount& eligible);
void ExportSnapshotToCSV(const fs::path& path);
void DumpPermittedScriptPubKeys();
bool ReadPermittedScriptPubKeys();
bool CalculateBalanceAndEligible(const CAmount& balance, CAmount& eligible);
bool CalculateBalanceAndEligible(const std::vector<fCoinEntry>& entries, CAmount& balance, CAmount& eligible);
bool CalculateBalanceAndEligible(const wallet::CWallet* pwallet, CScript target, const std::vector<fCoinEntry>& entries, CAmount& balance, CAmount& eligible, CAmount& nTotalReceived);

bool IsClaimValid(const CScript& source, const std::vector<unsigned char>& signature, const CScript& target);
#endif // PATCHCOIN_SNAPSHOTMANAGER_H
