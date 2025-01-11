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

CClaimSet BuildAndSignClaimSet(const CWallet& wallet)
{
    CClaimSet claimset{};

    if (!SignClaimSet(wallet, claimset)) {
        throw std::runtime_error("BuildAndSignClaimSet: Could not retrieve genesis key from wallet (missing or locked)");
    }

    if (!claimset.IsValid()) {
        throw std::runtime_error("BuildAndSignClaimSet: Internal error — ClaimSet not valid after signing");
    }

    return claimset;
}


void MaybeDealWithClaimSet(const CWallet& wallet, const bool force)
{
    if (!genesis_key_held) return;
    // patchcoin dont check for size in case we ever decide to switch to range
    if (force || send_claimset_to_send.claims.size() < g_claims.size() || GetTime() - send_claimset_to_send.nTime > 60) {
        std::vector<CClaim> claims;
        for (const auto& [_, claim] : g_claims) {
            claims.emplace_back(claim);
        }
        send_claimset_to_send = BuildAndSignClaimSet(wallet);
        send_claimset = true;
    }
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
            it->second.nTime = claim.nTime;
        }

        if (g_claimindex) {
            claim.seen = true;
            CClaim claim_t;
            g_claimindex->FindClaim(claim.GetHash(), claim_t);
            if (!claim_t.seen) {
                g_claimindex->AddClaim(claim);
            }
        }
    }
}
