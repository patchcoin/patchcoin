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

Claim::Claim(const std::string& source_address, const std::string& signature, const std::string& target_address)
    : sourceAddress(source_address), signatureString(signature), targetAddress(target_address)
{
    Init();
}

Claim::~Claim() = default;

void Claim::Init()
{
    if (GetBaseSize() != 6)
        return;
    const CScript& source_script = GetScriptFromAddress(sourceAddress);
    if (source_script.empty())
        return;

    if (const auto decoded_signature = DecodeBase64(signatureString)) {
        signature = *decoded_signature;
    } else {
        return;
    }
    if (GetBaseSize() != (6 + CPubKey::COMPACT_SIGNATURE_SIZE))
        return;

    target = GetScriptFromAddress(targetAddress);
    if (target.empty())
        return;

    if (GetBaseSize() != CLAIM_SIZE)
        return;

    if (SnapshotIsValid()) {
        snapshotIt = snapshot.find(source_script);
        if (snapshotIt != snapshot.end()) {
            snapshotPos = std::distance(snapshot.begin(), snapshotIt);
            source = std::make_shared<CScript>(snapshotIt->first);
            peercoinBalance = std::make_shared<CAmount>(snapshotIt->second);
            eligible = GetEligible();
            GENESIS_OUTPUTS_AMOUNT = static_cast<double>(Params().GetConsensus().genesisValue)
                                   / static_cast<double>(Params().GetConsensus().genesisOutputs);
            MAX_POSSIBLE_OUTPUTS = std::min(20u,
                static_cast<unsigned int>(std::ceil(
                    static_cast<double>(MAX_CLAIM_REWARD) / GENESIS_OUTPUTS_AMOUNT))) + 4;
            MAX_OUTPUTS = GetMaxOutputs();
        }
    }
}

bool Claim::SnapshotIsValid() const {
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

std::string Claim::GetSourceAddress() const { return sourceAddress; }
std::string Claim::GetSignatureString() const { return signatureString; }
std::string Claim::GetTargetAddress() const { return targetAddress; }

CScript Claim::GetSource() const { return source ? *source : CScript(); } // patchcoin todo: recheck
std::vector<unsigned char> Claim::GetSignature() const { return signature; }
CScript Claim::GetTarget() const { return target; }

CAmount Claim::GetPeercoinBalance() const {
    return snapshotIt != SnapshotManager::Peercoin().GetScriptPubKeys().end() ? *peercoinBalance : 0;
}

CAmount Claim::GetEligible() const
{
    if (eligible > 0) return eligible;
    CAmount eligibleAmount = 0;
    if (!SnapshotManager::Peercoin().CalculateEligible(GetPeercoinBalance(), eligibleAmount)
        || !MoneyRange(eligibleAmount)) {
        return 0;
    }
    return eligibleAmount;
}

CAmount Claim::GetMaxOutputs() const
{
    // patchcoin todo not accurate and merely serves as a static fence-in
    // patchcoin todo add look-ahead, similar to
    const unsigned int maxOutputs = static_cast<unsigned int>(
        std::ceil(static_cast<double>(GetEligible()) / GENESIS_OUTPUTS_AMOUNT)) + 4;
    return std::min(maxOutputs, MAX_POSSIBLE_OUTPUTS);
}

bool Claim::GetReceived(const wallet::CWallet* pwallet, CAmount& received) const
{
    return SnapshotManager::Peercoin().CalculateReceived(pwallet, target, received);
}

bool Claim::GetTotalReceived(const CBlockIndex* pindex, CAmount& received, unsigned int& outputs) const
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

void Claim::SetNull()
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

bool Claim::IsAnyNull() const
{
    return sourceAddress.empty() || signatureString.empty() || targetAddress.empty()
        || !source || signature.empty() || target.empty() || !peercoinBalance
        || GetSource().empty() || GetSignature().empty() || GetTarget().empty();
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
        if (sourceAddress.empty() || GetScriptFromAddress(sourceAddress).empty() || source == nullptr
            || GetAddressFromScript(*source).empty()) {
            LogPrintf("[claim] error: failed to extract destination from source script\n");
            return false;
        }
        if (targetAddress.empty() || GetScriptFromAddress(targetAddress).empty()
            || GetAddressFromScript(target).empty()) {
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

bool Claim::IsSourceTarget() const
{
    return GetSource() == GetTarget();
}

bool Claim::IsSourceTargetAddress() const
{
    return GetSourceAddress() == GetTargetAddress();
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
    if (target.empty()) {
        return false;
    }
    if (g_claims.count(target) != 0) {
        return false;
    }
    return std::none_of(g_claims.begin(), g_claims.end(), [this](const auto& entry) {
        const Claim& claim = entry.second;
        return claim.GetTarget() == target;
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
