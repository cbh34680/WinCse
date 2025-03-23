#include "WinCseLib.h"
#include "ObjectCache.hpp"
#include <functional>


using namespace WinCseLib;


//
// �I�u�W�F�N�g�E�L���b�V���� headObject, listObjects �ō쐬����A�Ăяo������
// �p�����[�^���L�[�Ƃ��ĕۑ����Ă���B
// ������ DoGetSecurityByName, DoOpen ���o�R���� FileNameToFileInfo �ɂ��
// ��ɌĂяo����Ă���B
// 
// �ړI�ɉ����� limit, delimiter �̒l�͈قȂ邪�A��Ɉȉ��̂悤�Ȋ����ɂȂ�
// 
//      Purpose::CheckFileExists ... �t�@�C���̑��݊m�F�A�����擾         DoGetSecurityByName, DoOpen -> headObject
//      Purpose::CheckDirExists  ... �f�B���N�g���̑��݊m�F�A�����擾     DoGetSecurityByName, DoOpen -> listObjects
//      Purpose::Display         ... �f�B���N�g�����̃I�u�W�F�N�g�ꗗ     DoReadDirectory             -> listObjects
// 
// �L���b�V���͏�L�̃L�[�ɕR�Â����I�u�W�F�N�g�̈ꗗ�Ȃ̂�
// �ꌏ�̂ݕۑ����Ă���Ƃ��� FSP_FSCTL_DIR_INFO �̃��X�g�ƂȂ�B
//

// ----------------------- Positive File

bool ObjectCache::getPositive_File(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo)
{
    APP_ASSERT(pDirInfo);

    DirInfoListType dirInfoList;

    if (!getPositive(CONT_CALLER argObjKey, Purpose::CheckFileExists, &dirInfoList))
    {
        return false;
    }

    APP_ASSERT(dirInfoList.size() == 1);
    *pDirInfo = (*dirInfoList.begin());

    return true;
}

void ObjectCache::setPositive_File(CALLER_ARG const ObjectKey& argObjKey, DirInfoType& dirInfo)
{
    APP_ASSERT(dirInfo);

    // �L���b�V���ɃR�s�[

    DirInfoListType dirInfoList{ dirInfo };

    setPositive(CONT_CALLER argObjKey, Purpose::CheckFileExists, dirInfoList);
}

// ----------------------- Negative File

bool ObjectCache::isInNegative_File(CALLER_ARG const ObjectKey& argObjKey)
{
    return isInNegative(CONT_CALLER argObjKey, Purpose::CheckFileExists);
}

void ObjectCache::addNegative_File(CALLER_ARG const ObjectKey& argObjKey)
{
    addNegative(CONT_CALLER argObjKey, Purpose::CheckFileExists);
}

const wchar_t* PurposeString(const Purpose p)
{
    static const wchar_t* PURPOSE_STRINGS[] = { L"*None*", L"CheckDirExists", L"Display", L"CheckFileExists", };

    const int i = static_cast<int>(p);
    APP_ASSERT(i < _countof(PURPOSE_STRINGS));

    return PURPOSE_STRINGS[i];
};

template <typename T>
int eraseCacheBy(const std::function<bool(const typename T::iterator&)>& shouldErase, T& cache)
{
    int count = 0;

    for (auto it=cache.begin(); it!=cache.end(); )
    {
        if (shouldErase(it))
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

// ---------------------------------------------------------------------------
//
// �ȍ~�͒��ڃ����o�ϐ��𑀍삷�����
//

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
            it.first.mObjKey.bucket().c_str(), it.first.mObjKey.key().c_str(), PurposeString(it.first.mPurpose));

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
            fwprintf(fp, INDENT5 L"CreationTime=%s" LN, WinFileTime100nsToLocalTimeStringW(dirInfo->FileInfo.LastWriteTime).c_str());
        }
    }

    fwprintf(fp, L"[NegativeCache]" LN);
    fwprintf(fp, INDENT1 L"mNegative.size=%zu" LN, mNegative.size());

    for (const auto& it: mNegative)
    {
        fwprintf(fp, INDENT1 L"bucket=[%s] key=[%s] purpose=%s" LN,
            it.first.mObjKey.bucket().c_str(), it.first.mObjKey.key().c_str(), PurposeString(it.first.mPurpose));

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

    const auto OldAccessTime = [&threshold](const auto& it)
    {
        return it->second.mAccessTime < threshold;
    };

    const int delPositive = eraseCacheBy(OldAccessTime, mPositive);
    const int delNegative = eraseCacheBy(OldAccessTime, mNegative);

    traceW(L"delete records: Positive=%d Negative=%d", delPositive, delNegative);

    return delPositive + delNegative;
}

int ObjectCache::deleteByObjectKey(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s", argObjKey.c_str());

    const auto EqualObjKey = [&argObjKey](const auto& it)
    {
        return it->first.mObjKey == argObjKey;
    };

    // �����ƈ�v������̂��L���b�V������폜

    const int delPositive = eraseCacheBy(EqualObjKey, mPositive);
    const int delNegative = eraseCacheBy(EqualObjKey, mNegative);

    traceW(L"delete records: Positive=%d Negative=%d", delPositive, delNegative);

    int delPositiveP = 0;
    int delNegativeP = 0;

    const auto parentDirPtr{ argObjKey.toParentDir() };
    if (parentDirPtr)
    {
        const auto& parentDir{ *parentDirPtr };

        const auto EqualParentDir = [&parentDir](const auto& it)
        {
            return it->first.mObjKey == parentDir;
        };

        // �����̐e�f�B���N�g�����L���b�V������폜

        delPositiveP = eraseCacheBy(EqualParentDir, mPositive);
        delNegativeP = eraseCacheBy(EqualParentDir, mNegative);

        traceW(L"delete records: PositiveP=%d NegativeP=%d", delPositiveP, delNegativeP);
    }
    else
    {
        // ������ "\bucket" �̏ꍇ�͂������ʉ߂��邪
        // �e�f�B���N�g���͑��݂��Ȃ��̂Ŗ��Ȃ�
    }

    return delPositive + delNegative + delPositiveP + delNegativeP;
}

// ----------------------- Positive

bool ObjectCache::getPositive(CALLER_ARG const ObjectKey& argObjKey,
    const Purpose argPurpose, DirInfoListType* pDirInfoList)
{
    APP_ASSERT(argObjKey.valid());
    APP_ASSERT(pDirInfoList);

    const ObjectCacheKey cacheKey{ argObjKey, argPurpose };
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

void ObjectCache::setPositive(CALLER_ARG const ObjectKey& argObjKey,
    const Purpose argPurpose, DirInfoListType& dirInfoList)
{
    APP_ASSERT(argObjKey.valid());
    APP_ASSERT(!dirInfoList.empty());

    switch (argPurpose)
    {
        case Purpose::CheckDirExists:
        {
            // �f�B���N�g���̑��݊m�F�ׂ̈ɂ����Ă΂��͂�

            APP_ASSERT(argObjKey.meansDir());
            APP_ASSERT(argObjKey.hasKey());

            break;
        }
        case Purpose::Display:
        {
            // DoReadDirectory() ����̂݌Ăяo�����͂�

            APP_ASSERT(argObjKey.meansDir());

            break;
        }
        case Purpose::CheckFileExists:
        {
            // �t�@�C���̑��݊m�F�ׂ̈ɂ����Ă΂��͂�

            APP_ASSERT(argObjKey.meansFile());

            break;
        }
        default:
        {
            APP_ASSERT(0);
        }
    }

    // �L���b�V���ɃR�s�[

    const ObjectCacheKey cacheKey{ argObjKey, argPurpose };
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

// ----------------------- Negative

bool ObjectCache::isInNegative(CALLER_ARG const ObjectKey& argObjKey, const Purpose argPurpose)
{
    APP_ASSERT(argObjKey.valid());

    const ObjectCacheKey cacheKey{ argObjKey, argPurpose };
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

void ObjectCache::addNegative(CALLER_ARG const WinCseLib::ObjectKey& argObjKey, const Purpose argPurpose)
{
    APP_ASSERT(argObjKey.valid());

    const ObjectCacheKey cacheKey{ argObjKey, argPurpose };
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

// EOF