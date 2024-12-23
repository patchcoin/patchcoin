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
bool LoadSnapshotFromFile(const std::string& filePath);
bool PopulateAndValidateSnapshotForeign(AutoFile& coins_file, const node::SnapshotMetadata& metadata);
CAmount LookupPeercoinAddress(const std::string& address);
bool GetAddressFromScriptPubKey(const CScript& scriptPubKey, std::string& outAddress);

#endif // PATCHCOIN_SNAPSHOTMANAGER_H
