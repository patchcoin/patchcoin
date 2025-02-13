#ifndef PATCHCOIN_PRIMITIVES_CLAIM_H
#define PATCHCOIN_PRIMITIVES_CLAIM_H

#include <serialize.h>

class CClaim
{
public:
    std::string sourceAddress;
    std::string targetAddress;
    std::string signatureString;

    CClaim()
    {
        SetNull();
    }

    CClaim(const std::string& sourceAddressIn, const std::string& targetAddressIn, const std::string& signatureStringIn);

    void SetNull()
    {
        sourceAddress.clear();
        targetAddress.clear();
        signatureString.clear();
    }

    SERIALIZE_METHODS(CClaim, obj)
    {
        READWRITE(obj.sourceAddress, obj.targetAddress, obj.signatureString);
    }

    friend bool operator==(const CClaim& a, const CClaim& b)
    {
        return a.sourceAddress == b.sourceAddress;
    }

    friend bool operator!=(const CClaim& a, const CClaim& b)
    {
        return !(a == b);
    }
};

#endif // PATCHCOIN_PRIMITIVES_CLAIM_H
