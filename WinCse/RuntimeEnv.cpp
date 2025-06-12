#include "RuntimeEnv.hpp"

using namespace CSELIB;

#define TO_LITERAL(name)        L#name

#define KV_FSSTR(name)          std::wstring(TO_LITERAL(name)) + L'=' + name.wstring()
#define KV_BOOL(name)           std::wstring(TO_LITERAL(name)) + L'=' + BOOL_CSTRW(name)
#define KV_TO_WSTR(name)        std::wstring(TO_LITERAL(name)) + L'=' + std::to_wstring(name)

namespace CSEDRV {

std::wstring RuntimeEnv::str() const
{
    KeepLastError _keep;

    return JoinStrings(std::initializer_list{
        KV_FSSTR(CacheDataDir),
        KV_TO_WSTR(CacheFileRetentionMin),
        KV_FSSTR(CacheReportDir),
        KV_TO_WSTR(DefaultCommonPrefixTime),
        KV_TO_WSTR(DefaultFileAttributes),
        KV_TO_WSTR(DeleteAfterUpload),
        KV_TO_WSTR(DeleteDirCondition),
        KV_BOOL(ReadOnly),
        KV_TO_WSTR(TransferReadSizeMib)
        }, L", ", true);
}

}   // namespace CSEDRV

// EOF