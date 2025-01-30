#include <primitives/claim.h>

#include <hash.h>

Mutex g_claims_mutex;
std::map<const CScript, CClaim> g_claims GUARDED_BY(g_claims_mutex);

// const CClaim::sman = SnapshotManager::Peercoin();
const std::map<CScript, CAmount>& CClaim::snapshot = SnapshotManager::Peercoin().GetScriptPubKeys();
const uint256& CClaim::hashSnapshot = SnapshotManager::Peercoin().GetHashScripts();

uint256 CClaim::GetHash() const
{
    return SerializeHash(*this);
}

unsigned int CClaim::GetBaseSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

/*
std::string CClaim::ToString() const
{
    return strprintf("CClaim(hash=%s, source=%s, signature=%s, target=%s, nTime=%lld)",
        GetHash().ToString(),
        HexStr(GetSource()),
        HexStr(signature),
        HexStr(target),
        nTime);
}
*/
