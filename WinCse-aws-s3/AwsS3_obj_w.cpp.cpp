#include "AwsS3.hpp"
#include <fstream>
#include <iostream>

using namespace WinCseLib;


bool AwsS3::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

#if !DELETE_ONLY_EMPTY_DIR
    // 先にディレクトリ内のファイルから削除する
    // --> サブディレクトリは含まれていないはず

    if (argObjKey.meansDir())
    {
        while (1)
        {
            //
            // 一度の listObjects では最大数の制限があるかもしれないので、削除する
            // 対象がなくなるまで繰り返す
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
                    // 削除開始からここまでの間にディレクトリが作成される可能性を考え
                    // 存在したら無視

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

    // キャッシュ・メモリから削除

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
        // create() から呼び出される場合はこちらを通過
        // --> まだローカル・キャッシュも作成される前なので、ファイル名もない

        const auto dirInfo{ makeDirInfo_byName(argObjKey.key(), GetCurrentWinFileTime100ns()) };

        fileInfo = dirInfo->FileInfo;
    }
    else
    {
        // ローカル・キャッシュの内容をアップロードする

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
        // ファイルの時のみ

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

    // キャッシュ・メモリから削除
    //
    // 後続の処理で DoGetSecurityByName() が呼ばれるが、上記で作成したディレクトリが
    // キャッシュに反映されていない状態で利用されてしまうことを回避するために
    // 事前に削除しておき、改めてキャッシュを作成させる

    const auto num = deleteCacheByObjectKey(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d", num);

    if (pFileInfo)
    {
        *pFileInfo = fileInfo;
    }

    return true;
}

// EOF