#ifndef SENDCLAIMSET_H
#define SENDCLAIMSET_H

#include <claimset.h>

// patchcoin todo this entire section doesnt need to be global. move it.

inline RecursiveMutex cs_claims_seen;
inline std::map<uint256, int64_t> claims_seen;

inline bool send_claimset{false};
inline CClaimSet send_claimset_to_send{};
inline CClaimSet last_claimset_received{};
inline bool genesis_key_held{false};

#endif //SENDCLAIMSET_H
