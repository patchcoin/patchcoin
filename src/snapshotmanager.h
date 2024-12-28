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
inline std::vector<CScript> scriptPubKeysOfPeercoinSnapshot;
inline uint256 hashScriptPubKeysOfPeercoinSnapshot;
uint256 HashScriptPubKeysOfPeercoinSnapshot(std::vector<CScript> scripts = scriptPubKeysOfPeercoinSnapshot);
bool LoadSnapshotOnStartup(const ArgsManager& args);
bool LookupPeercoinAddress(const std::string& address, CAmount& balance, CAmount& eligible);
void ExportSnapshotToCSV(const fs::path& path);
void DumpPermittedScriptPubKeys();
bool ReadPermittedScriptPubKeys();
bool CalculateBalanceAndEligible(const std::vector<fCoinEntry>& entries, CAmount& balance, CAmount& eligible);
bool CalculateBalanceAndEligible(const wallet::CWallet* pwallet, CScript target, const std::vector<fCoinEntry>& entries, CAmount& balance, CAmount& eligible, CAmount& nTotalReceived);

bool IsClaimValid(CScript source, std::vector<unsigned char> signature, CScript target);
#endif // PATCHCOIN_SNAPSHOTMANAGER_H
