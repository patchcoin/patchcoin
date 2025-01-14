#include <key.h>
#include <claimset.h>
#include <sendclaimset.h>
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

    return true;
}


void MaybeDealWithClaimSet(const CWallet& wallet, const bool force)
{
    if (!genesis_key_held || g_claims.empty()) return;
    // patchcoin dont check for size in case we ever decide to switch to range

    /*
    if (force || send_claimset_to_send.claims.size() < g_claims.size() || GetTime() - send_claimset_to_send.nTime > 60) {
        // send_claimset_to_send = BuildAndSignClaimSet(wallet);
    }
    */
    CClaimSet cset;
    if (BuildAndSignClaimSet(cset, wallet) && !cset.claims.empty()) {
        send_claimset_to_send = cset;
        send_claimset = true;
    };
}

void ApplyClaimSet(const CClaimSet& claimset)
{
    LOCK(cs_main);
    if (!g_claimindex) return;

    for (const auto& claim : claimset.claims) {
        if (!claim.IsValid()) continue;
        const auto& it = g_claims.find(claim.GetSource());
        // patchcoin todo possibly update more / merge these two
        if (it == g_claims.end()) {
            claim.Commit();
        } else if (!it->second.seen) {
            it->second.seen = true;
            // it->second.nTime = claim.nTime;
        }
        claim.seen = true;
        g_claimindex->AddClaim(claim);

        /* patchcoin todo
        if (g_claimindex) {
            claim.seen = true;
            CClaim claim_t;
            g_claimindex->FindClaim(claim.GetSource(), claim_t);
            if (!claim_t.seen) {
                g_claimindex->AddClaim(claim);
            }
        }
        */
    }
}
