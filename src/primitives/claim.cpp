#include <primitives/claim.h>

#include <hash.h>

std::map<const CScript, const CClaim> g_claims;

// const CClaim::sman = SnapshotManager::Peercoin();
const std::map<CScript, CAmount>& CClaim::snapshot = SnapshotManager::Peercoin().GetScriptPubKeys();
const uint256& CClaim::hashSnapshot = SnapshotManager::Peercoin().Hash();

uint256 CClaim::GetHash() const
{
    return SerializeHash(*this);
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
