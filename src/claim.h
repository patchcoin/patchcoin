#ifndef PATCHCOIN_CLAIM_H
#define PATCHCOIN_CLAIM_H

#include <map>
#include <pubkey.h>
#include <sync.h>
#include <consensus/amount.h>
#include <script/script.h>
#include <util/strencodings.h>

#include <primitives/transaction.h>
#include <primitives/claim.h>
#include <script/script_error.h>

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
    enum class ClaimVerificationResult {
        ERR_NOT_INITIALIZED,
        ERR_SNAPSHOT_MISMATCH,
        ERR_SIZE_MISMATCH,
        ERR_EMPTY_FIELDS,
        ERR_SOURCE_EQUALS_TARGET,
        ERR_SOURCE_SCRIPT_NOT_FOUND,
        ERR_DECODE_SCRIPT_FAILURE,
        ERR_SIGNATURE_VERIFICATION_FAILED,
        ERR_TX_VERIFICATION_FAILED,
        ERR_BALANCE_OUT_OF_RANGE,
        ERR_RECEIVED_ABOVE_ELIGIBLE,

        ERR_DUMMY_INPUT_SIZE,
        ERR_DUMMY_OUTPUT_SIZE,
        ERR_DUMMY_VALUE_RANGE,
        ERR_DUMMY_ADDRESS_MISMATCH,
        ERR_DUMMY_PREVOUT_NOT_FOUND,
        ERR_DUMMY_INPUT_VALUE_MISMATCH,
        ERR_DUMMY_SIG_FAIL,

        ERR_MESSAGE_INVALID_ADDRESS,
        ERR_MESSAGE_DECODE_SIGNATURE,
        ERR_MESSAGE_PUBKEY_RECOVERY,
        ERR_MESSAGE_NOT_SIGNED,

        OK
    };

    using SnapshotIterator = std::map<CScript, CAmount>::const_iterator;
    using IncompatibleSnapshotIterator = std::map<CScript, std::vector<std::pair<COutPoint, Coin>>>::const_iterator;

private:
    static const uint256& hashSnapshot;
    static const std::map<CScript, CAmount>& snapshot;
    static const std::map<CScript, std::vector<std::pair<COutPoint, Coin>>>& snapshot_incompatible;
    SnapshotIterator snapshotIt;
    IncompatibleSnapshotIterator incompatibleSnapshotIt;

    CClaim m_claim;

    std::string m_source_address;
    std::string m_target_address;
    std::string m_signature_string;

    std::shared_ptr<const CScript> source;
    std::vector<std::shared_ptr<const CAmount>> m_peercoin_balances;
    CAmount m_eligible = 0;

    void Init();

public:
    unsigned int CLAIM_SIZE() const { return m_compatible ? 132 : 1000; };
    double GENESIS_OUTPUTS_AMOUNT = 0;
    unsigned int MAX_POSSIBLE_OUTPUTS = 0;
    unsigned int MAX_OUTPUTS = 0;
    bool m_init = false;
    bool m_compatible = false;
    // patchcoin todo do we want these mutable?
    mutable int64_t nTime = 0;
    mutable bool m_seen = false;
    mutable std::map<uint256, CAmount> m_outs;  // patchcoin todo remove
    mutable CAmount nTotalReceived = 0;  // patchcoin todo remove

    Claim();
    explicit Claim(const std::string& source_address, const std::string& target_address, const std::string& signature_string);
    explicit Claim(const CClaim& claim);
    ~Claim();

    bool SnapshotIsValid() const;

    static CScript GetScriptFromAddress(const std::string& address);
    static std::string GetAddressFromScript(const CScript& script);

    SERIALIZE_METHODS(Claim, obj)
    {
        READWRITE(obj.m_claim);
        if (s.GetType() & SER_DISK)
            READWRITE(obj.nTime, obj.m_seen, obj.m_outs, obj.nTotalReceived);
        SER_READ(obj, obj.Init());
    }

    std::string GetSourceAddress() const;
    std::string GetTargetAddress() const;
    std::string GetSignatureString() const;

    CScript GetSource() const; // patchcoin todo: recheck
    std::vector<unsigned char> GetSignature() const;
    CScript GetTarget() const;

    CAmount GetPeercoinBalance() const;
    CAmount GetEligible() const;
    CAmount GetMaxOutputs() const;
    bool GetReceived(const wallet::CWallet* pwallet, CAmount& received) const;
    bool GetTotalReceived(const CBlockIndex* pindex, CAmount& received, unsigned int& outputs) const;

    void SetNull();
    ClaimVerificationResult VerifyDummyTx(ScriptError *serror) const;
    ClaimVerificationResult IsValid(ScriptError *serror) const;
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
