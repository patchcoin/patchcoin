#include <index/claimindex.h>
#include <net.h>
#include <node/context.h>
#include <validationinterface.h>
#include <node/claim.h>

#include <future>
#include <net_processing.h>

namespace node {

ClaimError BroadcastClaim(NodeContext& node, const CClaimRef& claim, std::string& err_string, bool relay, bool wait_callback)
{
    assert(node.peerman);

    std::promise<void> promise;
    uint256 hash = claim->GetHash();
    bool callback_set = false;

    {
        LOCK(cs_main);
        CClaim maybe_claim;

        std::vector<CClaim> claims;
        if (g_claimindex && g_claimindex->GetAllClaims(claims)) {
            for (const CClaim& claimFromDb : claims) {
                if (claimFromDb.sourceScriptPubKey == claim->sourceScriptPubKey && claimFromDb.targetScriptPubKey != claim->targetScriptPubKey) {
                    return ClaimError::ALREADY_EXISTS;
                }
            }
        } else {
            LogPrint(BCLog::NET, "Unable to access ClaimIndex");
            return ClaimError::INDEX_ERROR;
        }

        if (g_claimindex && g_claimindex->FindClaim(hash, maybe_claim)) {
            return ClaimError::ALREADY_EXISTS;
        }

        if (g_claimindex && !g_claimindex->AddClaim(*claim)) {
            err_string = "Failed to add claim to index";
            return ClaimError::INDEX_ERROR;
        }

        // Set up callback if waiting for validation
        /*
        if (wait_callback) {
            CallFunctionInValidationInterfaceQueue([&promise] {
                promise.set_value();
            });
            callback_set = true;
        }
        */
    } // Release cs_main lock

    // Wait for the callback to complete
    if (callback_set) {
        promise.get_future().wait();
    }

    // Add to claims_seen and relay the claim if requested
    if (relay) {
        {
            LOCK(cs_claims_seen);
            if (claims_seen.count(claim)) {
                LogPrint(BCLog::NET, "Claim already in global claims_seen: %s\n", claim->GetHash().ToString());
            } else {
                claims_seen.insert(claim);
                // node.peerman->RelayClaim(claimRef);
            }
        }
    }

    return ClaimError::OK;
}


std::shared_ptr<CClaim> GetClaim(const uint256& hash)
{
    std::shared_ptr<CClaim> claim = std::make_shared<CClaim>();
    if (g_claimindex && g_claimindex->FindClaim(hash, *claim)) {
        return claim;
    }
    return nullptr;
}

} // namespace node
