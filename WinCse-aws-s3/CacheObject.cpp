#include "WinCseLib.h"
#include "CacheObject.hpp"

using namespace WCSE;


#define LN              L"\n"
#define INDENT1         L"\t"
#define INDENT2         L"\t\t"
#define INDENT3         L"\t\t\t"
#define INDENT4         L"\t\t\t\t"
#define INDENT5         L"\t\t\t\t\t"


void CacheHeadObject::report(CALLER_ARG FILE* fp)
{
	std::lock_guard<std::mutex> lock_{ mGuard };

    fwprintf(fp, L"GetPositive=%d" LN, mGetPositive);
    fwprintf(fp, L"SetPositive=%d" LN, mSetPositive);
    fwprintf(fp, L"UpdPositive=%d" LN, mUpdPositive);
    fwprintf(fp, L"GetNegative=%d" LN, mGetNegative);
    fwprintf(fp, L"SetNegative=%d" LN, mSetNegative);
    fwprintf(fp, L"UpdNegative=%d" LN, mUpdNegative);

    fwprintf(fp, L"[PositiveCache]" LN);
    fwprintf(fp, INDENT1 L"Positive.size=%zu" LN, mPositive.size());

    for (const auto& it: mPositive)
    {
        fwprintf(fp, INDENT1 L"bucket=[%s] key=[%s]" LN,
            it.first.bucket().c_str(), it.first.key().c_str());

        fwprintf(fp, INDENT2 L"RefCount=%d" LN, it.second.mRefCount);
        fwprintf(fp, INDENT2 L"CreateCallChain=%s" LN, it.second.mCreateCallChain.c_str());
        fwprintf(fp, INDENT2 L"LastAccessCallChain=%s" LN, it.second.mLastAccessCallChain.c_str());
        fwprintf(fp, INDENT2 L"CreateTime=%s" LN, TimePointToLocalTimeStringW(it.second.mCreateTime).c_str());
        fwprintf(fp, INDENT2 L"LastAccessTime=%s" LN, TimePointToLocalTimeStringW(it.second.mLastAccessTime).c_str());
        fwprintf(fp, INDENT2 L"[dirInfo]" LN);

        const auto dirInfo{ it.second.mV };

        fwprintf(fp, INDENT3 L"FileNameBuf=[%s]" LN, dirInfo->FileNameBuf);

        fwprintf(fp, INDENT3 L"FileSize=%llu" LN, dirInfo->FileInfo.FileSize);
        fwprintf(fp, INDENT3 L"FileAttributes=%u" LN, dirInfo->FileInfo.FileAttributes);
        fwprintf(fp, INDENT3 L"CreationTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.CreationTime).c_str());
        fwprintf(fp, INDENT3 L"LastAccessTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.LastAccessTime).c_str());
        fwprintf(fp, INDENT3 L"LastWriteTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.LastWriteTime).c_str());
    }

    fwprintf(fp, L"[NegativeCache]" LN);
    fwprintf(fp, INDENT1 L"mNegative.size=%zu" LN, mNegative.size());

    for (const auto& it: mNegative)
    {
        fwprintf(fp, INDENT1 L"bucket=[%s] key=[%s]" LN,
            it.first.bucket().c_str(), it.first.key().c_str());

        fwprintf(fp, INDENT2 L"refCount=%d" LN, it.second.mRefCount);
        fwprintf(fp, INDENT2 L"CreateCallChain=%s" LN, it.second.mCreateCallChain.c_str());
        fwprintf(fp, INDENT2 L"LastAccessCallChain=%s" LN, it.second.mLastAccessCallChain.c_str());
        fwprintf(fp, INDENT2 L"CreateTime=%s" LN, TimePointToLocalTimeStringW(it.second.mCreateTime).c_str());
        fwprintf(fp, INDENT2 L"LastAccessTime=%s" LN, TimePointToLocalTimeStringW(it.second.mLastAccessTime).c_str());
    }
}

void CacheListObjects::report(CALLER_ARG FILE* fp)
{
    std::lock_guard<std::mutex> lock_{ mGuard };

    fwprintf(fp, L"GetPositive=%d" LN, mGetPositive);
    fwprintf(fp, L"SetPositive=%d" LN, mSetPositive);
    fwprintf(fp, L"UpdPositive=%d" LN, mUpdPositive);
    fwprintf(fp, L"GetNegative=%d" LN, mGetNegative);
    fwprintf(fp, L"SetNegative=%d" LN, mSetNegative);
    fwprintf(fp, L"UpdNegative=%d" LN, mUpdNegative);

    fwprintf(fp, L"[PositiveCache]" LN);
    fwprintf(fp, INDENT1 L"Positive.size=%zu" LN, mPositive.size());

    for (const auto& it: mPositive)
    {
        fwprintf(fp, INDENT1 L"bucket=[%s] key=[%s]" LN,
            it.first.bucket().c_str(), it.first.key().c_str());

        fwprintf(fp, INDENT2 L"RefCount=%d" LN, it.second.mRefCount);
        fwprintf(fp, INDENT2 L"CreateCallChain=%s" LN, it.second.mCreateCallChain.c_str());
        fwprintf(fp, INDENT2 L"LastAccessCallChain=%s" LN, it.second.mLastAccessCallChain.c_str());
        fwprintf(fp, INDENT2 L"CreateTime=%s" LN, TimePointToLocalTimeStringW(it.second.mCreateTime).c_str());
        fwprintf(fp, INDENT2 L"LastAccessTime=%s" LN, TimePointToLocalTimeStringW(it.second.mLastAccessTime).c_str());
        fwprintf(fp, INDENT2 L"[dirInfoList]" LN);

        fwprintf(fp, INDENT3 L"dirInfoList.size=%zu" LN, it.second.mV.size());

        for (const auto& dirInfo: it.second.mV)
        {
            fwprintf(fp, INDENT4 L"FileNameBuf=[%s]" LN, dirInfo->FileNameBuf);

            fwprintf(fp, INDENT5 L"FileSize=%llu" LN, dirInfo->FileInfo.FileSize);
            fwprintf(fp, INDENT5 L"FileAttributes=%u" LN, dirInfo->FileInfo.FileAttributes);
            fwprintf(fp, INDENT5 L"CreationTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.CreationTime).c_str());
            fwprintf(fp, INDENT5 L"LastAccessTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.LastAccessTime).c_str());
            fwprintf(fp, INDENT5 L"LastWriteTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.LastWriteTime).c_str());
        }
    }

    fwprintf(fp, L"[NegativeCache]" LN);
    fwprintf(fp, INDENT1 L"mNegative.size=%zu" LN, mNegative.size());

    for (const auto& it: mNegative)
    {
        fwprintf(fp, INDENT1 L"bucket=[%s] key=[%s]" LN,
            it.first.bucket().c_str(), it.first.key().c_str());

        fwprintf(fp, INDENT2 L"refCount=%d" LN, it.second.mRefCount);
        fwprintf(fp, INDENT2 L"CreateCallChain=%s" LN, it.second.mCreateCallChain.c_str());
        fwprintf(fp, INDENT2 L"LastAccessCallChain=%s" LN, it.second.mLastAccessCallChain.c_str());
        fwprintf(fp, INDENT2 L"CreateTime=%s" LN, TimePointToLocalTimeStringW(it.second.mCreateTime).c_str());
        fwprintf(fp, INDENT2 L"LastAccessTime=%s" LN, TimePointToLocalTimeStringW(it.second.mLastAccessTime).c_str());
    }
}

// EOF