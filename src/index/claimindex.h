#ifndef PATCHCOIN_INDEX_CLAIMINDEX_H
#define PATCHCOIN_INDEX_CLAIMINDEX_H

#include <index/base.h>
#include <claim.h>
#include <string>
#include <memory>

static constexpr bool DEFAULT_CLAIMINDEX{true};

class ClaimIndex final : public BaseIndex
{
protected:
    class DB;

private:
    const std::unique_ptr<DB> m_db;

    bool AllowPrune() const override { return false; }

protected:
    BaseIndex::DB& GetDB() const override;

public:
    explicit ClaimIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory = false, bool f_wipe = false);
    ~ClaimIndex() override;

    bool AddClaim(const CClaim& claim);

    bool ClaimExists(const CScript& source) const;

    bool FindClaim(const CScript& source, CClaim& claim) const;

    bool GetAllClaims(std::vector<CClaim>& claims) const;
};

extern std::unique_ptr<ClaimIndex> g_claimindex;

#endif // PATCHCOIN_INDEX_CLAIMINDEX_H
