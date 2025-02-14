#include <primitives/claim.h>

CClaim::CClaim(const CScript& sourceIn, const CScript& targetIn, const std::vector<unsigned char>& signatureIn, const CTransactionRef& dummyTxIn)
{
    source = sourceIn;
    target = targetIn;
    signature = signatureIn;
    dummyTx = dummyTxIn;
}
