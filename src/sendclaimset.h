#ifndef SENDCLAIMSET_H
#define SENDCLAIMSET_H

#include <claimset.h>

// patchcoin todo this entire section doesnt need to be global. move it.

inline RecursiveMutex cs_claims_seen;
inline std::map<const CScript, int64_t> claims_seen GUARDED_BY(cs_claims_seen);

inline CClaimSet send_claimset{};
inline bool genesis_key_held{false};

#endif //SENDCLAIMSET_H
