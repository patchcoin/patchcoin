#ifndef PATCHCOIN_PRIMITIVES_CLAIM_H
#define PATCHCOIN_PRIMITIVES_CLAIM_H
#include <hash.h>
#include <key_io.h>
#include <outputtype.h>
#include <snapshotmanager.h>
#include <script/script.h>
#include <util/message.h>

class CClaim
{
public:
    CScript sourceScriptPubKey;
    std::vector<unsigned char> signature;
    CScript targetScriptPubKey;
    int64_t nTime;
    mutable CAmount nTotalReceived;
    mutable CAmount nEligible;

    CClaim()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CClaim, obj)
    {
        READWRITE(obj.sourceScriptPubKey);
        READWRITE(obj.signature);
        READWRITE(obj.targetScriptPubKey);
        // patchcoin todo timestamp added for consistency in retrieval
        // patchcoin todo timestamps can be forged, so we shouldn't actually write them -> move that to claimindex
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(obj.nTime);
    }

    void SetNull()
    {
        sourceScriptPubKey.clear();
        signature.clear();
        targetScriptPubKey.clear();
        nTime = 0;
        nTotalReceived = 0;
        nEligible = 0;
    }

    bool IsNull() const
    {
        return sourceScriptPubKey.empty() && signature.empty() && targetScriptPubKey.empty();
    }

    bool IsValid() const
    {
        return IsClaimValid(sourceScriptPubKey, signature, targetScriptPubKey);
    }

    uint256 GetHash() const;

    // patchcoin todo compare against sourceScriptPubKey?
    friend bool operator==(const CClaim& a, const CClaim& b)
    {
        return a.GetHash() == b.GetHash();
    }

    friend bool operator!=(const CClaim& a, const CClaim& b)
    {
        return a.GetHash() != b.GetHash();
    }

    std::string ToString() const;
};

CClaim CreateNewClaim(const CScript& sourceScriptPubKey, const std::vector<unsigned char>& signature, const CScript& targetScriptPubKey);

typedef std::shared_ptr<const CClaim> CClaimRef;

template <typename... Args>
static inline CClaimRef MakeClaimRef(Args&&... args) {
    auto claim = std::make_shared<CClaim>(std::forward<Args>(args)...);
    claim->nTime = GetTimeMillis();
    return claim;
}

#endif // PATCHCOIN_PRIMITIVES_CLAIM_H
