#ifndef PATCHCOIN_CLAIMSET_H
#define PATCHCOIN_CLAIMSET_H

#include <claim.h>
#include <serialize.h>
#include <uint256.h>
#include <pubkey.h>
#include <chainparams.h>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <wallet/wallet.h>
#include <util/time.h>

class Claim;

// patchcoin todo:
// do we actually need or want this?

typedef std::vector<unsigned char> valtype;

class CClaimSetClaim : public Claim
{
public:
    unsigned int GetAllowedSize() const {
        return this->CLAIM_SIZE() + sizeof(nTime);
    }

    SERIALIZE_METHODS(CClaimSetClaim, obj)
    {
        READWRITEAS(Claim, obj);
        READWRITE(obj.nTime);
    }

    CClaimSetClaim() = default;
    CClaimSetClaim(const std::string& source_address, const std::string& target_address, const std::string& signature_string)
        : Claim(source_address, target_address, signature_string) {}
};

class CClaimSet
{
public:
    static constexpr unsigned int MAX_CLAIMS_COUNT{2500};
    std::vector<CClaimSetClaim> claims;
    int64_t nTime = GetTime();
    std::vector<unsigned char> vchSig;
    mutable bool fChecked = false;

    CClaimSet() = default;
    ~CClaimSet() = default;

    SERIALIZE_METHODS(CClaimSet, obj)
    {
        READWRITE(obj.claims, obj.nTime);
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(obj.vchSig);
    }

    bool AddClaim(const Claim& claim)
    {
        CClaimSetClaim claimSetClaim(claim.GetSourceAddress(), claim.GetTargetAddress(), claim.GetSignatureString());
        claimSetClaim.nTime = claim.nTime;
        // claimSetClaim.m_outs = claim.m_outs; // patchcoin todo
        ScriptError serror;
        if (claimSetClaim.IsValid(&serror) != Claim::ClaimVerificationResult::OK) {
            return false;
        }
        if (std::any_of(claims.begin(), claims.end(), [&](const CClaimSetClaim& claim_new) {
            return claim.GetSource() == claim_new.GetSource();
        })) {
            return false;
        }
        claims.emplace_back(claimSetClaim);
        return true;
    }

    bool AddClaims()
    {
        if (g_claims.empty()) return false;
        {
            LOCK(cs_main);
            std::vector<Claim> sortedClaims;
            for (const auto& [_, claim] : g_claims) {
                sortedClaims.push_back(claim);
            }
            std::sort(sortedClaims.begin(), sortedClaims.end(),
                      [](const Claim& a, const Claim& b) {
                          return a.nTime < b.nTime;
                      });
            for (const auto& claim : sortedClaims) {
                if (claim.nTotalReceived >= claim.GetEligible())
                    continue;
                if (claims.size() >= MAX_CLAIMS_COUNT)
                     break;
                if (!AddClaim(claim))
                    return false;
            }
            std::sort(claims.begin(), claims.end(),
                      [](const CClaimSetClaim& a, const CClaimSetClaim& b) {
                          return a.nTime > b.nTime;
                      });
        }
        return true;
    }

    bool IsEmpty() const
    {
        return claims.empty();
    }

    bool IsValid() const
    {
        if (fChecked)
            return true;

        if (IsEmpty()) {
            return false;
        }

        if (claims.size() > MAX_CLAIMS_COUNT) {
            return false;
        }

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

        std::set<CScript> seenScripts;

        for (const CClaimSetClaim& claim : claims) {
            if (!seenScripts.insert(claim.GetSource()).second) {
                return false;
            }
            ScriptError serror;
            if (claim.IsValid(&serror) != Claim::ClaimVerificationResult::OK) {
                return false;
            }
        }

        fChecked = true;

        return true;
    }

    uint256 GetHash() const;

    friend bool operator==(const CClaimSet& a, const CClaimSet& b) { return a.GetHash() == b.GetHash(); }
    friend bool operator!=(const CClaimSet& a, const CClaimSet& b) { return a.GetHash() != b.GetHash(); }
    // patchcoin todo:
    friend bool operator<(const CClaimSet& a, const CClaimSet& b) { return a.claims.front().nTime < b.claims.front().nTime; }
    friend bool operator>(const CClaimSet& a, const CClaimSet& b) { return a.claims.front().nTime > b.claims.front().nTime; }
};

bool SignClaimSet(const CWallet& wallet, CClaimSet& claimSet);

bool BuildClaimSet(CClaimSet& claimSet);

bool BuildAndSignClaimSet(CClaimSet& claimSet, const CWallet& wallet);

void ApplyClaimSet(const CClaimSet& claimSet);

void MaybeDealWithClaimSet(const CWallet& wallet, bool force = false);

#endif // PATCHCOIN_CLAIMSET_H
