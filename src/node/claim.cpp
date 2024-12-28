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

        // Check if the claim already exists in the claim index
        // patchcoin todo could add a flag to force send, although that should be debounced / limited as well. that or set a timer since last attempt
        if (g_claimindex->FindClaim(hash, maybe_claim)) {
            return ClaimError::ALREADY_EXISTS;
        }

        // Add the claim to the claim index
        if (!g_claimindex->AddClaim(*claim)) {
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
