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
    // patchcoin todo move this to private
    std::map<CScript, CClaim> claims;
    int64_t nTime;
    std::vector<unsigned char> vchSig;
    // patchcoin todo move this to private
    CClaimSet() : nTime(0)
    {
    }

    SERIALIZE_METHODS(CClaimSet, obj)
    {
        READWRITE(obj.claims, obj.nTime);
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(obj.vchSig);
    }

    bool AddClaim(const CClaim& claim)
    {
        if (!claim.IsValid()) {
            return false;
        }
        if (claims.find(claim.GetSource()) != claims.end()) {
            return false;
        }
        claims.emplace(claim.GetSource(), claim);
        return true;
    }

    bool AddClaims(const std::vector<CClaim>& newClaims)
    {
        bool allAdded = true;
        for (const auto& claim : newClaims) {
            if (!AddClaim(claim)) {
                allAdded = false;
            }
        }
        return allAdded;
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

        if (IsEmpty()) {
            return false;
        }

        for (const auto& [hash, claim] : claims) {
            if (!claim.IsValid()) {
                return false;
            }
        }

        return true;
    }

    uint256 GetHash() const;

    std::vector<CClaim> GetSortedClaims() const
    {
        std::vector<CClaim> sortedClaims;
        sortedClaims.reserve(claims.size());
        for (const auto& [hash, claim] : claims) {
            sortedClaims.push_back(claim);
        }
        std::sort(sortedClaims.begin(), sortedClaims.end(),
                  [](const CClaim& a, const CClaim& b) {
                      return a.nTime > b.nTime;
                  });
        return sortedClaims;
    }

    friend bool operator==(const CClaimSet& a, const CClaimSet& b) { return a.GetHash() == b.GetHash(); }
    friend bool operator!=(const CClaimSet& a, const CClaimSet& b) { return a.GetHash() != b.GetHash(); }
    // patchcoin todo:
    friend bool operator<(const CClaimSet& a, const CClaimSet& b) { return a.nTime < b.nTime; }
    friend bool operator>(const CClaimSet& a, const CClaimSet& b) { return a.nTime > b.nTime; }
};

CClaimSet BuildClaimSet(const std::vector<CClaim>& inputClaims);

CClaimSet BuildAndSignClaimSet(const std::vector<CClaim>& inputClaims, const CWallet& wallet);

void ApplyClaimSet(const CClaimSet& claimSet);

#endif // PATCHCOIN_CLAIMSET_H
