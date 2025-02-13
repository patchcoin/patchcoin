#include <chain.h>
#include <claim.h>
#include <chainparams.h>
#include <core_io.h>
#include <hash.h>
#include <key_io.h>
#include <snapshotmanager.h>
#include <policy/policy.h>
#include <script/standard.h>
#include <util/message.h>
#include <util/strencodings.h>

Mutex g_claims_mutex;
std::map<const CScript, Claim> g_claims GUARDED_BY(g_claims_mutex);

// const Claim::sman = SnapshotManager::Peercoin();
const std::map<CScript, CAmount>& Claim::snapshot = SnapshotManager::Peercoin().GetScriptPubKeys();
const std::map<CScript, std::vector<std::pair<COutPoint, Coin>>>& Claim::snapshot_incompatible = SnapshotManager::Peercoin().GetIncompatibleScriptPubKeys();
const uint256& Claim::hashSnapshot = SnapshotManager::Peercoin().GetHashScripts();

Claim::Claim()
{
    SetNull();
}

Claim::Claim(const std::string& source_address, const std::string& target_address, const std::string& signature_string)
    : m_claim(source_address, target_address, signature_string)
{
    SetNull();
    Init();
}

Claim::Claim(const CClaim& claim)
    : m_claim(claim)
{
    SetNull();
    Init();
}

Claim::~Claim() = default;

void Claim::Init()
{
    if (!SnapshotIsValid()) {
        return;
    }

    if (m_claim.sourceAddress.empty() || m_claim.targetAddress.empty() || m_claim.signatureString.empty()) {
        return;
    }

    const CScript source_tmp = GetScriptFromAddress(m_claim.sourceAddress);
    m_source_address = GetAddressFromScript(source_tmp);

    m_target = GetScriptFromAddress(m_claim.targetAddress);
    m_target_address = GetAddressFromScript(m_target);

    if (m_source_address.empty() || m_target_address.empty()) {
        return;
    }

    snapshotIt = snapshot.find(source_tmp);

    if (snapshotIt != snapshot.end()) {
        source = std::make_shared<const CScript>(snapshotIt->first);
        if (const auto decoded_sig = DecodeBase64(m_claim.signatureString)) {
            m_signature = *decoded_sig;
            m_signature_string = EncodeBase64(m_signature);
        } else {
            return;
        }
        m_peercoin_balances.push_back(std::make_shared<CAmount>(snapshotIt->second));
        m_compatible = true;
    } else {
        incompatibleSnapshotIt = snapshot_incompatible.find(source_tmp);
        if (incompatibleSnapshotIt == snapshot_incompatible.end()) {
            return;
        }
        source = std::make_shared<const CScript>(incompatibleSnapshotIt->first);
        if (!DecodeHexTx(m_dummy_tx, m_claim.signatureString)) {
            return;
        }
        for (const auto& [outpoint, coin] : incompatibleSnapshotIt->second) {
            m_peercoin_balances.push_back(std::make_shared<CAmount>(coin.out.nValue));
        }
        m_signature_string = m_claim.signatureString;
    }

    m_eligible = GetEligible();
    assert(MoneyRange(m_eligible) && m_eligible <= MAX_CLAIM_REWARD);
    GENESIS_OUTPUTS_AMOUNT = static_cast<double>(Params().GetConsensus().genesisValue)
                             / static_cast<double>(Params().GetConsensus().genesisOutputs);
    MAX_POSSIBLE_OUTPUTS = std::min(20u,
        static_cast<unsigned int>(std::ceil(static_cast<double>(MAX_CLAIM_REWARD) / GENESIS_OUTPUTS_AMOUNT))) + 4;
    MAX_OUTPUTS = GetMaxOutputs();
    m_init = true;
}

bool Claim::SnapshotIsValid() const
{
    return hashSnapshot == Params().GetConsensus().hashPeercoinSnapshot
        && !SnapshotManager::Peercoin().GetScriptPubKeys().empty()
        && !SnapshotManager::Peercoin().GetIncompatibleScriptPubKeys().empty()
        && !snapshot.empty()
        && !snapshot_incompatible.empty();
}

CScript Claim::GetScriptFromAddress(const std::string& address)
{
    CScript script;
    CTxDestination dest = DecodeDestination(address);
    if (IsValidDestination(dest)) {
        script = GetScriptForDestination(dest);
    }
    return script;
}

std::string Claim::GetAddressFromScript(const CScript& script)
{
    std::string address;
    CTxDestination dest;
    if (ExtractDestination(script, dest) && IsValidDestination(dest)) {
        address = EncodeDestination(dest);
    }
    return address;
}

std::string Claim::GetSourceAddress() const {
    return m_claim.sourceAddress;
}

std::string Claim::GetTargetAddress() const {
    return m_claim.targetAddress;
}

std::string Claim::GetSignatureString() const {
    return m_claim.signatureString;
}

std::string Claim::GetComputedSourceAddress() const {
    return m_source_address;
}

std::string Claim::GetComputedTargetAddress() const {
    return m_target_address;
}

std::string Claim::GetComputedSignatureString() const {
    return m_signature_string;
}

CScript Claim::GetSource() const {
    return source ? *source : CScript();
}

std::vector<unsigned char> Claim::GetSignature() const {
    return m_signature;
}

CScript Claim::GetTarget() const {
    return m_target;
}

CAmount Claim::GetPeercoinBalance() const {
    const CAmount balance = std::accumulate(m_peercoin_balances.begin(), m_peercoin_balances.end(), 0,
        [](CAmount sum, const auto& bal) {
            return sum + *bal;
        });
    return MoneyRange(balance) ? balance : 0;
}

CAmount Claim::GetEligible() const
{
    if (m_eligible > 0) {
        return m_eligible;
    }
    CAmount eligible = 0;
    if (!SnapshotManager::Peercoin().CalculateEligible(GetPeercoinBalance(), eligible)
        || !MoneyRange(eligible))
    {
        return 0;
    }
    return eligible;
}

CAmount Claim::GetMaxOutputs() const
{
    const unsigned int maxOutputs = static_cast<unsigned int>(
        std::ceil(static_cast<double>(GetEligible()) / GENESIS_OUTPUTS_AMOUNT)) + 4;
    return std::min(maxOutputs, MAX_POSSIBLE_OUTPUTS);
}

bool Claim::GetReceived(const wallet::CWallet* pwallet, CAmount& received) const
{
    return SnapshotManager::Peercoin().CalculateReceived(pwallet, m_target, received);
}

bool Claim::GetTotalReceived(const CBlockIndex* pindex, CAmount& received, unsigned int& outputs) const
{
    if (GetEligible() == 0) return false;
    while (pindex && outputs < m_outs.size()) {
        const auto& it = m_outs.find(pindex->GetBlockHash());
        if (it != m_outs.end()) {
            outputs++;
            received += it->second;
        }
        pindex = pindex->pprev;
    }
    if (received > m_eligible) {
        return false;
    }
    if (outputs > MAX_POSSIBLE_OUTPUTS /*|| outputs > MAX_OUTPUTS || outputs > GetMaxOutputs()*/) {
        return false;
    }
    return true;
}

void Claim::SetNull()
{
    // m_claim = CClaim();
    source.reset();
    m_signature.clear();
    m_dummy_tx = CMutableTransaction();
    m_target.clear();
    m_peercoin_balances.clear();
    m_seen = false;
    m_outs.clear();

    m_source_address.clear();
    m_signature_string.clear();
    m_target_address.clear();
    m_eligible = 0;

    snapshotIt = snapshot.end();
    incompatibleSnapshotIt = snapshot_incompatible.end();

    nTime = 0; // GetTime();
    m_init = false;
    m_compatible = false;

    GENESIS_OUTPUTS_AMOUNT = 0;
    MAX_POSSIBLE_OUTPUTS = 0;
    MAX_OUTPUTS = 0;
}

bool Claim::VerifyDummyTx() const
{
    if (m_dummy_tx.vin.size() != 1) {
        LogPrintf("VerifyDummyTx failed: input size must be 1\n");
        return false;
    }
    if (m_dummy_tx.vout.size() != 1) {
        LogPrintf("VerifyDummyTx failed: output size must be 1\n");
        return false;
    }
    if (!MoneyRange(m_dummy_tx.vout[0].nValue)) {
        LogPrintf("VerifyDummyTx failed: tx target output amount out of range\n");
        return false;
    }
    const CScript& scriptPubKey = m_dummy_tx.vout[0].scriptPubKey;
    const std::string& targetAddress = GetAddressFromScript(scriptPubKey);
    if (m_claim.targetAddress != targetAddress || targetAddress != m_target_address || scriptPubKey != m_target) {
        LogPrintf("VerifyDummyTx failed: target address must match tx target address\n");
        return false;
    }
    Coin coin;
    CTxOut prevout;
    constexpr unsigned int nIn = 0;
    bool found = false;
    for (const auto& [script, utxo] : snapshot_incompatible)
    {
        if (script != *source)
            continue;
        for (const auto& [outpoint, coin_t] : utxo)
        {
            if (outpoint.hash == m_dummy_tx.vin[nIn].prevout.hash &&
                outpoint.n == m_dummy_tx.vin[nIn].prevout.n)
            {
                coin = coin_t;
                prevout = coin.out;
                found = true;
                break;
            }
        }
        if (found)
            break;
    }
    if (!found || prevout.IsNull()) {
        LogPrintf("VerifyDummyTx failed: unable to find previous output\n");
        return false;
    }
    if (coin.out.nValue != m_dummy_tx.vout[0].nValue) {
        LogPrintf("VerifyDummyTx failed: input value must match output value\n");
        return false;
    }
    PrecomputedTransactionData txdata;
    txdata.Init(m_dummy_tx, std::vector<CTxOut>{prevout} /*, true*/);
    const MutableTransactionSignatureChecker checker(&m_dummy_tx, nIn, prevout.nValue, txdata, MissingDataBehavior::FAIL/* ASSERT_FAIL */);
    ScriptError serror;
    if (!VerifyScript(m_dummy_tx.vin[nIn].scriptSig, prevout.scriptPubKey, &(m_dummy_tx.vin[nIn].scriptWitness), STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror)) {
        LogPrintf("VerifyDummyTx failed: %s\n", ScriptErrorString(serror));
        return false;
    }
    return true;
}

bool Claim::IsValid() const
{
    if (!m_init) {
        LogPrintf("[claim] error: not initialized\n");
        return false;
    }
    try {
        if (!SnapshotIsValid()) {
            LogPrintf("[claim] error: snapshot hash does not match consensus hash\n");
            return false;
        }
        const unsigned int size = GetBaseSize();
        if (m_compatible && size != CLAIM_SIZE()) {
            LogPrintf("[claim] error: size mismatch: current=%s target=%s\n", size, CLAIM_SIZE());
            return false;
        }
        if (!m_compatible && size > CLAIM_SIZE()) {
            LogPrintf("[claim] error: size mismatch: current=%s target=%s\n", size, CLAIM_SIZE());
            return false;
        }
        if (!m_claim.sourceAddress.size() || !m_claim.targetAddress.size() || !m_claim.signatureString.size()) {
            LogPrintf("[claim] error: called on an empty string source=%s target=%s signature=%s\n",
               m_claim.sourceAddress, m_claim.targetAddress, m_claim.signatureString);
            return false;
        }
        if (IsSourceTarget() || IsSourceTargetAddress()) {
            LogPrintf("[claim] error: input matches output address\n");
            return false;
        }
        if (m_compatible && snapshotIt == snapshot.end()) {
            LogPrintf("[claim] error: source script not found in snapshot\n");
            return false;
        }
        if (!m_compatible && incompatibleSnapshotIt == snapshot_incompatible.end()) {
            LogPrintf("[claim] error: source script not found in snapshot\n");
            return false;
        }
        if (GetScriptFromAddress(m_source_address).empty()
            || source == nullptr
            || GetAddressFromScript(*source).empty())
        {
            LogPrintf("[claim] error: failed to extract destination from source script\n");
            return false;
        }
        if (GetScriptFromAddress(m_target_address).empty()
            || GetAddressFromScript(m_target).empty())
        {
            LogPrintf("[claim] error: failed to extract destination from target script\n");
            return false;
        }
        if (m_compatible) {
            MessageVerificationResult res = MessageVerify(
                m_source_address,
                m_signature_string,
                m_target_address,
                PEERCOIN_MESSAGE_MAGIC
            );
            if (res != MessageVerificationResult::OK) {
                LogPrintf("[claim] error: signature verification failed (%d)\n", static_cast<int>(res));
                return false;
            }
        } else {
            if (!VerifyDummyTx()) {
                LogPrintf("[claim] error: tx verification failed\n");
                return false;
            }
        }
        if (!MoneyRange(GetPeercoinBalance())) {
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

bool Claim::IsSourceTarget() const
{
    return GetSource() == GetTarget();
}

bool Claim::IsSourceTargetAddress() const
{
    return m_source_address == m_target_address;
}

bool Claim::IsUnique() const
{
    // patchcoin ensure consistency with claimset
    return !IsSourceTargetAddress() && !IsSourceTarget() && IsUniqueSource() && IsUniqueTarget();
}

bool Claim::IsUniqueSource() const
{
    if (!source || source->empty()) {
        return false;
    }
    if (g_claims.count(*source) != 0) {
        return false;
    }
    return std::none_of(g_claims.begin(), g_claims.end(), [this](const auto& entry) {
        const Claim& claim = entry.second;
        return claim.GetTarget() == *source;
    });
}

bool Claim::IsUniqueTarget() const
{
    if (m_target.empty()) {
        return false;
    }
    if (g_claims.count(m_target) != 0) {
        return false;
    }
    return std::none_of(g_claims.begin(), g_claims.end(), [this](const auto& entry) {
        const Claim& claim = entry.second;
        return claim.GetTarget() == m_target;
    });
}

bool Claim::Insert() const EXCLUSIVE_LOCKS_REQUIRED(g_claims_mutex)
{
    if (!(IsValid() && IsUnique()))
        return false;
    const auto [_, inserted]{g_claims.try_emplace(GetSource(), *this)};
    return inserted && !IsUniqueSource();
}

uint256 Claim::GetHash() const
{
    return SerializeHash(*this);
}

unsigned int Claim::GetBaseSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}
