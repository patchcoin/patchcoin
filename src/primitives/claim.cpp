#include <primitives/claim.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>

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
