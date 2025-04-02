#include "AwsS3.hpp"

using namespace WinCseLib;

NTSTATUS AwsS3::prepareLocalFile(CALLER_ARG OpenContext* ctx)
{
    if (ctx->mFileInfo.FileSize <= PART_LENGTH_BYTE)
    {
        return this->prepareLocalFile_Simple(CONT_CALLER ctx);
    }
    else
    {
        return this->prepareLocalFile_Multipart(CONT_CALLER ctx);
    }
}

//
// GetObject() �Ŏ擾�������e���t�@�C���ɏo��
//
// argOffset)
//      -1 �ȉ�     �����o���I�t�Z�b�g�w��Ȃ�
//      ����ȊO    CreateFile ��� SetFilePointerEx �����s�����
//
static int64_t outputObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputParams& argOutputParams)
{
    NEW_LOG_BLOCK();

    // ���̓f�[�^
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // �t�@�C���T�C�Y

    std::vector<char> vbuffer(1024 * 64);

    traceW(argOutputParams.str().c_str());

    // result �̓��e���t�@�C���ɏo�͂���

    auto remainingTotal = inputSize;

    FileHandle hFile = ::CreateFileW
    (
        argOutputParams.mPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        argOutputParams.mCreationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile.invalid())
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%ld", lerr);

        return -1LL;
    }

    if (argOutputParams.mSpecifyRange)
    {
        LARGE_INTEGER li{};
        li.QuadPart = argOutputParams.mOffset;

        if (::SetFilePointerEx(hFile.handle(), li, NULL, FILE_BEGIN) == 0)
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFilePointerEx lerr=%ld", lerr);

            return -1LL;
        }
    }

    while (remainingTotal > 0)
    {
        // �o�b�t�@�Ƀf�[�^��ǂݍ���

        char* buffer = vbuffer.data();
        const std::streamsize bytesRead = pbuf->sgetn(buffer, min(remainingTotal, (int64_t)vbuffer.size()));
        if (bytesRead <= 0)
        {
            traceW(L"fault: Read error");

            return -1LL;
        }

        //traceW(L"%lld bytes read", bytesRead);

        // �t�@�C���Ƀf�[�^����������

        char* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            //traceW(L"%lld bytes remaining", remainingWrite);

            DWORD bytesWritten = 0;
            if (!::WriteFile(hFile.handle(), pos, (DWORD)remainingWrite, &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: WriteFile lerr=%ld", lerr);

                return -1LL;
            }

            //traceW(L"%lld bytes written", bytesWritten);

            pos += bytesWritten;
            remainingWrite -= bytesWritten;
        }

        remainingTotal -= bytesRead;
    }

    //traceW(L"return %lld", inputSize);

    return inputSize;
}

//
// �����Ŏw�肳�ꂽ���[�J���E�L���b�V�������݂��Ȃ��A���� �΂��� s3 �I�u�W�F�N�g��
// �X�V�������Â��ꍇ�͐V���� GetObject() �����s���ăL���b�V���E�t�@�C�����쐬����
// 
// argOffset)
//      -1 �ȉ�     �����o���I�t�Z�b�g�w��Ȃ�
//      ����ȊO    CreateFile ��� SetFilePointerEx �����s�����
//

int64_t AwsS3::getObjectAndWriteToFile(CALLER_ARG
    const ObjectKey& argObjKey, const FileOutputParams& argOutputParams)
{
    NEW_LOG_BLOCK();

    //traceW(L"argObjKey=%s meta=%s", argObjKey.c_str(), argOutputParams.str().c_str());

    std::stringstream ss;

    if (argOutputParams.mSpecifyRange)
    {
        // �I�t�Z�b�g�̎w�肪����Ƃ��͊����t�@�C���ւ�
        // �����������݂Ȃ̂� Length ���w�肳���ׂ��ł���

        ss << "bytes=";
        ss << argOutputParams.mOffset;
        ss << '-';
        ss << argOutputParams.getOffsetEnd();
    }

    const std::string range{ ss.str() };
    //traceA("range=%s", range.c_str());

    namespace chrono = std::chrono;
    const chrono::steady_clock::time_point start{ chrono::steady_clock::now() };

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (!range.empty())
    {
        request.SetRange(range);
    }

    const auto outcome = mClient->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetObject");
        return -1LL;
    }

    const auto& result = outcome.GetResult();

    // result �̓��e���t�@�C���ɏo�͂���

    const auto bytesWritten = outputObjectResultToFile(CONT_CALLER result, argOutputParams);

    if (bytesWritten < 0)
    {
        traceW(L"fault: outputObjectResultToFile");
        return -1LL;
    }

    const chrono::steady_clock::time_point end{ chrono::steady_clock::now() };
    const auto duration{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };

    //traceW(L"DOWNLOADTIME argObjKey=%s size=%lld duration=%lld", argObjKey.c_str(), bytesWritten, duration.count());

    return bytesWritten;
}

NTSTATUS syncFileAttributes(CALLER_ARG const FSP_FSCTL_FILE_INFO& remoteInfo,
    const std::wstring& localPath, bool* pNeedDownload)
{
    //
    // �����[�g�̃t�@�C�����������[�J���̃L���b�V���E�t�@�C���ɔ��f����
    // �_�E�����[�h���K�v�ȏꍇ�� pNeedDownload �ɂ��ʒm
    //
    NEW_LOG_BLOCK();
    APP_ASSERT(pNeedDownload);

    //traceW(L"argObjKey=%s localPath=%s", argObjKey.c_str(), localPath.c_str());
    //traceW(L"remoteInfo FileSize=%llu LastWriteTime=%llu", remoteInfo.FileSize, remoteInfo.LastWriteTime);
    //traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", remoteInfo.CreationTime, remoteInfo.LastWriteTime);

    FSP_FSCTL_FILE_INFO localInfo{};

    // 
    // * �p�^�[��
    //      �S�ē����ꍇ�͉������Ȃ�
    //      �قȂ��Ă�����̂�����ꍇ�͈ȉ��̕\�ɏ]��
    // 
    //                                      +-----------------------------------------+
    //				                        | �����[�g                                |
    //                                      +---------------------+-------------------+
    //				                        | �T�C�Y==0	          | �T�C�Y>0          |
    // ------------+------------+-----------+---------------------+-------------------+
    //	���[�J��   | ���݂���   | �T�C�Y==0 | �X�V�����𓯊�      | �_�E�����[�h      |
    //             |            +-----------+---------------------+-------------------+
    //			   |            | �T�C�Y>0  | �؂�l��            | �_�E�����[�h      |
    //             +------------+-----------+---------------------+-------------------+
    //		       | ���݂��Ȃ�	|	        | ��t�@�C���쐬      | �_�E�����[�h      |
    // ------------+------------+-----------+---------------------+-------------------+
    //
    bool syncTime = false;
    bool truncateFile = false;
    bool needDownload = false;
    DWORD lastError = ERROR_SUCCESS;
    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    FileHandle hFile = ::CreateFileW
    (
        localPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    lastError = ::GetLastError();

    if (hFile.valid())
    {
        //traceW(L"exists: local");

        // ���[�J���E�t�@�C�������݂���

        ntstatus = GetFileInfoInternal(hFile.handle(), &localInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return ntstatus;
        }

        //traceW(L"localInfo FileSize=%llu LastWriteTime=%llu", localInfo.FileSize, localInfo.LastWriteTime);
        //traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", localInfo.CreationTime, localInfo.LastWriteTime);

        if (remoteInfo.FileSize == localInfo.FileSize &&
            localInfo.CreationTime == remoteInfo.CreationTime &&
            localInfo.LastWriteTime == remoteInfo.LastWriteTime)
        {
            // --> �S�ē����Ȃ̂ŏ����s�v

            traceW(L"same file, skip, localPath=%s", localPath.c_str());
        }
        else
        {
            if (remoteInfo.FileSize == 0)
            {
                if (localInfo.FileSize == 0)
                {
                    // ���[�J�� == 0 : �����[�g == 0
                    // --> �X�V�����𓯊�

                    syncTime = true;
                }
                else
                {
                    // ���[�J�� > 0 : �����[�g == 0
                    // --> �؂�l��

                    truncateFile = true;
                }
            }
            else
            {
                // �����[�g > 0
                // --> �_�E�����[�h

                needDownload = true;
            }
        }
    }
    else
    {
        if (lastError != ERROR_FILE_NOT_FOUND)
        {
            // �z�肵�Ȃ��G���[

            traceW(L"fault: CreateFileW lerr=%lu", lastError);
            return FspNtStatusFromWin32(lastError);
        }

        //traceW(L"not exists: local");

        // ���[�J���E�t�@�C�������݂��Ȃ�

        if (remoteInfo.FileSize == 0)
        {
            // --> ��t�@�C���쐬

            truncateFile = true;
        }
        else
        {
            // --> �_�E�����[�h

            needDownload = true;
        }
    }

    traceW(L"syncRemoteTime=%s, truncateLocal=%s, needDownload=%s",
        BOOL_CSTRW(syncTime), BOOL_CSTRW(truncateFile), BOOL_CSTRW(needDownload));

    if (syncTime && truncateFile)
    {
        APP_ASSERT(0);
    }

    if (syncTime || truncateFile)
    {
        APP_ASSERT(!needDownload);

        hFile = ::CreateFileW
        (
            localPath.c_str(),
            FILE_WRITE_ATTRIBUTES, //GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            truncateFile ? CREATE_ALWAYS : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile.invalid())
        {
            lastError = ::GetLastError();
            traceW(L"fault: CreateFileW lerr=%lu", lastError);

            return FspNtStatusFromWin32(lastError);
        }

        // �X�V�����𓯊�

        //traceW(L"setFileTime");

        if (!hFile.setFileTime(remoteInfo.CreationTime, remoteInfo.LastWriteTime))
        {
            lastError = ::GetLastError();
            traceW(L"fault: setFileTime lerr=%lu", lastError);

            return FspNtStatusFromWin32(lastError);
        }

        ntstatus = GetFileInfoInternal(hFile.handle(), &localInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return ntstatus;
        }

        if (truncateFile)
        {
            traceW(L"truncate localPath=%s", localPath.c_str());
        }
        else
        {
            traceW(L"sync localPath=%s", localPath.c_str());
        }
    }

    if (!needDownload)
    {
        // �_�E�����[�h���s�v�ȏꍇ�́A���[�J���Ƀt�@�C�������݂����ԂɂȂ��Ă���͂�

        APP_ASSERT(hFile.valid());
        APP_ASSERT(localInfo.CreationTime);
    }

    *pNeedDownload = needDownload;

    return STATUS_SUCCESS;
}

// EOF