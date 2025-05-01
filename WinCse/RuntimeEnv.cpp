#include "RuntimeEnv.hpp"

using namespace CSELIB;
using namespace CSEDRV;


#define TO_LITERAL(name)        L#name

#define KV_FSSTR(name)          std::wstring(TO_LITERAL(name)) + L'=' + name.wstring()
#define KV_BOOL(name)           std::wstring(TO_LITERAL(name)) + L'=' + BOOL_CSTRW(name)
#define KV_TO_WSTR(name)        std::wstring(TO_LITERAL(name)) + L'=' + std::to_wstring(name)

std::wstring RuntimeEnv::str() const noexcept
{
    return JoinStrings(std::initializer_list{
        KV_FSSTR(CacheDataDir),
        KV_TO_WSTR(CacheFileRetentionMin),
        KV_FSSTR(CacheReportDir),
        KV_BOOL(DeleteAfterUpload),
        KV_TO_WSTR(DeleteDirCondition),
        KV_BOOL(ReadOnly),
        KV_TO_WSTR(TransferPerSizeMib)
        }, L", ", true);
}

// EOF