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
#include <chain.h>
#include <optional>
#include <logging.h>

class CClaim;

namespace wallet {
class CWallet;
} // namespace wallet

extern std::map<const CScript, CClaim> g_claims;

class CClaim
{
public:
    using SnapshotIterator = std::map<CScript, CAmount>::const_iterator;

private:
    static const std::map<CScript, CAmount>& snapshot;
    static const uint256& hashSnapshot;
    SnapshotIterator snapshotIt;
    uint32_t snapshotPos;

    std::string sourceAddress;
    std::string signatureString;
    std::string targetAddress;
    std::shared_ptr<const CScript> source;
    std::vector<unsigned char> signature;
    CScript target;
    std::shared_ptr<const CAmount> peercoinBalance;
    CAmount eligible = 0;

    void Init() {
        const CScript source_script = GetScriptFromAddress(sourceAddress);

        if (auto decoded_signature = DecodeBase64(signatureString)) {
            signature = *decoded_signature;
        }

        target = GetScriptFromAddress(targetAddress);

        if (SnapshotIsValid()) {
            snapshotIt = snapshot.find(source_script);
            if (snapshotIt != snapshot.end()) {
                snapshotPos = std::distance(snapshot.begin(), snapshotIt);
                source = std::make_shared<CScript>(snapshotIt->first);
                peercoinBalance = std::make_shared<CAmount>(snapshotIt->second);
                eligible = GetEligible();
                GENESIS_OUTPUTS_AMOUNT = static_cast<double>(Params().GetConsensus().genesisValue) / static_cast<double>(Params().GetConsensus().genesisOutputs);
                MAX_POSSIBLE_OUTPUTS = std::min(16u, static_cast<unsigned int>(std::ceil(static_cast<double>(MAX_CLAIM_REWARD) / GENESIS_OUTPUTS_AMOUNT))) + 4;
                MAX_OUTPUTS = GetMaxOutputs();
            }
        }
    }

public:
    static constexpr unsigned int CLAIM_SIZE{6 /* snapshotPos */  + CPubKey::COMPACT_SIGNATURE_SIZE /* 65 */ + 25 /* target script */};
    double GENESIS_OUTPUTS_AMOUNT = 0;
    unsigned int MAX_POSSIBLE_OUTPUTS = 0;
    unsigned int MAX_OUTPUTS = 0;
    // patchcoin todo do we want these mutable?
    mutable int64_t nTime = GetTime();
    mutable bool seen = false;
    mutable std::map<uint256, CAmount> outs;
    mutable CAmount nTotalReceived = 0;

    CClaim()
    {
        SetNull();
    }

    CClaim(const std::string& source_address, const std::string& signature, const std::string& target_address)
        : sourceAddress(source_address), signatureString(signature), targetAddress(target_address)
    {
        Init();
    }

    ~CClaim() = default;

    bool SnapshotIsValid() const {
        return hashSnapshot == Params().GetConsensus().hashPeercoinSnapshot && !SnapshotManager::Peercoin().GetScriptPubKeys().empty();
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

    static std::string LocateAddress(const uint32_t pos)
    {
        std::string address;
        // const auto& snapshot = SnapshotManager::Peercoin().GetScriptPubKeys();
        const CClaim dummy;
        if (dummy.SnapshotIsValid() && pos < static_cast<uint32_t>(snapshot.size())) {
            const auto& it = std::next(snapshot.begin(), pos);
            if (it != snapshot.end()) {
                address = GetAddressFromScript(it->first);
            }
        }
        return address;
    }

    SERIALIZE_METHODS(CClaim, obj) {
        std::vector<unsigned char> signature;
        CScript target_script;
        SER_WRITE(obj, signature = obj.GetSignature());
        SER_WRITE(obj, target_script = obj.GetTarget());
        READWRITE(obj.snapshotPos, signature, target_script);
        if (s.GetType() & SER_DISK)
            READWRITE(obj.nTime, obj.seen, obj.outs, obj.nTotalReceived);
        SER_READ(obj, obj.sourceAddress = LocateAddress(obj.snapshotPos));
        SER_READ(obj, obj.signatureString = EncodeBase64(signature));
        SER_READ(obj, obj.targetAddress = GetAddressFromScript(target_script));
        SER_READ(obj, obj.Init());
    }

    std::string GetSourceAddress() const { return sourceAddress; }
    std::string GetSignatureString() const { return signatureString; }
    std::string GetTargetAddress() const { return targetAddress; }

    CScript GetSource() const { return source ? *source : CScript(); } // patchcoin todo: recheck
    std::vector<unsigned char> GetSignature() const { return signature; }
    CScript GetTarget() const { return target; }

    CAmount GetPeercoinBalance() const {
        return snapshotIt != SnapshotManager::Peercoin().GetScriptPubKeys().end() ? *peercoinBalance : 0;
    }

    CAmount GetEligible() const
    {
        if (eligible > 0) return eligible;
        CAmount eligibleAmount = 0;
        if (!SnapshotManager::Peercoin().CalculateEligible(GetPeercoinBalance(), eligibleAmount) || !MoneyRange(eligibleAmount)) {
            return 0;
        }
        return eligibleAmount;
    }

    CAmount GetMaxOutputs() const
    {
        // patchcoin todo not accurate and merely serves as a static fence-in
        // patchcoin todo add look-ahead, similar to
        const unsigned int maxOutputs = static_cast<unsigned int>(std::ceil(static_cast<double>(GetEligible()) / GENESIS_OUTPUTS_AMOUNT)) + 4;
        return std::min(maxOutputs, MAX_POSSIBLE_OUTPUTS);
    }

    bool GetReceived(const wallet::CWallet* pwallet, CAmount& received) const
    {
        return SnapshotManager::Peercoin().CalculateReceived(pwallet, target, received);
    }

    bool GetTotalReceived(const CBlockIndex* pindex, CAmount& received, unsigned int& outputs) const
    {
        if (GetEligible() == 0) return false;
        while (pindex && outputs < outs.size()) {
            const auto& it = outs.find(pindex->GetBlockHash());
            if (it != outs.end()) {
                outputs++;
                received += it->second;
            }
            pindex = pindex->pprev;
        }
        if (received > eligible) {
            return false;
        }
        if (outputs > MAX_POSSIBLE_OUTPUTS /*|| outputs > MAX_OUTPUTS || outputs > GetMaxOutputs()*/) {
            return false;
        }
        return true;
    }

    void SetNull()
    {
        sourceAddress.clear();
        signatureString.clear();
        targetAddress.clear();
        source.reset();
        signature.clear();
        target.clear();
        peercoinBalance.reset();
        seen = false;
        outs.clear();
    }

    bool IsAnyNull() const
    {
        return sourceAddress.empty() || signatureString.empty() || targetAddress.empty()
            || !source || signature.empty() || target.empty() || !peercoinBalance
            || GetSource().empty() || GetSignature().empty() || GetTarget().empty();
    }

    bool IsValid() const
    {
        try {
            if (GetBaseSize() != CLAIM_SIZE) {
                LogPrintf("[claim] error: size mismatch: current=%s target=%s\n", GetBaseSize(), CLAIM_SIZE);
                return false;
            }
            if (IsAnyNull()) {
                LogPrintf("[claim] error: called on an empty string source=%s signature=%s target=%s\n",
                   sourceAddress.c_str(), signatureString.c_str(), targetAddress.c_str());
                return false;
            }
            if (IsSourceTarget() || IsSourceTargetAddress()) {
                LogPrintf("[claim] error: input matches output address\n");
                return false;
            }
            if (!SnapshotIsValid()) {
                LogPrintf("[claim] error: snapshot hash does not match consensus hash\n");
                return false;
            }
            if (snapshotIt == snapshot.end() || snapshotPos >= snapshot.size()) {
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
            if (!MoneyRange(snapshotIt->second) || !MoneyRange(this->GetPeercoinBalance())) {
                LogPrintf("[claim] error: peercoin balance out of range\n");
                return false;
            }
            const CAmount eligible = GetEligible();
            if (!MoneyRange(eligible)) {
                LogPrintf("[claim] error: eligible balance out of range\n");
                return false;
            }
            if (eligible < nTotalReceived) {
                LogPrintf("[claim] error: total received above eligible\n");
                return false;
            }
            // patchcoin todo: set isChecked and return early? need to make sure we haven't been modified
            return true;
        } catch (const std::exception& e) {
            LogPrintf("[claim] error: unexpected exception in validation: %s\n", e.what());
            return false;
        }
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

    bool IsUniqueTarget() const
    {
        if (target.empty()) return false;
        // patchcoin todo ensure consistency with claimset
        return std::none_of(g_claims.begin(), g_claims.end(), [this](const auto& entry) {
            const CClaim& claim = entry.second;
            return claim.GetTarget() == target;
        });
    }

    bool Insert() const
    {
        if (!(IsValid() && IsUnique()))
            return false;
        const auto [_, inserted]{g_claims.try_emplace(GetSource(), *this)};
        return inserted && !IsUniqueSource();
    }

    uint256 GetHash() const;

    unsigned int GetBaseSize() const;

    friend bool operator==(const CClaim& a, const CClaim& b) { return a.GetSource() == b.GetSource(); }
    friend bool operator!=(const CClaim& a, const CClaim& b) { return a.GetSource() != b.GetSource(); }
    friend bool operator<(const CClaim& a, const CClaim& b) { return a.nTime < b.nTime; }
    friend bool operator>(const CClaim& a, const CClaim& b) { return a.nTime > b.nTime; }
};

#endif // PATCHCOIN_PRIMITIVES_CLAIM_H
