#include "AwsS3.hpp"
#include <fstream>

using namespace WCSE;


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
NTSTATUS AwsS3::readObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    StatsIncr(readObject);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s, ctx=%p HANDLE=%p, Offset=%llu, Length=%lu",
        ctx->mObjKey.c_str(), ctx, ctx->mFile.handle(), Offset, Length);

    NTSTATUS ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);

    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: prepareLocalFile");
        return ntstatus;
    }

    APP_ASSERT(ctx->mFile.valid());

    // Offset, Length �ɂ��t�@�C����ǂ�

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: ReadFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"PBytesTransferred=%lu", *PBytesTransferred);

    return STATUS_SUCCESS;
}

NTSTATUS AwsS3::writeObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s, ctx=%p HANDLE=%p, Offset=%llu, Length=%lu, WriteToEndOfFile=%s, ConstrainedIo=%s",
        ctx->mObjKey.c_str(), ctx, ctx->mFile.handle(), Offset, Length,
        BOOL_CSTRW(WriteToEndOfFile), BOOL_CSTRW(ConstrainedIo));

    NTSTATUS ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);

    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: prepareLocalFile");
        return ntstatus;
    }

    APP_ASSERT(ctx->mFile.valid());

    if (ConstrainedIo)
    {
        LARGE_INTEGER FileSize;

        if (!::GetFileSizeEx(ctx->mFile.handle(), &FileSize))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

            return FspNtStatusFromWin32(lerr);
        }

        if (Offset >= (UINT64)FileSize.QuadPart)
        {
            return STATUS_SUCCESS;
        }

        if (Offset + Length > (UINT64)FileSize.QuadPart)
        {
            Length = (ULONG)((UINT64)FileSize.QuadPart - Offset);
        }
    }

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::WriteFile(ctx->mFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: WriteFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"PBytesTransferred=%lu", *PBytesTransferred);

    ctx->mFlags |= CSDCTX_FLAGS_WRITE;

    ntstatus = GetFileInfoInternal(ctx->mFile.handle(), FileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return FspNtStatusFromWin32(::GetLastError());
    }

    return STATUS_SUCCESS;
}

bool AwsS3::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

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
                const std::wstring localPath{ GetCacheFilePath(mCacheDataDir, fileObjKey.str()) };

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
                const auto num = deleteObjectCache(CONT_CALLER fileObjKey);
                //traceW(L"cache delete num=%d", num);
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

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    //traceW(L"cache delete num=%d", num);

    return true;
}

bool AwsS3::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR sourceFile /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());
    APP_ASSERT(!argObjKey.isBucket());

    traceW(L"argObjKey=%s, sourceFile=%s", argObjKey.c_str(), sourceFile);

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (sourceFile)
    {
        // ���[�J���E�L���b�V���̓��e���A�b�v���[�h����

        APP_ASSERT(argObjKey.meansFile());

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

        request.AddMetadata("wincse-creation-time", std::to_string(argFileInfo.CreationTime).c_str());
        //request.AddMetadata("wincse-last-access-time", std::to_string(argFileInfo.LastAccessTime).c_str());
        request.AddMetadata("wincse-last-write-time", std::to_string(argFileInfo.LastWriteTime).c_str());

#if _DEBUG
        request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime).c_str());
        //request.AddMetadata("wincse-debug-last-access-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastAccessTime).c_str());
        request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime).c_str());
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

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    //traceW(L"cache delete num=%d", num);

    return true;
}

// EOF