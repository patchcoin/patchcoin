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

// patchcoin todo:
// do we actually need or want this?

typedef std::vector<unsigned char> valtype;

class CClaimSetClaim : public CClaim
{
public:
    SERIALIZE_METHODS(CClaimSetClaim, obj)
    {
        READWRITEAS(CClaim, obj);
        READWRITE(obj.nTime);
    }
};

class CClaimSet
{
public:
    // patchcoin todo re-check here -> scripts claims dont currently link to the script set, so fix that
    // also, again, not being re-layed on connection currently
    // maybe also store claimset in a special key or something and then load that on startup. that way we dont need to load individual claims(might be cool or bad not sure yet)
    // patchcoin todo move this to private
    std::vector<CClaimSetClaim> claims;
    int64_t nTime;
    std::vector<unsigned char> vchSig;
    // patchcoin todo move this to private
    CClaimSet() : nTime(GetTime())
    {
        std::vector<CClaim> sortedClaims;
        sortedClaims.reserve(g_claims.size());
        for (const auto& [_, claim] : g_claims) {
            sortedClaims.push_back(claim);
        }
        std::sort(sortedClaims.begin(), sortedClaims.end(),
                  [](const CClaim& a, const CClaim& b) {
                      return a.nTime > b.nTime;
                  });
        for (const auto& claim : sortedClaims) {
            AddClaim(static_cast<const CClaimSetClaim&>(claim));
        }
        return;
    }

    SERIALIZE_METHODS(CClaimSet, obj)
    {
        READWRITE(obj.claims, obj.nTime);
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(obj.vchSig);
    }

    bool AddClaim(const CClaimSetClaim& claim)
    {
        if (!claim.IsValid()) {
            return false;
        }
        if (std::any_of(claims.begin(), claims.end(), [&](const CClaim& claim_new) {
            return claim.GetSource() == claim_new.GetSource();
        })) {
            return false;
        }
        claims.emplace_back(claim);
        return true;
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

        for (const auto& claim : claims) {
            CClaim tempClaim(claim.GetSourceAddress(), claim.GetSignatureString(), claim.GetTargetAddress());
            if (!tempClaim.IsValid()) {
                return false;
            }
        }

        return true;
    }

    uint256 GetHash() const;

    friend bool operator==(const CClaimSet& a, const CClaimSet& b) { return a.GetHash() == b.GetHash(); }
    friend bool operator!=(const CClaimSet& a, const CClaimSet& b) { return a.GetHash() != b.GetHash(); }
    // patchcoin todo:
    friend bool operator<(const CClaimSet& a, const CClaimSet& b) { return a.claims.size() < b.claims.size(); }
    friend bool operator>(const CClaimSet& a, const CClaimSet& b) { return a.claims.size() > b.claims.size(); }
};

CClaimSet BuildClaimSet(const std::vector<CClaim>& inputClaims);

CClaimSet BuildAndSignClaimSet(const std::vector<CClaim>& inputClaims, const CWallet& wallet);

void ApplyClaimSet(const CClaimSet& claimSet);

void MaybeDealWithClaimSet(const CWallet& wallet, bool force = false);

#endif // PATCHCOIN_CLAIMSET_H
