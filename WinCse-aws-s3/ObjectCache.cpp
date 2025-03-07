#include "WinCseLib.h"
#include "ObjectCache.hpp"
#include <filesystem>
#include <mutex>


using namespace WinCseLib;


//
// �I�u�W�F�N�g�E�L���b�V���� headObject, listObjects �ō쐬����A�Ăяo������
// �p�����[�^���L�[�Ƃ��ĕۑ����Ă���B
// ������ DoGetSecurityByName, DoOpen ���o�R���� FileNameToFileInfo �ɂ��
// ��ɌĂяo����Ă���B
// 
// �ړI�ɉ����� limit, delimiter �̒l�͈قȂ邪�A��Ɉȉ��̂悤�Ȋ����ɂȂ�
// 
//      Purpose::CheckFile ... �t�@�C���̑��݊m�F�A�����擾         DoGetSecurityByName, DoOpen -> headObject
//      Purpose::CheckDir  ... �f�B���N�g���̑��݊m�F�A�����擾     DoGetSecurityByName, DoOpen -> listObjects
//      Purpose::Display   ... �f�B���N�g�����̃I�u�W�F�N�g�ꗗ     DoReadDirectory             -> listObjects
// 
// �L���b�V���͏�L�̃L�[�ɕR�Â����I�u�W�F�N�g�̈ꗗ�Ȃ̂�
// �ꌏ�̂ݕۑ����Ă���Ƃ��� FSP_FSCTL_DIR_INFO �̃��X�g�ƂȂ�B
//

template <typename T>
int eraseByTime(T& cache, std::chrono::system_clock::time_point threshold)
{
    int count = 0;

    for (auto it=cache.begin(); it!=cache.end(); )
    {
        // �ŏI�A�N�Z�X���Ԃ�������菬�����ꍇ�͍폜

        if (it->second.mAccessTime < threshold)
        {
            it = cache.erase(it);
            count++;
        }
        else
        {
            ++it;
        }
    }

    return count;
}

#define LN              L"\n"
#define INDENT1         L"\t"
#define INDENT2         L"\t\t"
#define INDENT3         L"\t\t\t"
#define INDENT4         L"\t\t\t\t"
#define INDENT5         L"\t\t\t\t\t"

void ObjectCache::report(CALLER_ARG FILE* fp)
{
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
        fwprintf(fp, INDENT1 L"bucket=[%s] key=[%s] purpose=%s" LN,
            it.first.mBucket.c_str(), it.first.mKey.c_str(), PurposeString(it.first.mPurpose));

        fwprintf(fp, INDENT2 L"refCount=%d" LN, it.second.mRefCount);
        fwprintf(fp, INDENT2 L"createCallChain=%s" LN, it.second.mCreateCallChain.c_str());
        fwprintf(fp, INDENT2 L"accessCallChain=%s" LN, it.second.mAccessCallChain.c_str());
        fwprintf(fp, INDENT2 L"createTime=%s" LN, TimePointToLocalTimeStringW(it.second.mCreateTime).c_str());
        fwprintf(fp, INDENT2 L"accessTime=%s" LN, TimePointToLocalTimeStringW(it.second.mAccessTime).c_str());
        fwprintf(fp, INDENT2 L"[dirInfoList]" LN);

        fwprintf(fp, INDENT3 L"dirInfoList.size=%zu" LN, it.second.mDirInfoList.size());

        for (const auto& dirInfo: it.second.mDirInfoList)
        {
            fwprintf(fp, INDENT4 L"FileNameBuf=[%s]" LN, dirInfo->FileNameBuf);

            fwprintf(fp, INDENT5 L"FileSize=%llu" LN, dirInfo->FileInfo.FileSize);
            fwprintf(fp, INDENT5 L"FileAttributes=%u" LN, dirInfo->FileInfo.FileAttributes);
            fwprintf(fp, INDENT5 L"CreationTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.CreationTime).c_str());
        }
    }

    fwprintf(fp, L"[NegativeCache]" LN);
    fwprintf(fp, INDENT1 L"mNegative.size=%zu" LN, mNegative.size());

    for (const auto& it: mNegative)
    {
        fwprintf(fp, INDENT1 L"bucket=[%s] key=[%s] purpose=%s" LN,
            it.first.mBucket.c_str(), it.first.mKey.c_str(), PurposeString(it.first.mPurpose));

        fwprintf(fp, INDENT2 L"refCount=%d" LN, it.second.mRefCount);
        fwprintf(fp, INDENT2 L"createCallChain=%s" LN, it.second.mCreateCallChain.c_str());
        fwprintf(fp, INDENT2 L"accessCallChain=%s" LN, it.second.mAccessCallChain.c_str());
        fwprintf(fp, INDENT2 L"createTime=%s" LN, TimePointToLocalTimeStringW(it.second.mCreateTime).c_str());
        fwprintf(fp, INDENT2 L"accessTime=%s" LN, TimePointToLocalTimeStringW(it.second.mAccessTime).c_str());
    }
}

int ObjectCache::deleteOldRecords(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    NEW_LOG_BLOCK();

    const int delPositiveDir = eraseByTime(mPositive, threshold);
    const int delNegative = eraseByTime(mNegative, threshold);

    traceW(L"delete records: PositiveDir=%d Negative=%d", delPositiveDir, delNegative);

    return delPositiveDir + delNegative;
}

// ----------------------- Positive Dir

bool ObjectCache::getPositive(CALLER_ARG const Purpose argPurpose,
    const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    APP_ASSERT(pDirInfoList);

    const ObjectCacheKey cacheKey{ argPurpose, argBucket, argKey };
    const auto it{ mPositive.find(cacheKey) };

    if (it == mPositive.end())
    {
        return false;
    }

    *pDirInfoList = it->second.mDirInfoList;

    it->second.mAccessCallChain = CALL_CHAIN();
    it->second.mAccessTime = std::chrono::system_clock::now();
    it->second.mRefCount++;
    mGetPositive++;

    return true;
}

void ObjectCache::setPositive(CALLER_ARG const Purpose argPurpose,
    const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType& dirInfoList)
{
    APP_ASSERT(!dirInfoList.empty());

    switch (argPurpose)
    {
        case Purpose::CheckDir:
        {
            // �f�B���N�g���̑��݊m�F�ׂ̈ɂ����Ă΂��͂�
            APP_ASSERT(!argKey.empty());
            APP_ASSERT(argKey.back() == L'/');

            break;
        }
        case Purpose::Display:
        {
            // DoReadDirectory() ����̂݌Ăяo�����͂�
            if (!argKey.empty())
            {
                APP_ASSERT(argKey.back() == L'/');
            }

            break;
        }
        case Purpose::CheckFile:
        {
            // �t�@�C���̑��݊m�F�ׂ̈ɂ����Ă΂��͂�
            APP_ASSERT(!argKey.empty());
            APP_ASSERT(argKey.back() != L'/');

            break;
        }
        default:
        {
            APP_ASSERT(0);
        }
    }

    // �L���b�V���ɃR�s�[

    const ObjectCacheKey cacheKey{ argPurpose, argBucket, argKey };
    const PosisiveCacheVal cacheVal{ CONT_CALLER dirInfoList };

    if (mPositive.find(cacheKey) == mPositive.end())
    {
        mSetPositive++;
    }
    else
    {
        mUpdPositive++;
    }

    mPositive.emplace(cacheKey, cacheVal);
}

// ----------------------- Positive File

bool ObjectCache::getPositive_File(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoType* pDirInfo)
{
    APP_ASSERT(pDirInfo);

    DirInfoListType dirInfoList;

    if (!getPositive(CONT_CALLER Purpose::CheckFile, argBucket, argKey, &dirInfoList))
    {
        return false;
    }

    APP_ASSERT(dirInfoList.size() == 1);
    *pDirInfo = (*dirInfoList.begin());

    return true;
}

void ObjectCache::setPositive_File(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoType& dirInfo)
{
    APP_ASSERT(dirInfo);

    // �L���b�V���ɃR�s�[

    DirInfoListType dirInfoList{ dirInfo };

    setPositive(CONT_CALLER Purpose::CheckFile, argBucket, argKey, dirInfoList);
}

// ----------------------- Negative Dir

bool ObjectCache::isInNegative(CALLER_ARG const Purpose argPurpose,
    const std::wstring& argBucket, const std::wstring& argKey)
{
    const ObjectCacheKey cacheKey{ argPurpose, argBucket, argKey };
    const auto it{ mNegative.find(cacheKey) };

    if (it == mNegative.end())
    {
        return false;
    }

    it->second.mAccessCallChain = CALL_CHAIN();
    it->second.mAccessTime = std::chrono::system_clock::now();
    it->second.mRefCount++;
    mGetNegative++;

    return true;
}

void ObjectCache::addNegative(CALLER_ARG const Purpose argPurpose,
    const std::wstring& argBucket, const std::wstring& argKey)
{
    // �L���b�V���ɃR�s�[

    const ObjectCacheKey cacheKey{ argPurpose, argBucket, argKey };
    const NegativeCacheVal cacheVal{ CONT_CALLER0 };

    if (mNegative.find(cacheKey) == mNegative.end())
    {
        mSetNegative++;
    }
    else
    {
        mUpdNegative++;
    }

    mNegative.emplace(cacheKey, cacheVal);
}

// ----------------------- Negative File

bool ObjectCache::isInNegative_File(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    return isInNegative(CONT_CALLER Purpose::CheckFile, argBucket, argKey);
}

void ObjectCache::addNegative_File(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    addNegative(CONT_CALLER Purpose::CheckFile, argBucket, argKey);
}

static const wchar_t* PURPOSE_STRINGS[] = { L"*None*", L"CheckDir", L"Display", L"CheckFile", };

const wchar_t* PurposeString(const Purpose p)
{
    const int i = static_cast<int>(p);
    APP_ASSERT(i < _countof(PURPOSE_STRINGS));

    return PURPOSE_STRINGS[i];
};

// EOF