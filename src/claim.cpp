#include <bitcoinaddresslookup.h>
#include <claim.h>
#include <chain.h>
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
{
    const bool is_ptc = IsValidDestinationString(source_address);
    const bool is_btc = IsValidDestinationString(source_address, *Params().BitcoinMain());

    if (!(is_ptc || is_btc))
        return;

    const CTxDestination destination = is_ptc ? DecodeDestination(source_address) : DecodeDestination_BTC(source_address);

    if (!IsValidDestination(destination)) {
        return;
    }

    const CScript source = is_ptc ? GetScriptFromAddress(source_address) : GetScriptFromBtcAddress(source_address);
    const CScript target = GetScriptFromAddress(target_address);
    std::vector<unsigned char> vSig;
    CMutableTransaction mtx;

    if (is_btc || std::get_if<PKHash>(&destination) != nullptr) {
        vSig = DecodeBase64(signature_string).value_or(std::vector<unsigned char>{});
    } else {
        if (!DecodeHexTx(mtx, signature_string)) {
            return;
        }
    }

    m_claim = CClaim(source, target, vSig, MakeTransactionRef(std::move(mtx)));

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
        LogPrintf("[claim] Init: snapshot is invalid or empty.\n");
        return;
    }

    m_source_address = GetAddressFromScript(m_claim.source);
    m_bitcoin_source_address = GetBtcAddressFromScript(m_claim.source);
    m_target_address = GetAddressFromScript(m_claim.target);

    if (m_source_address.empty() || m_bitcoin_source_address.empty() || m_target_address.empty()) {
        LogPrintf("[claim] Init: failed to decode source or target address\n");
        return;
    }

    snapshotIt = snapshot.find(m_claim.source);

    if (snapshotIt != snapshot.end()) {
        source = std::make_shared<const CScript>(snapshotIt->first);
        m_peercoin_balances.push_back(std::make_shared<CAmount>(snapshotIt->second));
        m_signature_string = EncodeBase64(m_claim.signature);
        m_compatible = true;
    } else {
        incompatibleSnapshotIt = snapshot_incompatible.find(m_claim.source);
        if (incompatibleSnapshotIt != snapshot_incompatible.end()) {
            source = std::make_shared<const CScript>(incompatibleSnapshotIt->first);
            for (const auto& [outpoint, coin] : incompatibleSnapshotIt->second) {
                m_peercoin_balances.push_back(std::make_shared<CAmount>(coin.out.nValue));
            }
            m_signature_string = EncodeHexTx(*m_claim.dummyTx);
        } else {
            m_is_btc = true;
            m_signature_string = EncodeBase64(m_claim.signature);
        }
        m_compatible = false;
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
    if (hashSnapshot != Params().GetConsensus().hashPeercoinSnapshot) {
        LogPrintf("[claim] SnapshotIsValid: mismatch with consensus hash\n");
        return false;
    }
    if (SnapshotManager::Peercoin().GetScriptPubKeys().empty()) {
        LogPrintf("[claim] SnapshotIsValid: empty scriptPubKeys in SnapshotManager\n");
        return false;
    }
    if (SnapshotManager::Peercoin().GetIncompatibleScriptPubKeys().empty()) {
        LogPrintf("[claim] SnapshotIsValid: empty incompatibleScriptPubKeys in SnapshotManager\n");
        return false;
    }
    if (snapshot.empty()) {
        LogPrintf("[claim] SnapshotIsValid: local snapshot map is empty\n");
        return false;
    }
    if (snapshot_incompatible.empty()) {
        LogPrintf("[claim] SnapshotIsValid: local snapshot_incompatible map is empty\n");
        return false;
    }

    return true;
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

CScript Claim::GetScriptFromBtcAddress(const std::string& address)
{
    CScript script;
    CTxDestination dest = DecodeDestination_BTC(address);
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

std::string Claim::GetBtcAddressFromScript(const CScript& script)
{
    std::string address;
    CTxDestination dest;
    if (ExtractDestination(script, dest) && IsValidDestination(dest)) {
        address = EncodeDestination_BTC(dest);
    }
    return address;
}

std::string Claim::GetSourceAddress() const {
    return m_source_address;
}

std::string Claim::GetBtcSourceAddress() const {
    return m_bitcoin_source_address;
}

std::string Claim::GetTargetAddress() const {
    return m_target_address;
}

std::string Claim::GetSignatureString() const {
    return m_signature_string;
}

CClaim Claim::GetClaim() const
{
    return m_claim;
}

CScript Claim::GetSource() const {
    if (m_is_btc) {
        return m_claim.source;
    }
    return *source;
}

std::vector<unsigned char> Claim::GetSignature() const {
    return m_claim.signature;
}

CScript Claim::GetTarget() const {
    return m_claim.target;
}

uint16_t Claim::GetSnapshotPosition() const
{
    if (m_compatible) {
        return static_cast<uint16_t>(std::distance(snapshot.begin(), snapshotIt));
    }
    return static_cast<uint16_t>(std::distance(snapshot_incompatible.begin(), incompatibleSnapshotIt));
}

CAmount Claim::GetPeercoinBalance() const {
    CAmount balance = 0;
    for (const auto& bal : m_peercoin_balances) {
        balance += *bal;
    }
    return MoneyRange(balance) ? balance : 0;
}

CAmount Claim::GetBitcoinBalance() const {
    CAmount balance = 0;
    const auto lookup = BitcoinAddressLookup::Get();
    if (lookup && lookup->isEnabled()) {
        balance = lookup->getBalance(m_bitcoin_source_address);
    }
    return MoneyRange(balance) ? balance : 0;
}

CAmount Claim::GetEligible() const
{
    if (m_eligible > 0) {
        return m_eligible;
    }

    CAmount eligible = 0;
    bool ok;
    if (m_is_btc) {
        ok = SnapshotManager::CalculateEligibleBTC(GetBitcoinBalance(), eligible);
    } else {
        ok = SnapshotManager::Peercoin().CalculateEligible(GetPeercoinBalance(), eligible);
    }

    if (!ok || !MoneyRange(eligible) || eligible > MAX_CLAIM_REWARD) {
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
    return SnapshotManager::Peercoin().CalculateReceived(pwallet, GetTarget(), received);
}

bool Claim::GetTotalReceived(const CBlockIndex* pindex, CAmount& received, unsigned int& outputs) const
{
    if (!m_is_btc && GetEligible() <= 0) {
        return false;
    }
    const CBlockIndex* pindexFrom = pindex;
    while (pindexFrom) {
        const auto& it = pindexFrom->nClaims.find(GetSource());
        if (it != pindexFrom->nClaims.end()) {
            received += it->second;
            outputs++;
        }
        pindexFrom = pindexFrom->pprev;
    }

    if (!m_is_btc && received > m_eligible) {
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
    m_source_address.clear();
    m_signature_string.clear();
    m_target_address.clear();
    m_peercoin_balances.clear();
    m_eligible = 0;

    snapshotIt = snapshot.end();
    incompatibleSnapshotIt = snapshot_incompatible.end();
    source.reset();

    nTime = 0; // GetTime();
    m_init = false;
    m_compatible = false;
    m_is_btc = false;
    m_electrum_result = false;
    m_seen = false;

    GENESIS_OUTPUTS_AMOUNT = 0;
    MAX_POSSIBLE_OUTPUTS = 0;
    MAX_OUTPUTS = 0;
}

Claim::ClaimVerificationResult Claim::VerifyDummyTx(ScriptError* serror) const
{
    if (m_claim.dummyTx->vin.size() != 1) {
        LogPrintf("VerifyDummyTx failed: input size must be one\n");
        return ClaimVerificationResult::ERR_DUMMY_INPUT_SIZE;
    }
    if (m_claim.dummyTx->vout.size() != 1) {
        LogPrintf("VerifyDummyTx failed: output size must be one\n");
        return ClaimVerificationResult::ERR_DUMMY_OUTPUT_SIZE;
    }
    if (!MoneyRange(m_claim.dummyTx->vout[0].nValue)) {
        LogPrintf("VerifyDummyTx failed: tx target output amount out of range\n");
        return ClaimVerificationResult::ERR_DUMMY_VALUE_RANGE;
    }
    const CScript& scriptPubKey = m_claim.dummyTx->vout[0].scriptPubKey;
    const std::string& targetAddress = GetAddressFromScript(scriptPubKey);
    if (targetAddress != m_target_address || scriptPubKey != GetTarget()) {
        LogPrintf("VerifyDummyTx failed: target address must match tx target address\n");
        return ClaimVerificationResult::ERR_DUMMY_ADDRESS_MISMATCH;
    }

    Coin coin;
    CTxOut prevout;
    constexpr unsigned int nIn = 0;
    bool found = false;

    for (const auto& [script, utxo] : snapshot_incompatible)
    {
        if (script != GetSource())
            continue;
        for (const auto& [outpoint, coin_t] : utxo)
        {
            if (outpoint.hash == m_claim.dummyTx->vin[nIn].prevout.hash &&
                outpoint.n == m_claim.dummyTx->vin[nIn].prevout.n)
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
        return ClaimVerificationResult::ERR_DUMMY_PREVOUT_NOT_FOUND;
    }

    if (coin.out.nValue != m_claim.dummyTx->vout[0].nValue) {
        LogPrintf("VerifyDummyTx failed: input value must match output value\n");
        return ClaimVerificationResult::ERR_DUMMY_INPUT_VALUE_MISMATCH;
    }

    PrecomputedTransactionData txdata;
    txdata.Init(*m_claim.dummyTx, std::vector<CTxOut>{prevout}, true);
    const TransactionSignatureChecker checker(&(*m_claim.dummyTx), nIn, prevout.nValue, txdata, MissingDataBehavior::FAIL/* ASSERT_FAIL */);
    if (!VerifyScript(m_claim.dummyTx->vin[nIn].scriptSig, prevout.scriptPubKey, &(m_claim.dummyTx->vin[nIn].scriptWitness), STANDARD_SCRIPT_VERIFY_FLAGS, checker, serror)) {
        LogPrintf("VerifyDummyTx failed: %s\n", ScriptErrorString(*serror));
        return ClaimVerificationResult::ERR_DUMMY_SIG_FAIL;
    }

    return ClaimVerificationResult::OK;
}

Claim::ClaimVerificationResult Claim::IsValid(ScriptError *serror) const
{
    if (!m_init) {
        LogPrintf("[claim] error: not initialized\n");
        return ClaimVerificationResult::ERR_NOT_INITIALIZED;
    }

    try {
        if (!SnapshotIsValid()) {
            LogPrintf("[claim] error: snapshot hash does not match consensus hash\n");
            return ClaimVerificationResult::ERR_SNAPSHOT_MISMATCH;
        }
        if (!m_claim.signature.empty() && m_claim.dummyTx && !m_claim.dummyTx->IsNull()) {
            LogPrintf("[claim] error: signature and tx cannot both be set\n");
            return ClaimVerificationResult::ERR_NOT_INITIALIZED;
        }
        const unsigned int size = GetBaseSize();
        if (m_compatible && size != CLAIM_SIZE()) {
            LogPrintf("[claim] error: size mismatch: current=%u target=%zu\n", size, CLAIM_SIZE());
            return ClaimVerificationResult::ERR_SIZE_MISMATCH;
        }
        if (!m_compatible && size > CLAIM_SIZE()) {
            LogPrintf("[claim] error: size mismatch: current=%u target=%zu\n", size, CLAIM_SIZE());
            return ClaimVerificationResult::ERR_SIZE_MISMATCH;
        }
        if (!m_source_address.size() || !m_target_address.size() /*|| !m_signature_string.size()*/) {
            LogPrintf("[claim] error: called on empty string fields\n");
            return ClaimVerificationResult::ERR_EMPTY_FIELDS;
        }
        if ((m_compatible || m_is_btc) && !m_signature_string.size()) {
            LogPrintf("[claim] error: called on empty string fields\n");
            return ClaimVerificationResult::ERR_EMPTY_FIELDS;
        }
        if (!m_compatible && !m_is_btc && m_claim.dummyTx && m_claim.dummyTx->IsNull()) {
            LogPrintf("[claim] error: called on empty string fields\n");
            return ClaimVerificationResult::ERR_EMPTY_FIELDS;
        }
        if (m_is_btc && m_claim.dummyTx && !m_claim.dummyTx->IsNull()) {
            LogPrintf("[claim] error: btc no dummy tx accepted\n");
            return ClaimVerificationResult::ERR_EMPTY_FIELDS;
        }
        if (IsSourceTarget() || IsSourceTargetAddress()) {
            LogPrintf("[claim] error: input matches output address\n");
            return ClaimVerificationResult::ERR_SOURCE_EQUALS_TARGET;
        }
        if (m_compatible && !m_is_btc && snapshotIt == snapshot.end()) {
            LogPrintf("[claim] error: source script not found in snapshot\n");
            return ClaimVerificationResult::ERR_SOURCE_SCRIPT_NOT_FOUND;
        }
        if (!m_compatible && !m_is_btc && incompatibleSnapshotIt == snapshot_incompatible.end()) {
            LogPrintf("[claim] error: source script not found in snapshot\n");
            return ClaimVerificationResult::ERR_SOURCE_SCRIPT_NOT_FOUND;
        }
        if (!m_is_btc && (GetScriptFromAddress(m_source_address).empty()
            || source == nullptr
            || GetAddressFromScript(*source).empty()))
        {
            LogPrintf("[claim] error: failed to extract destination from source script\n");
            return ClaimVerificationResult::ERR_DECODE_SCRIPT_FAILURE;
        }
        if (m_is_btc && (GetScriptFromBtcAddress(m_bitcoin_source_address).empty()
            || GetBtcAddressFromScript(GetSource()).empty())) {
            LogPrintf("[claim] error: failed to extract destination from source script\n");
            return ClaimVerificationResult::ERR_DECODE_SCRIPT_FAILURE;
        }
        if (GetScriptFromAddress(m_target_address).empty()) {
            LogPrintf("[claim] error: failed to extract destination from target script\n");
            return ClaimVerificationResult::ERR_DECODE_SCRIPT_FAILURE;
        }
        const auto lookup = BitcoinAddressLookup::Get();
        if (m_is_btc && lookup && lookup->isEnabled() && lookup->getBalance(m_bitcoin_source_address) <= 0) {
            LogPrintf("[claim] error: bitcoin balance out of range or bitcoin script not found\n");
            return ClaimVerificationResult::ERR_BALANCE_OUT_OF_RANGE;
        }
        if (m_compatible && !m_is_btc) {
            MessageVerificationResult res = MessageVerify(
                m_source_address,
                m_signature_string,
                m_target_address,
                PEERCOIN_MESSAGE_MAGIC
            );
            if (res != MessageVerificationResult::OK) {
                LogPrintf("[claim] error: signature verification failed (%d)\n", static_cast<int>(res));
                return ClaimVerificationResult::ERR_SIGNATURE_VERIFICATION_FAILED;
            }
        }
        if (!m_compatible && !m_is_btc) {
            ClaimVerificationResult dummyRes = VerifyDummyTx(serror);
            if (dummyRes != ClaimVerificationResult::OK) {
                LogPrintf("[claim] error: tx verification failed (dummyRes=%d)\n", static_cast<int>(dummyRes));
                return ClaimVerificationResult::ERR_TX_VERIFICATION_FAILED;
            }
        }
        if (m_is_btc && !m_electrum_result && lookup && lookup->isEnabled()) {
            m_electrum_result = false;
            try {
                ElectrumInterface electrum;
                auto [_, success] = electrum.VerifyMessage(m_bitcoin_source_address, m_signature_string, m_target_address);
                m_electrum_result = success;
            } catch (const std::exception& e) {
                m_electrum_result = false;
            }
            if (m_electrum_result == false) {
                LogPrintf("[claim] error: bitcoin signature verification failed\n");
                return ClaimVerificationResult::ERR_SIGNATURE_VERIFICATION_FAILED;
            }
        }
        if (!m_is_btc && !MoneyRange(GetPeercoinBalance())) {
            LogPrintf("[claim] error: peercoin balance out of range\n");
            return ClaimVerificationResult::ERR_BALANCE_OUT_OF_RANGE;
        }
        if (m_is_btc && !MoneyRange(GetBitcoinBalance())) {
            LogPrintf("[claim] error: bitcoin balance out of range\n");
            return ClaimVerificationResult::ERR_BALANCE_OUT_OF_RANGE;
        }
        if (MAX_CLAIM_REWARD < nTotalReceived) {
            LogPrintf("[claim] error: total received above eligible\n");
            return ClaimVerificationResult::ERR_RECEIVED_ABOVE_ELIGIBLE;
        }
        if (m_is_btc && (!lookup || !lookup->isEnabled())) {
            return ClaimVerificationResult::OK;
        }
        const CAmount eligible = GetEligible();
        if (!MoneyRange(eligible)) {
            LogPrintf("[claim] error: eligible balance out of range\n");
            return ClaimVerificationResult::ERR_BALANCE_OUT_OF_RANGE;
        }
        if (eligible < nTotalReceived) {
            LogPrintf("[claim] error: total received above eligible\n");
            return ClaimVerificationResult::ERR_RECEIVED_ABOVE_ELIGIBLE;
        }
        // patchcoin todo: set isChecked and return early? need to make sure we haven't been modified
        return ClaimVerificationResult::OK;
    } catch (const std::exception& e) {
        LogPrintf("[claim] error: unexpected exception in validation: %s\n", e.what());
        return ClaimVerificationResult::ERR_MESSAGE_NOT_SIGNED;
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
    if (!m_is_btc && (!source || source->empty())) {
        return false;
    }
    if (m_is_btc && GetSource().empty()) {
        return false;
    }
    if (g_claims.count(GetSource()) != 0) {
        return false;
    }
    return std::none_of(g_claims.begin(), g_claims.end(), [this](const auto& entry) {
        const Claim& claim = entry.second;
        if (m_is_btc)
            return claim.GetTarget() == GetSource();
        return claim.GetTarget() == *source;
    });
}

bool Claim::IsUniqueTarget() const
{
    if (GetTarget().empty()) {
        return false;
    }
    if (g_claims.count(GetTarget()) != 0) {
        return false;
    }
    return std::none_of(g_claims.begin(), g_claims.end(), [this](const auto& entry) {
        const Claim& claim = entry.second;
        return claim.GetTarget() == GetTarget();
    });
}

bool Claim::Insert() const EXCLUSIVE_LOCKS_REQUIRED(g_claims_mutex)
{
    ScriptError serror;
    const ClaimVerificationResult result = IsValid(&serror);
    if (result != ClaimVerificationResult::OK || !IsUnique()) {
        return false;
    }
    const auto [_, inserted]{g_claims.try_emplace(GetSource(), *this)};
    return inserted && !IsUniqueSource();
}

bool Claim::Insert(const CBlockIndex* pindex) const EXCLUSIVE_LOCKS_REQUIRED(g_claims_mutex)
{
    nTotalReceived = 0;
    unsigned int outputs = 0;
    Claim::GetTotalReceived(pindex, nTotalReceived, outputs);
    return Claim::Insert();
}

uint256 Claim::GetHash() const
{
    return SerializeHash(*this);
}

unsigned int Claim::GetBaseSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string Claim::ToString() const
{
    std::stringstream s;
    s << strprintf("Claim(hash=%s, source_address=%s, target_address=%s, eligible=%lld, peercoin_balance=%lld, compatible=%s, init=%s, snapshotPos=%u)\n",
        GetHash().ToString(),
        m_is_btc ? m_bitcoin_source_address : m_source_address,
        m_target_address,
        m_eligible,
        m_is_btc ? GetBitcoinBalance() : GetPeercoinBalance(),
        m_compatible ? "true" : "false",
        m_init ? "true" : "false",
        m_is_btc ? 0 : GetSnapshotPosition());
    s << "Signature: " << m_signature_string << "\n";
    return s.str();
}
