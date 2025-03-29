#include "AwsS3.hpp"
#include <fstream>
#include <iostream>

using namespace WinCseLib;


bool AwsS3::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

#if !DELETE_ONLY_EMPTY_DIR
    // ��Ƀf�B���N�g�����̃t�@�C������폜����
    // --> �T�u�f�B���N�g���͊܂܂�Ă��Ȃ��͂�

    if (argObjKey.meansDir())
    {
        while (1)
        {
            //
            // ��x�� listObjects �ł͍ő吔�̐��������邩������Ȃ��̂ŁA�폜����
            // �Ώۂ��Ȃ��Ȃ�܂ŌJ��Ԃ�
            //

            DirInfoListType dirInfoList;

            if (!listObjects(CONT_CALLER argObjKey, &dirInfoList))
            {
                traceW(L"fault: listObjects");
                return false;
            }

            Aws::S3::Model::Delete delete_objects;

            for (const auto& dirInfo: dirInfoList)
            {
                if (wcscmp(dirInfo->FileNameBuf, L".") == 0 || wcscmp(dirInfo->FileNameBuf, L"..") == 0)
                {
                    continue;
                }

                if (FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
                {
                    // �폜�J�n���炱���܂ł̊ԂɃf�B���N�g�����쐬�����\�����l��
                    // ���݂����疳��

                    continue;
                }

                const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

                Aws::S3::Model::ObjectIdentifier obj;
                obj.SetKey(fileObjKey.keyA());
                delete_objects.AddObjects(obj);

                //
                std::wstring localPath;

                if (!GetCacheFilePath(mCacheDataDir, fileObjKey.str(), &localPath))
                {
                    traceW(L"fault: GetCacheFilePath");
                    return false;
                }

                if (!::DeleteFileW(localPath.c_str()))
                {
                    const auto lerr = ::GetLastError();
                    if (lerr != ERROR_FILE_NOT_FOUND)
                    {
                        traceW(L"fault: DeleteFile");
                        return false;
                    }
                }

                //
                const auto num = deleteCacheByObjectKey(CONT_CALLER fileObjKey);
                traceW(L"cache delete num=%d", num);
            }

            if (delete_objects.GetObjects().empty())
            {
                break;
            }

            Aws::S3::Model::DeleteObjectsRequest request;
            request.SetBucket(argObjKey.bucketA());
            request.SetDelete(delete_objects);

            const auto outcome = mClient->DeleteObjects(request);

            if (!outcomeIsSuccess(outcome))
            {
                traceW(L"fault: DeleteObjects");
                return false;
            }
        }
    }

#endif

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    const auto outcome = mClient->DeleteObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    // �L���b�V���E����������폜

    const auto num = deleteCacheByObjectKey(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d", num);

    return true;
}

bool AwsS3::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const wchar_t* sourceFile /* nullable */, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());
    APP_ASSERT(!argObjKey.isBucket());

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    FSP_FSCTL_FILE_INFO fileInfo{};

    if (sourceFile == nullptr)
    {
        // create() ����Ăяo�����ꍇ�͂������ʉ�
        // --> �܂����[�J���E�L���b�V�����쐬�����O�Ȃ̂ŁA�t�@�C�������Ȃ�

        const auto dirInfo{ makeDirInfo_byName(argObjKey.key(), GetCurrentWinFileTime100ns()) };

        fileInfo = dirInfo->FileInfo;
    }
    else
    {
        // ���[�J���E�L���b�V���̓��e���A�b�v���[�h����

        APP_ASSERT(argObjKey.meansFile());

        if (!PathToFileInfoW(sourceFile, &fileInfo))
        {
            traceW(L"fault: PathToFileInfoA");
            return false;
        }

        const Aws::String fileName{ WC2MB(sourceFile) };

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>
        (
            __FUNCTION__,
            fileName.c_str(),
            std::ios_base::in | std::ios_base::binary
        );

        if (!inputData->good())
        {
            traceW(L"fault: inputData->good");
            return false;
        }

        request.SetBody(inputData);
    }

    if (argObjKey.meansFile())
    {
        // �t�@�C���̎��̂�

        request.AddMetadata("wincse-creation-time", std::to_string(fileInfo.CreationTime).c_str());
        request.AddMetadata("wincse-last-access-time", std::to_string(fileInfo.LastAccessTime).c_str());
        request.AddMetadata("wincse-last-write-time", std::to_string(fileInfo.LastWriteTime).c_str());
#if SET_ATTRIBUTES_LOCAL_FILE
        request.AddMetadata("wincse-file-attributes", std::to_string(fileInfo.FileAttributes).c_str());
#endif

#if _DEBUG
        request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(fileInfo.CreationTime).c_str());
        request.AddMetadata("wincse-debug-last-access-time", WinFileTime100nsToLocalTimeStringA(fileInfo.LastAccessTime).c_str());
        request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(fileInfo.LastWriteTime).c_str());
#endif
    }

    const auto outcome = mClient->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    // �L���b�V���E����������폜
    //
    // �㑱�̏����� DoGetSecurityByName() ���Ă΂�邪�A��L�ō쐬�����f�B���N�g����
    // �L���b�V���ɔ��f����Ă��Ȃ���Ԃŗ��p����Ă��܂����Ƃ�������邽�߂�
    // ���O�ɍ폜���Ă����A���߂ăL���b�V�����쐬������

    const auto num = deleteCacheByObjectKey(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d", num);

    if (pFileInfo)
    {
        *pFileInfo = fileInfo;
    }

    return true;
}

// EOF