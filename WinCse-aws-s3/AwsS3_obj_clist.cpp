#include "AwsS3.hpp"

using namespace WCSE;


DirInfoType AwsS3::unsafeHeadObjectWithCache(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(!argObjKey.isBucket());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mHeadObjectCache.isNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return nullptr;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoType dirInfo;

    if (mHeadObjectCache.get(CONT_CALLER argObjKey, &dirInfo))
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
    }
    else
    {
        // HeadObject API �̎��s

        dirInfo = this->apicallHeadObject(CONT_CALLER argObjKey);
        if (!dirInfo)
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            NEW_LOG_BLOCK();

            traceW(L"fault: apicallHeadObject");

            mHeadObjectCache.addNegative(CONT_CALLER argObjKey);

            return nullptr;
        }

        // �L���b�V���ɃR�s�[

        NEW_LOG_BLOCK();

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mHeadObjectCache.set(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    return dirInfo;
}

DirInfoType AwsS3::unsafeHeadObjectWithCache_CheckDir(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(!argObjKey.isBucket());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mHeadObjectCache.isNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return nullptr;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoType dirInfo;

    if (mHeadObjectCache.get(CONT_CALLER argObjKey, &dirInfo))
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
    }
    else
    {
        // HeadObject API �̎��s
        // --> �f�B���N�g���Ȃ̂ŁA�V�K�쐬�̂Ƃ��ɂ����o�^����Ă��Ȃ�

        dirInfo = this->apicallHeadObject(CONT_CALLER argObjKey);
        if (!dirInfo)
        {
            NEW_LOG_BLOCK();

            traceW(L"fault: apicallHeadObject");

            //
            // �e�f�B���N�g���� CommonPrefix �Ńf�B���N�g�������擾
            //

            std::wstring parentDir;
            std::wstring searchName;

            const auto b = SplitPath(argObjKey.str(), &parentDir, &searchName);
            APP_ASSERT(b);

            DirInfoListType dirInfoList;

            if (!this->apicallListObjectsV2(CONT_CALLER ObjectKey::fromPath(parentDir), true, 0, &dirInfoList))
            {
                // �l�K�e�B�u�E�L���b�V���ɓo�^

                traceW(L"fault: apicallListObjectsV2");

                mHeadObjectCache.addNegative(CONT_CALLER argObjKey);

                return nullptr;
            }

            for (const auto& it: dirInfoList)
            {
                std::wstring fileName{ it->FileNameBuf };

                if (FA_IS_DIR(it->FileInfo.FileAttributes))
                {
                    fileName += L'/';
                }

                if (fileName == searchName)
                {
                    dirInfo = makeDirInfo_dir(it->FileNameBuf, it->FileInfo.LastWriteTime);
                    break;
                }
            }

            if (!dirInfo)
            {
                // �l�K�e�B�u�E�L���b�V���ɓo�^

                traceW(L"not found in Parent CommonPrefix");

                mHeadObjectCache.addNegative(CONT_CALLER argObjKey);

                return nullptr;
            }
        }

        // �L���b�V���ɃR�s�[

        NEW_LOG_BLOCK();
        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mHeadObjectCache.set(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    return dirInfo;
}

bool AwsS3::unsafeListObjectsWithCache(CALLER_ARG const ObjectKey& argObjKey,
    DirInfoListType* pDirInfoList /* nullable */)
{
    APP_ASSERT(argObjKey.meansDir());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mListObjectsCache.isNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoListType dirInfoList;

    if (mListObjectsCache.get(CONT_CALLER argObjKey, &dirInfoList))
    {
        // �|�W�e�B�u�E�L���b�V���Ɍ�������
    }
    else
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�����Ȃ�

        if (!this->apicallListObjectsV2(CONT_CALLER argObjKey, true, 0, &dirInfoList))
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            NEW_LOG_BLOCK();
            traceW(L"fault: apicallListObjectsV2");

            mListObjectsCache.addNegative(CONT_CALLER argObjKey);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        NEW_LOG_BLOCK();
        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mListObjectsCache.set(CONT_CALLER argObjKey, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

//
// �ȍ~�͌��J����Ă��邪�AgObjectCache ���X���b�h�E�Z�[�t�̂��ߔr������̕K�v�Ȃ�����
//

DirInfoType AwsS3::getCachedHeadObject(CALLER_ARG const ObjectKey& argObjKey)
{
    DirInfoType dirInfo;

    if (mHeadObjectCache.get(CONT_CALLER argObjKey, &dirInfo))
    {
        return dirInfo;
    }

    return nullptr;
}

bool AwsS3::isNegativeHeadObject(CALLER_ARG const ObjectKey& argObjKey)
{
    return mHeadObjectCache.isNegative(CONT_CALLER argObjKey); 
}

void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    mHeadObjectCache.report(CONT_CALLER fp);
    mListObjectsCache.report(CONT_CALLER fp);
}

int AwsS3::deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    const auto delHead = mHeadObjectCache.deleteByTime(CONT_CALLER threshold);
    const auto delList = mListObjectsCache.deleteByTime(CONT_CALLER threshold);

    return delHead + delList;
}

int AwsS3::clearObjectCache(CALLER_ARG0)
{
    const auto now{ std::chrono::system_clock::now() };

    return this->deleteOldObjectCache(CONT_CALLER now);
}

int AwsS3::deleteObjectCache(CALLER_ARG const ObjectKey& argObjKey)
{
    const auto delHead = mHeadObjectCache.deleteByKey(CONT_CALLER argObjKey);
    const auto delList = mListObjectsCache.deleteByKey(CONT_CALLER argObjKey);

    return delHead + delList;
}

// EOF