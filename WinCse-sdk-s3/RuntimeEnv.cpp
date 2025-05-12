#include "RuntimeEnv.hpp"

using namespace CSELIB;
using namespace CSESS3;


#define TO_LITERAL(name)        L#name

#define KV_WSTR(name)           std::wstring(TO_LITERAL(name)) + L'=' + name
#define KV_BOOL(name)           std::wstring(TO_LITERAL(name)) + L'=' + BOOL_CSTRW(name)
#define KV_TO_WSTR(name)        std::wstring(TO_LITERAL(name)) + L'=' + std::to_wstring(name)

std::wstring RuntimeEnv::str() const
{
    LastErrorBackup _backup;

    return JoinStrings(std::initializer_list{
        KV_TO_WSTR(BucketCacheExpiryMin),
        KV_WSTR(ClientGuid),
        KV_TO_WSTR(DefaultCommonPrefixTime),
        KV_TO_WSTR(MaxDisplayBuckets),
        KV_TO_WSTR(MaxDisplayObjects),
        KV_TO_WSTR(ObjectCacheExpiryMin),
        KV_WSTR(ClientRegion),
        KV_BOOL(StrictBucketRegion),
        KV_BOOL(StrictFileTimestamp),
        KV_TO_WSTR(TransferWriteSizeMib)
        }, L", ", true);
}

// EOF