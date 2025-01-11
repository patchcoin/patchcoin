#ifndef PATCHCOIN_PRIMITIVES_CLAIM_H
#define PATCHCOIN_PRIMITIVES_CLAIM_H

#include <key_io.h>
#include <outputtype.h>
#include <snapshotmanager.h>
#include <script/script.h>
#include <util/message.h>
#include <util/strencodings.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cassert>
#include <optional>

class CClaim;

namespace wallet {
class CWallet;
} // namespace wallet

// inline std::set<std::shared_ptr<const CScript>> g_claims_sources;
// inline std::set<const CScript> g_claims_targets;
inline std::map<CScript, CClaim> g_claims; // unique ptr to scriptsOfPeercoinScnapshot / use getHash instead?

class CClaim
{
public:
    using SnapshotIterator = std::map<CScript, CAmount>::const_iterator;

private:
    std::shared_ptr<const std::map<CScript, CAmount>> snapshot = std::make_shared<std::map<CScript, CAmount>>(scriptPubKeysOfPeercoinSnapshot);
    std::shared_ptr<const uint256> hashSnapshot = std::make_shared<uint256>(hashScriptPubKeysOfPeercoinSnapshot);
    SnapshotIterator snapshotIt;

    size_t snapshotPos;
    std::string sourceAddress;
    std::string signatureString;
    std::string targetAddress;
    std::shared_ptr<const CScript> source; // patchcoin todo move to unique_ptr?
    std::vector<unsigned char> signature;
    CScript target;
    std::shared_ptr<const CAmount> peercoinBalance;

public:
    int64_t nTime = GetTime(); // patchcoin todo set this on receive or create, not here

    mutable bool seen = false;
    bool markedValid = false;
    CAmount nTotalReceived = 0;
    CClaim()
    {
        SetNull();
    }

    bool SnapshotIsValid() const {
        return hashSnapshot && hashSnapshot != nullptr && *hashSnapshot == Params().GetConsensus().hashPeercoinSnapshot && snapshot && snapshot != nullptr && !snapshot->empty();
    }

    static CScript GetScriptFromAddress(const std::string& address)
    {
        CScript script;
        CTxDestination dest = DecodeDestination(address);
        if (IsValidDestination(dest)) {
            script = GetScriptForDestination(dest);
        }
        return script;
    }

    static std::string GetAddressFromScript(const CScript& script)
    {
        std::string address;
        CTxDestination dest;
        if (ExtractDestination(script, dest) && IsValidDestination(dest)) {
            address = EncodeDestination(dest);
        }
        return address;
    }

    static std::string LocateAddress(size_t pos)
    {
        std::string address;
        const CClaim dummy;
        if (dummy.SnapshotIsValid() && pos < scriptPubKeysOfPeercoinSnapshot.size()) {
            const auto it = std::next(scriptPubKeysOfPeercoinSnapshot.begin(), pos);
            if (it != scriptPubKeysOfPeercoinSnapshot.end()) {
                address = GetAddressFromScript(it->first);
            }
        }
        return address;
    }

    void Init()
    {
        CScript source_script = GetScriptFromAddress(sourceAddress);
        std::optional<std::vector<unsigned char>> decoded_signature = DecodeBase64(signatureString);
        if (decoded_signature.has_value()) {
            signature = decoded_signature.value();
        }
        target = GetScriptFromAddress(targetAddress); // patchcoin todo maybe sign this
        if (SnapshotIsValid()) {
            snapshotIt = snapshot->find(source_script);
            if (snapshotIt != snapshot->end()) {
                snapshotPos = std::distance(snapshot->begin(), snapshotIt);
                source = std::make_shared<CScript>(snapshotIt->first);
                peercoinBalance = std::make_shared<CAmount>(snapshotIt->second);
            }
        }
    }

    CClaim(const std::string& source_address, const std::string& signature, const std::string& target_address)
    {
        SetNull();
        sourceAddress = source_address;
        signatureString = signature;
        targetAddress = target_address;
        Init();
    }

    SERIALIZE_METHODS(CClaim, obj)
    {
        CScript /*source_script,*/ target_script;
        std::vector<unsigned char> signature;
        // SER_WRITE(obj, source_script = GetScriptFromAddress(obj.sourceAddress)); // patchcoin todo could also just write a bunch of hashes and then match that against pub key list. this might lower data needed to be shared per claimset, not sure
        // READWRITE(source_script);
        // SER_READ(obj, obj.sourceAddress = GetAddressFromScript(source_script)); // patchcoin todo and then retrieve that here
        // assert(obj.SnapshotIsValid() && obj.snapshotPos < obj.snapshot->size()); // patchcoin todo move this away. this wont be fun to deal with on network
        // SER_WRITE();
        READWRITE(obj.snapshotPos); // patchcoin todo there is little point in hashing just this number
        SER_READ(obj, obj.sourceAddress = LocateAddress(obj.snapshotPos));
        if (!(s.GetType() & SER_GETHASH)) {
            SER_WRITE(obj, signature = obj.GetSignature());
            SER_WRITE(obj, target_script = obj.GetTarget());
            // patchcoin todo add SER_NETWORK check -> nTime + seen only used on ClaimSet
            READWRITE(signature, target_script); // patchcoin todo potentially track transactions related to the claim + tally
            if (s.GetType() & SER_DISK)
                READWRITE(obj.nTime, obj.seen);
            SER_READ(obj, obj.signatureString = EncodeBase64(signature));
            SER_READ(obj, obj.targetAddress = GetAddressFromScript(target_script));
        }
    }

    std::string GetSourceAddress() const { return sourceAddress; }
    std::string GetSignatureString() const { return signatureString; }
    std::string GetTargetAddress() const { return targetAddress; }

    // patchcoin todo calculate once and set them, no need to fully run that in the constructor
    CScript GetSource() const { return *source; }
    std::vector<unsigned char> GetSignature() const { return signature; }
    CScript GetTarget() const { return target; }

    CAmount GetPeercoinBalance() const { return (snapshotIt != snapshot->end()) ? *peercoinBalance : 0; }
    CAmount GetEligible() const
    {
        CAmount eligible = 0;
        if (!(CalculateEligible(GetPeercoinBalance(), eligible) && MoneyRange(eligible)))
            return 0;

        return eligible;
    }
    bool GetReceived(const wallet::CWallet* pwallet, CAmount& received) const
    {
        return CalculateReceived(pwallet, target, received);
    }

    void SetNull()
    {
        sourceAddress.clear();
        signatureString.clear();
        targetAddress.clear();
        source = nullptr;
        signature.clear();
        target.clear();
        peercoinBalance = nullptr;
        seen = false;
        nTotalReceived = 0;
    }

    bool IsAnyNull() const
    {
        return sourceAddress.empty() || signatureString.empty() || targetAddress.empty()
               || source == nullptr || signature.empty() || target.empty()
               || peercoinBalance == nullptr;
    }

    bool IsValid() const
    {
        if (IsAnyNull()) {
            LogPrintf("[claim] error: called on an empty string source=%s, signature=%s, target=%s\n",
                sourceAddress.c_str(), signatureString.c_str(), targetAddress.c_str());
            return false;
        }
        if (IsSourceTarget() || IsSourceTargetAddress()) {
            LogPrintf("[claim] error: input matches output address");
            return false;
        }
        if (!SnapshotIsValid()) {
            LogPrintf("[claim] error: snapshot hash does not match consensus hash\n");
            return false;
        }
        if (snapshotIt == snapshot->end()) {
            LogPrintf("[claim] error: source script not found in snapshot\n");
            return false;
        }
        if (sourceAddress.empty() || GetScriptFromAddress(sourceAddress).empty() || source == nullptr || GetAddressFromScript(*source).empty()) {
            LogPrintf("[claim] error: failed to extract destination from source script\n");
            return false;
        }
        if (targetAddress.empty() || GetScriptFromAddress(targetAddress).empty() || GetAddressFromScript(target).empty()) {
            LogPrintf("[claim] error: failed to extract destination from target script\n");
            return false;
        }
        if (!MoneyRange(snapshotIt->second) || !MoneyRange(this->GetPeercoinBalance())) {
            LogPrintf("[claim] error: peercoin balance out of range\n");
            return false;
        }
        if (!MoneyRange(GetEligible())) {
            LogPrintf("[claim] error: eligible balance out of range\n");
            return false;
        }
        MessageVerificationResult res = MessageVerify(
            sourceAddress,
            signatureString,
            targetAddress,
            PEERCOIN_MESSAGE_MAGIC
        );
        if (res != MessageVerificationResult::OK) {
            LogPrintf("[claim] error: signature verification failed (%d)\n", static_cast<int>(res));
            return false;
        }
        // patchcoin todo: set isChecked and return early? need to make sure we haven't been modified
        return true;
    }

    bool IsSourceTarget() const
    {
        return GetSource() == GetTarget();
    }

    bool IsSourceTargetAddress() const
    {
        return GetSourceAddress() == GetTargetAddress();
    }

    bool IsUnique() const
    {
        // patchcoin ensure consistency with claimset
        return !IsSourceTargetAddress() && !IsSourceTarget() && IsUniqueSource() && IsUniqueTarget();
    }

    bool IsUniqueSource() const
    {
        // patchcoin ensure consistency with claimset
        return source && !source->empty() && g_claims.count(*source) == 0;
    }

    bool IsUniqueTarget() const {
        if (target.empty()) return false;
        // patchcoin todo ensure consistency with claimset
        return std::none_of(g_claims.begin(), g_claims.end(), [this](const auto& entry) {
            const CClaim& claim = entry.second;
            return claim.GetTarget() == target;
        });
    }

    bool Commit() const
    {// patchcoin todo try erase first?
        if (!(IsValid() && IsUnique()))
            return false;
        const CClaim claim(GetSourceAddress(), GetSignatureString(), GetTargetAddress());
        g_claims.try_emplace(claim.GetSource(), claim); // patchcoin todo GetHash()
        return !IsUniqueSource();
    }

    uint256 GetHash() const;

    friend bool operator==(const CClaim& a, const CClaim& b)
    {
        return a.GetHash() == b.GetHash();
    }

    friend bool operator!=(const CClaim& a, const CClaim& b)
    {
        return a.GetHash() != b.GetHash();
    }

    /*

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "CClaim{sourceAddress: " << sourceAddress
            << ", targetAddress: " << targetAddress
            << ", eligible: " << GetEligible()
            << ", balance: " << GetPeercoinBalance()
            << ", time: " << nTime << "}";
        return oss.str();
    }
    */
};

#endif // PATCHCOIN_PRIMITIVES_CLAIM_H
