#ifndef PATCHCOIN_SNAPSHOTMANAGER_H
#define PATCHCOIN_SNAPSHOTMANAGER_H

#include <coins.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <node/context.h>
#include <node/utxo_snapshot.h>

struct fCoinEntry {
    COutPoint outpoint;
    Coin coin;
    fCoinEntry(const COutPoint& outpointIn, const Coin& coinIn)
        : outpoint(outpointIn), coin(coinIn) {}
};

inline std::map<std::string, std::vector<fCoinEntry>> foreignSnapshotByAddress;
bool LoadSnapshotOnStartup(const ArgsManager& args);
bool LoadSnapshotFromFile(const fs::path& path);
bool LookupPeercoinAddress(const std::string& address, CAmount& balance, CAmount& eligible);
void ExportSnapshotToCSV(const fs::path& path);

#endif // PATCHCOIN_SNAPSHOTMANAGER_H
