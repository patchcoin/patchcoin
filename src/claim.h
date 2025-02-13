#ifndef PATCHCOIN_CLAIM_H
#define PATCHCOIN_CLAIM_H

#include <map>
#include <pubkey.h>
#include <sync.h>
#include <consensus/amount.h>
#include <script/script.h>
#include <util/strencodings.h>

class CBlockIndex;
class Coin;
class COutPoint;
class uint256;
class CScript;
class Claim;

namespace wallet {
class CWallet;
} // namespace wallet

extern Mutex g_claims_mutex;
extern std::map<const CScript, Claim> g_claims GUARDED_BY(g_claims_mutex);

class Claim
{
public:
    using SnapshotIterator = std::map<CScript, CAmount>::const_iterator;

private:
    static const uint256& hashSnapshot;
    static const std::map<CScript, CAmount>& snapshot;
    static const std::map<CScript, std::vector<std::pair<COutPoint, Coin>>>& snapshot_incompatible;
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

    void Init();

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

    Claim();
    Claim(const std::string& source_address, const std::string& signature, const std::string& target_address);
    ~Claim();

    bool SnapshotIsValid() const;

    static CScript GetScriptFromAddress(const std::string& address);
    static std::string GetAddressFromScript(const CScript& script);
    static std::string LocateAddress(const uint32_t& pos);

    SERIALIZE_METHODS(Claim, obj) {
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

    std::string GetSourceAddress() const;
    std::string GetSignatureString() const;
    std::string GetTargetAddress() const;
    CScript GetSource() const; // patchcoin todo: recheck
    std::vector<unsigned char> GetSignature() const;
    CScript GetTarget() const;

    CAmount GetPeercoinBalance() const;
    CAmount GetEligible() const;
    CAmount GetMaxOutputs() const;
    bool GetReceived(const wallet::CWallet* pwallet, CAmount& received) const;
    bool GetTotalReceived(const CBlockIndex* pindex, CAmount& received, unsigned int& outputs) const;

    void SetNull();
    bool IsAnyNull() const;
    bool IsValid() const;
    bool IsSourceTarget() const;
    bool IsSourceTargetAddress() const;
    bool IsUnique() const;
    bool IsUniqueSource() const;
    bool IsUniqueTarget() const;
    bool Insert() const EXCLUSIVE_LOCKS_REQUIRED(g_claims_mutex);
    uint256 GetHash() const;
    unsigned int GetBaseSize() const;

    friend bool operator==(const Claim& a, const Claim& b) { return a.GetSource() == b.GetSource(); }
    friend bool operator!=(const Claim& a, const Claim& b) { return a.GetSource() != b.GetSource(); }
    friend bool operator<(const Claim& a, const Claim& b) { return a.nTime < b.nTime; }
    friend bool operator>(const Claim& a, const Claim& b) { return a.nTime > b.nTime; }
};

#endif // PATCHCOIN_CLAIM_H
