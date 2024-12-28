#include <primitives/claim.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>

uint256 CClaim::GetHash() const
{
    return SerializeHash(*this);
}

std::string CClaim::ToString() const
{
    return strprintf("CClaim(hash=%s, sourceScriptPubKey=%s, signature=%s, targetScriptPubKey=%s, nTime=%lld)",
        GetHash().ToString(),
        HexStr(sourceScriptPubKey),
        HexStr(signature),
        HexStr(targetScriptPubKey),
        nTime);
}

CClaim CreateNewClaim(const CScript& sourceScriptPubKey, const std::vector<unsigned char>& signature, const CScript& targetScriptPubKey)
{
    // patchcoin todo drop this function
    if (sourceScriptPubKey.empty() || targetScriptPubKey.empty()) {
        throw std::invalid_argument("Source or target scriptPubKey cannot be empty");
    }

    if (signature.empty()) {
        throw std::invalid_argument("Signature cannot be empty");
    }

    auto claim = std::make_shared<CClaim>();
    claim->sourceScriptPubKey = sourceScriptPubKey;
    claim->signature = signature;
    claim->targetScriptPubKey = targetScriptPubKey;
    claim->nTime = GetTimeMillis();
    claim->nTotalReceived = 0;

    if (!claim->IsValid()) {
        throw std::runtime_error("Invalid claim: failed validation");
    }

    return *claim;
}
