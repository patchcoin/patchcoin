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
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <optional>

extern std::map<CScript, CAmount> scriptPubKeysOfPeercoinSnapshot;
extern uint256 hashScriptPubKeysOfPeercoinSnapshot;

class CClaim
{
public:
    using SnapshotIterator = std::map<CScript, CAmount>::const_iterator;

private:
    std::string sourceAddress;
    std::string signatureString;
    std::string targetAddress;
    const CScript* source;
    std::vector<unsigned char> signature;
    CScript target;

    const std::map<CScript, CAmount>* snapshot = &scriptPubKeysOfPeercoinSnapshot;
    const uint256* hashSnapshot = &hashScriptPubKeysOfPeercoinSnapshot;
    SnapshotIterator snapshotIt;

    bool SnapshotIsValid() const { return *hashSnapshot == Params().GetConsensus().hashPeercoinSnapshot; }
public:
    int64_t nTime = GetTime();
    bool seen;
    CAmount nTotalReceived;
    CClaim()
    {
        SetNull();
    };

    static CScript GetScriptFromAddress(const std::string& address)
    {
        CTxDestination dest = DecodeDestination(address);
        if (!IsValidDestination(dest)) {
            throw std::invalid_argument("Invalid address: " + address);
        }
        CScript script = GetScriptForDestination(dest);
        return script;
    }

    static std::string GetAddressFromScript(const CScript& script)
    {
        std::string address;
        CTxDestination dest;
        if (ExtractDestination(script, dest)) {
            address = EncodeDestination(dest);
        }
        return address;
    }

    bool Init()
    {
        CScript source_script = GetScriptFromAddress(sourceAddress);
        std::optional<std::vector<unsigned char>> decoded_signature = DecodeBase64(signatureString);
        if (decoded_signature.has_value()) {
            signature = decoded_signature.value();
        } else {
            throw std::invalid_argument("Invalid signature format: Decoding failed.");
        }
        target = GetScriptFromAddress(targetAddress);
        if (SnapshotIsValid()) {
            snapshotIt = snapshot->find(source_script);
            if (snapshotIt != snapshot->end()) {
                source = &snapshotIt->first;
            } else {
                throw std::invalid_argument("Source script not found in snapshot");
            }
        } else {
            throw std::runtime_error("Invalid snapshot");
        }
        return true;
    }

    CClaim(const std::string& source_address_string,
           const std::string& signature_string,
           const std::string& target_address_string)
        : sourceAddress(source_address_string),
          signatureString(signature_string),
          targetAddress(target_address_string),
          source(nullptr),
          seen(false),
          nTotalReceived(0)
    {
        Init();
    }

    SERIALIZE_METHODS(CClaim, obj)
    {
        CScript source_script, target_script;
        std::vector<unsigned char> signature;
        SER_WRITE(obj, source_script = obj.GetSource());
        READWRITE(source_script);
        SER_READ(obj, obj.sourceAddress = GetAddressFromScript(source_script));
        if (!(s.GetType() & SER_GETHASH)) {
            SER_WRITE(obj, signature = obj.GetSignature());
            SER_WRITE(obj, target_script = obj.GetTarget());
            READWRITE(signature, target_script, obj.nTime, obj.seen);
            SER_READ(obj, obj.signatureString = EncodeBase64(signature));
            SER_READ(obj, obj.targetAddress = GetAddressFromScript(target_script));
        }
    }

    CScript GetSource() const { return *source; }
    std::vector<unsigned char> GetSignature() const { return signature; }
    CScript GetTarget() const { return target; }
    std::string GetSourceAddress() const { return sourceAddress; }
    std::string GetSignatureString() const { return signatureString; }
    std::string GetTargetAddress() const { return targetAddress; }
    CAmount GetPeercoinBalance() const { return (snapshotIt != snapshot->end()) ? snapshotIt->second : 0; }

    CAmount GetEligible() const
    {
        if (snapshotIt == snapshot->end()) {
            return 0;
        }
        CAmount balance = snapshotIt->second;
        if (!MoneyRange(balance))
            return 0;

        CAmount eligible = std::min(balance * 10, 50000 * COIN);
        if (!MoneyRange(eligible)) {
            eligible = 0;
        }

        return eligible;
    }

    void SetNull()
    {
        // patchcoin todo
        source = nullptr;
        nTotalReceived = 0;
        seen = false;
        signature.clear();
        target.clear();
    }

    bool IsNull() const
    {
        // patchcoin todo
        return signature.empty() && target.empty() && sourceAddress.empty() && targetAddress.empty();
    }

    bool IsValid() const
    {
        if (!SnapshotIsValid()) {
            LogPrintf("[claim] error: snapshot hash does not match consensus hash\n");
            return false;
        }
        if (sourceAddress.empty()) {
            LogPrintf("[claim] error: failed to extract destination from source script\n");
            return false;
        }
        if (targetAddress.empty()) {
            LogPrintf("[claim] error: failed to extract destination from target script\n");
            return false;
        }
        if (snapshotIt == snapshot->end()) {
            LogPrintf("[claim] error: source script not found in snapshot\n");
            return false;
        }
        if (!MoneyRange(snapshotIt->second)) {
            LogPrintf("[claim] error: peercoin balance out of range\n");
            return false;
        }
        if (!MoneyRange(GetEligible())) {
            LogPrintf("[claim] error: eligible balance out of range\n");
            return false;
        }

        MessageVerificationResult res = MessageVerify(
            sourceAddress,
            EncodeBase64(signature),
            targetAddress,
            PEERCOIN_MESSAGE_MAGIC
        );

        if (res != MessageVerificationResult::OK) {
            LogPrintf("[claim] error: signature verification failed (%d)\n", static_cast<int>(res));
            return false;
        }

        return true;
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
