#include <index/claimindex.h>
#include <logging.h>
#include <util/system.h>
#include <dbwrapper.h>
#include <chainparams.h>
#include <claim.h>

static constexpr uint8_t DB_CLAIMINDEX{'c'};

std::unique_ptr<ClaimIndex> g_claimindex;

class ClaimIndex::DB : public BaseIndex::DB
{
public:
    explicit DB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    bool WriteClaim(const CScript& source, const Claim& claim);

    bool ReadClaim(const CScript& source, Claim& claim);

    bool ReadAllClaims(std::vector<Claim>& claims);
};

ClaimIndex::DB::DB(size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex::DB(gArgs.GetDataDirNet() / "indexes" / "claimindex",
                    n_cache_size, f_memory, f_wipe)
{
}

bool ClaimIndex::DB::WriteClaim(const CScript& source, const Claim& claim)
{
    return Write(std::make_pair(DB_CLAIMINDEX, source), claim);
}

bool ClaimIndex::DB::ReadClaim(const CScript& source, Claim& claim)
{
    if (Read(std::make_pair(DB_CLAIMINDEX, source), claim)) {
        return true;
    }
    return false;
}

bool ClaimIndex::DB::ReadAllClaims(std::vector<Claim>& claims)
{
    std::unique_ptr<CDBIterator> it(NewIterator());
    claims.clear();

    for (it->Seek(DB_CLAIMINDEX); it->Valid(); it->Next()) {
        std::pair<uint8_t, CScript> key;
        Claim claim;

        if (it->GetKey(key) && key.first == DB_CLAIMINDEX) {
            if (it->GetValue(claim)) {
                claims.push_back(std::move(claim));
            } else {
                LogPrintf("ClaimIndex::DB::ReadAllClaims: Failed to read claim value.\n");
                return false;
            }
        } else {
            break;
        }
    }
    return true;
}

ClaimIndex::ClaimIndex(std::unique_ptr<interfaces::Chain> chain,
                       size_t n_cache_size,
                       bool f_memory,
                       bool f_wipe)
    : BaseIndex(std::move(chain), "claimindex")
    , m_db(std::make_unique<ClaimIndex::DB>(n_cache_size, f_memory, f_wipe))
{
}

ClaimIndex::~ClaimIndex() = default;


BaseIndex::DB& ClaimIndex::GetDB() const
{
    return *m_db;
}

bool ClaimIndex::AddClaim(const Claim& claim)
{
    return m_db->WriteClaim(claim.GetSource(), claim);
}

bool ClaimIndex::ClaimExists(const CScript& source) const
{
    Claim claim;
    return m_db->ReadClaim(source, claim);
}

bool ClaimIndex::FindClaim(const CScript& source, Claim& claim) const
{
    return m_db->ReadClaim(source, claim);
}

bool ClaimIndex::GetAllClaims(std::vector<Claim>& claims) const
{
    return m_db->ReadAllClaims(claims);
}
