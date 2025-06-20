#include <key.h>
#include <claimset.h>
#include <index/claimindex.h>
#include <rpc/server_util.h>
#include <wallet/wallet.h>

uint256 CClaimSet::GetHash() const
{
    return SerializeHash(*this);
}

typedef std::vector<unsigned char> valtype;
bool SignClaimSet(const CWallet& wallet, CClaimSet& claimSet)
{
    std::vector<valtype> vSolutions;

    Solver(Params().GenesisBlock().vtx[0]->vout[0].scriptPubKey, vSolutions);

    if (wallet.IsLegacy()) {
        const valtype& vchPubKey = vSolutions[0];
        CKey key;
        if (!wallet.GetLegacyScriptPubKeyMan()->GetKey(CKeyID(Hash160(vchPubKey)), key))
            return false;
        if (key.GetPubKey() != CPubKey(vchPubKey))
            return false;
        return key.Sign(claimSet.GetHash(), claimSet.vchSig, 0);
    } else {
        CTxDestination address;
        CPubKey pubKey(vSolutions[0]);
        address = PKHash(pubKey);
        PKHash* pkhash = std::get_if<PKHash>(&address);
        SigningResult res = wallet.SignBlockHash(claimSet.GetHash(), *pkhash, claimSet.vchSig); // patchcoin todo does signblockhash make sense
        if (res == SigningResult::OK)
            return true;
        return false;
    }
}

bool BuildClaimSet(CClaimSet& claimSet)
{
    return claimSet.AddClaims();
}


bool BuildAndSignClaimSet(CClaimSet& claimSet, const CWallet& wallet)
{
    if (!BuildClaimSet(claimSet))
        return false;

    if (!SignClaimSet(wallet, claimSet))
        return false;

    if (!claimSet.IsValid())
        return false;

    for (const Claim& claim : claimSet.claims) {
        CAmount nTotalReceived = 0;
        if (!claim.GetReceived(&wallet, nTotalReceived) || !MoneyRange(nTotalReceived) || nTotalReceived < claim.nTotalReceived || nTotalReceived > claim.GetEligible()) {
            LogPrintf("claimset: cached and wallet amounts mismatch. this should not happen\n");
            return false;
        }
    }

    return true;
}

void ApplyClaimSet(const CClaimSet& claimset)
{
    LOCK2(cs_main, g_claims_mutex);
    if (!g_claimindex) {
        return;
    }

    for (const CClaimSetClaim& cClaim : claimset.claims) {
        ScriptError serror;
        if (cClaim.IsValid(&serror) != Claim::ClaimVerificationResult::OK) {
            return;
        }

        Claim claim(cClaim.m_is_btc ? cClaim.GetBtcSourceAddress() : cClaim.GetSourceAddress(), cClaim.GetTargetAddress(), cClaim.GetSignatureString());
        claim.nTime = cClaim.nTime;

        if (claim.IsValid(&serror) != Claim::ClaimVerificationResult::OK) {
            return;
        }

        auto it = g_claims.find(claim.GetSource());
        if (it == g_claims.end()) {
            claim.m_seen = true;
            if (!claim.Insert()) {
                return;
            }
            if (!g_claimindex->AddClaim(claim)) {
                return;
            }
        } else {
            if (!it->second.m_seen) {
                it->second.m_seen = true;
                it->second.nTime = claim.nTime;

                Claim claim_t;
                if (!g_claimindex->FindClaim(claim.GetSource(), claim_t)) {
                    return;
                }
                claim_t.nTime = claim.nTime;
                claim_t.m_seen = true;

                if (!g_claimindex->AddClaim(claim_t)) {
                    return;
                }
            }
        }
    }
}
