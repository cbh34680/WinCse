#include "RuntimeEnv.hpp"

using namespace CSELIB;

#define TO_LITERAL(name)        L#name

#define KV_WSTR(name)           std::wstring(TO_LITERAL(name)) + L'=' + name
#define KV_BOOL(name)           std::wstring(TO_LITERAL(name)) + L'=' + BOOL_CSTRW(name)
#define KV_TO_WSTR(name)        std::wstring(TO_LITERAL(name)) + L'=' + std::to_wstring(name)

namespace CSEDVC {

std::wstring RuntimeEnv::str() const
{
    KeepLastError _keep;

    return JoinStrings(std::initializer_list{
        KV_TO_WSTR(BucketCacheExpiryMin),
        KV_WSTR(ClientGuid),
        KV_TO_WSTR(DefaultCommonPrefixTime),
        KV_BOOL(IgnoreBucketRegion),
        KV_TO_WSTR(MaxApiRetryCount),
        KV_TO_WSTR(MaxDisplayBuckets),
        KV_TO_WSTR(MaxDisplayObjects),
        KV_TO_WSTR(ObjectCacheExpiryMin),
        KV_BOOL(StrictBucketRegion),
        KV_BOOL(StrictFileTimestamp),
        KV_TO_WSTR(TransferWriteSizeMib)
        }, L", ", true);
}

bool RuntimeEnv::matchesBucketFilter(const std::wstring& argBucketName) const
{
    const auto& filters{ this->BucketFilters };

    if (filters.empty())
    {
        return true;
    }

    const auto it = std::find_if(filters.cbegin(), filters.cend(), [&argBucketName](const auto& item)
    {
        return std::regex_match(argBucketName, item);
    });

    return it != filters.cend();
}

bool RuntimeEnv::shouldIgnoreWinPath(const std::filesystem::path& argWinPath) const
{
    APP_ASSERT(!argWinPath.empty());
    APP_ASSERT(argWinPath.wstring().at(0) == L'\\');

    // ���X�g�̍ő吔�Ɋ֘A����̂ŁAAPI ���s���ʂ𐶐�����Ƃ��ɂ��`�F�b�N���K�v

    if (this->IgnoreFileNamePatterns)
    {
        return std::regex_search(argWinPath.wstring(), *this->IgnoreFileNamePatterns);
    }

    // ���K�\�����ݒ肳��Ă��Ȃ�

    return false;
}

}   // namespace CSEDVC

// EOF