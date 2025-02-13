#include <claim.h>

#include <hash.h>

Mutex g_claims_mutex;
std::map<const CScript, Claim> g_claims GUARDED_BY(g_claims_mutex);

// const Claim::sman = SnapshotManager::Peercoin();
const std::map<CScript, CAmount>& Claim::snapshot = SnapshotManager::Peercoin().GetScriptPubKeys();
const uint256& Claim::hashSnapshot = SnapshotManager::Peercoin().GetHashScripts();

uint256 Claim::GetHash() const
{
    return SerializeHash(*this);
}

unsigned int Claim::GetBaseSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

/*
std::string Claim::ToString() const
{
    return strprintf("Claim(hash=%s, source=%s, signature=%s, target=%s, nTime=%lld)",
        GetHash().ToString(),
        HexStr(GetSource()),
        HexStr(signature),
        HexStr(target),
        nTime);
}
*/
