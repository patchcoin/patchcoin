#ifndef PATCHCOIN_PRIMITIVES_CLAIM_H
#define PATCHCOIN_PRIMITIVES_CLAIM_H

#include <script/script.h>

#include <primitives/transaction.h>

class CClaim
{
public:
    int32_t nVersion = 1;
    CScript source;
    CScript target;
    std::vector<unsigned char> signature;
    CTransactionRef dummyTx;

    CClaim()
    {
        SetNull();
    }

    CClaim(const CScript& sourceIn, const CScript& targetIn, const std::vector<unsigned char>& signatureIn, const CTransactionRef& dummyTxIn);

    void SetNull()
    {
        nVersion = 1;
        source.clear();
        target.clear();
        signature.clear();
        dummyTx = MakeTransactionRef(std::move(CMutableTransaction()));
    }

    SERIALIZE_METHODS(CClaim, obj)
    {
        READWRITE(obj.nVersion, obj.source, obj.target, obj.signature, obj.dummyTx);
    }

    friend bool operator==(const CClaim& a, const CClaim& b)
    {
        return a.source == b.source;
    }

    friend bool operator!=(const CClaim& a, const CClaim& b)
    {
        return !(a == b);
    }
};

#endif // PATCHCOIN_PRIMITIVES_CLAIM_H
