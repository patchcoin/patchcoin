#ifndef PATCHCOIN_NODE_CLAIM_H
#define PATCHCOIN_NODE_CLAIM_H

#include <primitives/claim.h>
#include <util/error.h>

namespace Consensus {
struct Params;
}

namespace node {
struct NodeContext;

[[nodiscard]] ClaimError BroadcastClaim(NodeContext& node, const CClaimRef& claim, std::string& err_string, bool relay, bool wait_callback);

std::shared_ptr<CClaim> GetClaim(const uint256& hash);

} // namespace node

#endif // PATCHCOIN_NODE_CLAIM_H
