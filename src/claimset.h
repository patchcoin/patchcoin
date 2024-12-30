#ifndef PATCHCOIN_CLAIMSET_H
#define PATCHCOIN_CLAIMSET_H

#include <primitives/claim.h>
#include <serialize.h>
#include <uint256.h>
#include <pubkey.h>
#include <hash.h>
#include <chainparams.h>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <wallet/wallet.h>

typedef std::vector<unsigned char> valtype;

class CClaimSet
{
public:
    std::vector<CClaim> claims{};
    int64_t nTime;
    std::vector<unsigned char> vchSig;

    CClaimSet() : nTime(0) {}

    SERIALIZE_METHODS(CClaimSet, obj)
    {
        READWRITE(obj.claims);
        READWRITE(obj.nTime);
        READWRITE(obj.vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << claims;
        ss << nTime;
        return ss.GetHash();
    }

    bool IsEmpty() const
    {
        return claims.empty();
    }

    bool IsValid() const
    {
        if (vchSig.empty()) {
            return false;
        }

        std::vector<valtype> vSolutions;
        if (Solver(Params().GenesisBlock().vtx[0]->vout[0].scriptPubKey, vSolutions) != TxoutType::PUBKEY) {
            return false;
        }
        const valtype& vchPubKey = vSolutions[0];
        CPubKey key(vchPubKey);
        if (!key.Verify(GetHash(), vchSig)) {
            return false;
        }

        if (claims.empty()) { // patchcoin todo might do nothing
            return false;
        }

        for (const auto& claim : claims) {
            if (!claim.IsValid()) {
                return false;
            }
        }

        return true;
    }
};

CClaimSet BuildClaimSet(const std::vector<CClaim>& inputClaims);

CClaimSet BuildAndSignClaimSet(const std::vector<CClaim>& inputClaims, const CWallet& wallet);

void ApplyClaimSet(const CClaimSet& claimSet);

#endif // PATCHCOIN_CLAIMSET_H
