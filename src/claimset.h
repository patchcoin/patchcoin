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
#include <logging.h>

// patchcoin todo:
// do we actually need or want this?

typedef std::vector<unsigned char> valtype;

class CClaimSetClaim : public CClaim
{
public:
    static constexpr unsigned int CLAIMSET_CLAIM_SIZE = CLAIM_SIZE + 8;
    SERIALIZE_METHODS(CClaimSetClaim, obj)
    {
        READWRITEAS(CClaim, obj);
        READWRITE(obj.nTime);
    }

    CClaimSetClaim() = default;
    CClaimSetClaim(const std::string& source_address, const std::string& signature_string, const std::string& target_address)
        : CClaim(source_address, signature_string, target_address) {}
};

class CClaimSet
{
public:
    static constexpr unsigned int MAX_CLAIMS_COUNT{1000};
    static constexpr unsigned int MAX_CLAIMSET_SIZE{CClaimSetClaim::CLAIMSET_CLAIM_SIZE * MAX_CLAIMS_COUNT + 8 /* nTime */ + CPubKey::SIGNATURE_SIZE /* 72 */};
    std::vector<CClaimSetClaim> claims;
    int64_t nTime = GetTime();
    std::vector<unsigned char> vchSig;
    // patchcoin todo move this to private

    CClaimSet() = default;
    ~CClaimSet() = default;

    SERIALIZE_METHODS(CClaimSet, obj)
    {
        READWRITE(obj.claims, obj.nTime);
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(obj.vchSig);
    }

    bool AddClaim(const CClaim& claim)
    {
        CClaimSetClaim claimSetClaim(claim.GetSourceAddress(), claim.GetSignatureString(), claim.GetTargetAddress());
        claimSetClaim.nTime = claim.nTime;
        claimSetClaim.outs = claim.outs; // patchcoin todo
        if (!claimSetClaim.IsValid()) {
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
            std::vector<CClaim> sortedClaims;
            for (const auto& [_, claim] : g_claims) {
                sortedClaims.push_back(claim);
            }
            std::sort(sortedClaims.begin(), sortedClaims.end(),
                      [](const CClaim& a, const CClaim& b) {
                          return a.nTime < b.nTime;
                      });
            for (const auto& claim : sortedClaims) {
                if (claim.nTotalReceived >= claim.GetEligible())
                    continue;
                if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) + CClaimSetClaim::CLAIMSET_CLAIM_SIZE > MAX_CLAIMSET_SIZE)
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
        LogPrintf("%s %s\n", ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION), MAX_CLAIMSET_SIZE);
        if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_CLAIMSET_SIZE) {
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

        if (IsEmpty()) {
            return false;
        }

        std::set<CScript> seenScripts;

        for (const CClaimSetClaim& claim : claims) {
            if (!seenScripts.insert(claim.GetSource()).second) {
                return false;
            }
            if (!claim.IsValid()) {
                return false;
            }
        }

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
