#include "AwsS3.hpp"
#include "ObjectCache.hpp"

using namespace WinCseLib;


static ObjectCache gObjectCache;

void AwsS3::unsafeReportObjectCache(CALLER_ARG FILE* fp)
{
    gObjectCache.report(CONT_CALLER fp);
}

int AwsS3::unsafeDeleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    return gObjectCache.deleteByTime(CONT_CALLER threshold);
}

int AwsS3::unsafeClearObjectCache(CALLER_ARG0)
{
    return gObjectCache.deleteByTime(CONT_CALLER std::chrono::system_clock::now());
}

int AwsS3::unsafeDeleteObjectCache(CALLER_ARG const WinCseLib::ObjectKey& argObjKey)
{
    return gObjectCache.deleteByKey(CONT_CALLER argObjKey);
}

bool AwsS3::unsafeHeadObjectWithCache(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoType dirInfo;
    const bool inPositiveCache = gObjectCache.getPositive_File(CONT_CALLER argObjKey, &dirInfo);

    if (inPositiveCache)
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������

        APP_ASSERT(dirInfo);
        //traceW(L"found in positive-cache");
    }
    else
    {
        //traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative_File(CONT_CALLER argObjKey))
        {
            // �l�K�e�B�u�E�L���b�V�����Ɍ�������

            //traceW(L"found in negative cache");

            return false;
        }

        // HeadObject API �̎��s

        //traceW(L"do HeadObject");

        dirInfo = this->apicallHeadObject(CONT_CALLER argObjKey);
        if (!dirInfo)
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            //traceW(L"add negative");

            gObjectCache.addNegative_File(CONT_CALLER argObjKey);

            return false;
        }

        // �L���b�V���ɃR�s�[

        gObjectCache.setPositive_File(CONT_CALLER argObjKey, dirInfo);
    }

    if (pFileInfo)
    {
        (*pFileInfo) = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::unsafeListObjectsWithCache(CALLER_ARG const ObjectKey& argObjKey,
    const Purpose argPurpose, DirInfoListType* pDirInfoList /* nullable */)
{
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    //traceW(L"purpose=%s, argObjKey=%s", PurposeString(argPurpose), argObjKey.c_str());

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoListType dirInfoList;
    const bool inPositiveCache = gObjectCache.getPositive(CONT_CALLER argObjKey, argPurpose, &dirInfoList);

    if (inPositiveCache)
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������

        //traceW(L"found in positive-cache");
    }
    else
    {
        //traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative(CONT_CALLER argObjKey, argPurpose))
        {
            // �l�K�e�B�u�E�L���b�V�����Ɍ�������

            //traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() �̎��s

        //traceW(L"call doListObjectV2");

        if (!this->apicallListObjectsV2(CONT_CALLER argPurpose, argObjKey, &dirInfoList))
        {
            // ���s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�

            //traceW(L"object not found");

            // �l�K�e�B�u�E�L���b�V���ɓo�^

            //traceW(L"add negative");
            gObjectCache.addNegative(CONT_CALLER argObjKey, argPurpose);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        gObjectCache.setPositive(CONT_CALLER argObjKey, argPurpose, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

bool AwsS3::unsafeGetPositiveCache_File(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo)
{
    APP_ASSERT(argObjKey.meansFile());

    return gObjectCache.getPositive_File(CONT_CALLER argObjKey, pDirInfo); 
}

// EOF