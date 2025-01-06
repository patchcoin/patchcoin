#include <key.h>
#include <claimset.h>
#include <index/claimindex.h>
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
        SigningResult res = wallet.SignBlockHash(claimSet.GetHash(), *pkhash, claimSet.vchSig);
        if (res == SigningResult::OK)
            return true;
        return false;
    }
}

CClaimSet BuildClaimSet(const std::vector<CClaim>& inputClaims)
{
    CClaimSet claimset;
    claimset.AddClaims(inputClaims);
 //   claimset.nTime = GetTime();
/*
    std::sort(claimset.claims.begin(), claimset.claims.end(),
              [](const CClaim& a, const CClaim& b) {
                  return a.nTime > b.nTime;
              });
*/
    return claimset;
}

CClaimSet BuildAndSignClaimSet(const std::vector<CClaim>& inputClaims, const CWallet& wallet)
{
    CClaimSet claimset{BuildClaimSet(inputClaims)};

    if (!SignClaimSet(wallet, claimset)) {
        throw std::runtime_error("BuildAndSignClaimSet: Could not retrieve genesis key from wallet (missing or locked)");
    }

    if (!claimset.IsValid()) {
        throw std::runtime_error("BuildAndSignClaimSet: Internal error — ClaimSet not valid after signing");
    }

    return claimset;
}

void ApplyClaimSet(const CClaimSet& claimset)
{
    /*
    LOCK(cs_main);
    if (!g_claimindex) return;

    for (const CClaim& claim : claimset.claims) {
        if (g_claimindex && claim.IsValid()) {
            claim.seen = true;
            PopulateClaimAmountsB(claim); // patchcoin todo remove
            CClaim claim_t;
            g_claimindex->FindClaim(claim_t.GetHash(), claim_t);
            if (!claim_t.seen) {
                g_claimindex->AddClaim(claim);
            }
        }
    }
    */
}
