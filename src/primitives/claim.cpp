#include <primitives/claim.h>

CClaim::CClaim(const std::string& sourceAddressIn, const std::string& targetAddressIn, const std::string& signatureStringIn)
{
    sourceAddress = sourceAddressIn;
    targetAddress = targetAddressIn;
    signatureString = signatureStringIn;
}
