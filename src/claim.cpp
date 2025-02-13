#include <chain.h>
#include <claim.h>
#include <chainparams.h>
#include <hash.h>
#include <key_io.h>
#include <snapshotmanager.h>
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

Claim::Claim(const std::string& source_address, const std::string& signature_str, const std::string& target_address)
    : m_claim(source_address, target_address, signature_str)
    , m_source_address(source_address)
    , m_signature_string(signature_str)
    , m_target_address(target_address)
{
    Init();
}

Claim::Claim(const CClaim& claim)
    : m_claim(claim)
    , m_source_address(claim.sourceAddress)
    , m_signature_string(claim.signatureString)
    , m_target_address(claim.targetAddress)
{
    Init();
}

Claim::~Claim() = default;

void Claim::Init()
{
    if (GetBaseSize() != 6)
        return;

    const CScript& source_script = GetScriptFromAddress(m_source_address);
    if (source_script.empty())
        return;

    if (const auto decodedSig = DecodeBase64(m_signature_string)) {
        m_signature = *decodedSig;
    } else {
        return;
    }
    if (GetBaseSize() != (6 + CPubKey::COMPACT_SIGNATURE_SIZE))
        return;

    m_target = GetScriptFromAddress(m_target_address);
    if (m_target.empty())
        return;

    if (GetBaseSize() != CLAIM_SIZE)
        return;

    if (SnapshotIsValid()) {
        snapshotIt = snapshot.find(source_script);
        if (snapshotIt != snapshot.end()) {
            snapshotPos = std::distance(snapshot.begin(), snapshotIt);
            source = std::make_shared<CScript>(snapshotIt->first);
            peercoinBalance = std::make_shared<CAmount>(snapshotIt->second);
            m_eligible = GetEligible();
            GENESIS_OUTPUTS_AMOUNT = static_cast<double>(Params().GetConsensus().genesisValue)
                                   / static_cast<double>(Params().GetConsensus().genesisOutputs);
            MAX_POSSIBLE_OUTPUTS = std::min(20u,
                static_cast<unsigned int>(std::ceil(
                    static_cast<double>(MAX_CLAIM_REWARD) / GENESIS_OUTPUTS_AMOUNT))) + 4;
            MAX_OUTPUTS = GetMaxOutputs();
        }
    }
}

bool Claim::SnapshotIsValid() const
{
    return hashSnapshot == Params().GetConsensus().hashPeercoinSnapshot
        && !SnapshotManager::Peercoin().GetScriptPubKeys().empty();
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

std::string Claim::LocateAddress(const uint32_t& pos)
{
    std::string address;
    if (Params().GetConsensus().hashPeercoinSnapshot == hashSnapshot
        && SnapshotManager::Peercoin().GetHashScripts() == hashSnapshot
        && pos < static_cast<uint32_t>(snapshot.size()))
    {
        const auto& it = std::next(snapshot.begin(), pos);
        if (it != snapshot.end()) {
            address = GetAddressFromScript(it->first);
        }
    }
    return address;
}

std::string Claim::GetSourceAddress() const {
    return m_source_address;
}

std::string Claim::GetSignatureString() const {
    return m_signature_string;
}

std::string Claim::GetTargetAddress() const {
    return m_target_address;
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

CAmount Claim::GetPeercoinBalance() const
{
    return (snapshotIt != SnapshotManager::Peercoin().GetScriptPubKeys().end() && peercoinBalance) ? *peercoinBalance : 0;
}

CAmount Claim::GetEligible() const
{
    if (m_eligible > 0) {
        return m_eligible;
    }
    CAmount eligibleAmount = 0;
    if (!SnapshotManager::Peercoin().CalculateEligible(GetPeercoinBalance(), eligibleAmount)
        || !MoneyRange(eligibleAmount))
    {
        return 0;
    }
    return eligibleAmount;
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
    source.reset();
    m_signature.clear();
    m_target.clear();
    peercoinBalance.reset();
    m_seen = false;
    m_outs.clear();

    m_source_address.clear();
    m_signature_string.clear();
    m_target_address.clear();
    m_eligible = 0;
}

bool Claim::IsAnyNull() const
{
    return m_source_address.empty()
        || m_signature_string.empty()
        || m_target_address.empty()
        || !source
        || m_signature.empty()
        || m_target.empty()
        || !peercoinBalance
        || GetSource().empty()
        || GetSignature().empty()
        || GetTarget().empty();
}

bool Claim::IsValid() const
{
    try {
        if (GetBaseSize() != CLAIM_SIZE) { // patchcoin todo
            LogPrintf("[claim] error: size mismatch: current=%s target=%s\n", GetBaseSize(), CLAIM_SIZE);
            return false;
        }
        if (IsAnyNull()) {
            LogPrintf("[claim] error: called on an empty string source=%s signature=%s target=%s\n",
               m_claim.sourceAddress, m_claim.signatureString, m_claim.targetAddress);
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
        if (!MoneyRange(snapshotIt->second) || !MoneyRange(GetPeercoinBalance())) {
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
