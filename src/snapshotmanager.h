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
bool LoadSnapshotOnStartup(const ArgsManager& args);
bool LoadSnapshotFromFile(const fs::path& path);
bool PopulateAndValidateSnapshotForeign(AutoFile& coins_file, const node::SnapshotMetadata& metadata);
bool LookupPeercoinAddress(const std::string& address, CAmount& balance, CAmount& eligible);
bool GetAddressFromScriptPubKey(const CScript& scriptPubKey, std::string& outAddress);
void ExportSnapshotToCSV(const fs::path& path);

#endif // PATCHCOIN_SNAPSHOTMANAGER_H
