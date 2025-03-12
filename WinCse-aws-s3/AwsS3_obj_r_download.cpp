#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


//
// GetObject() �Ŏ擾�������e���t�@�C���ɏo��
//
// argOffset)
//      -1 �ȉ�     �����o���I�t�Z�b�g�w��Ȃ�
//      ����ȊO    CreateFile ��� SetFilePointerEx �����s�����
//
static int64_t writeObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputMeta& argMeta)
{
    NEW_LOG_BLOCK();

    int64_t ret = -1LL;

    // ���̓f�[�^
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // �t�@�C���T�C�Y

    std::vector<char> vbuffer(1024 * 64);

    traceW(argMeta.str().c_str());

    // result �̓��e���t�@�C���ɏo�͂���
    HANDLE hFile = INVALID_HANDLE_VALUE;

    auto remainingTotal = inputSize;

    hFile = ::CreateFileW(argMeta.mPath.c_str(),
        GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, argMeta.mCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%ld", lerr);
        goto exit;
    }

    if (argMeta.mSpecifyRange)
    {
        LARGE_INTEGER li{};
        li.QuadPart = argMeta.mOffset;

        if (::SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) == 0)
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFilePointerEx lerr=%ld", lerr);
            goto exit;
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
            goto exit;
        }

        traceW(L"%lld bytes read", bytesRead);

        // �t�@�C���Ƀf�[�^����������

        char* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            traceW(L"%lld bytes remaining", remainingWrite);

            DWORD bytesWritten = 0;
            if (!::WriteFile(hFile, pos, (DWORD)remainingWrite, &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: WriteFile lerr=%ld", lerr);
                goto exit;
            }

            traceW(L"%lld bytes written", bytesWritten);

            pos += bytesWritten;
            remainingWrite -= bytesWritten;
        }

        remainingTotal -= bytesRead;
    }

    if (argMeta.mSetFileTime)
    {
        // �^�C���X�^���v���X�V

        const auto lastModified = argResult.GetLastModified().Millis();

        FILETIME ft;
        UtcMillisToWinFileTime(lastModified, &ft);

        FILETIME ftNow;
        ::GetSystemTimeAsFileTime(&ftNow);

        if (!::SetFileTime(hFile, &ft, &ftNow, &ft))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFileTime lerr=%ld", lerr);
            return false;
        }
    }

    ret = inputSize;

exit:
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

    traceW(L"return %lld", ret);

    return ret;
}

//
// �����Ŏw�肳�ꂽ���[�J���E�L���b�V�������݂��Ȃ��A���� �΂��� s3 �I�u�W�F�N�g��
// �X�V�������Â��ꍇ�͐V���� GetObject() �����s���ăL���b�V���E�t�@�C�����쐬����
// 
// argOffset)
//      -1 �ȉ�     �����o���I�t�Z�b�g�w��Ȃ�
//      ����ȊO    CreateFile ��� SetFilePointerEx �����s�����
//
int64_t AwsS3::prepareLocalCacheFile(CALLER_ARG
    const ObjectKey& argObjKey, const FileOutputMeta& argMeta)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s meta=%s", argObjKey.c_str(), argMeta.str().c_str());

    std::stringstream ss;

    if (argMeta.mSpecifyRange)
    {
        // �I�t�Z�b�g�̎w�肪����Ƃ��͊����t�@�C���ւ�
        // �����������݂Ȃ̂� Length ���w�肳���ׂ��ł���

        ss << "bytes=";
        ss << argMeta.mOffset;
        ss << '-';
        ss << argMeta.getOffsetEnd();
    }

    const std::string range{ ss.str() };
    traceA("range=%s", range.c_str());

    namespace chrono = std::chrono;
    const chrono::steady_clock::time_point start{ chrono::steady_clock::now() };

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (!range.empty())
    {
        request.SetRange(range);
    }

    const auto outcome = mClient.ptr->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetObject");
        return -1LL;
    }

    const auto& result = outcome.GetResult();

    // result �̓��e���t�@�C���ɏo�͂���

    const auto bytesWritten = writeObjectResultToFile(CONT_CALLER result, argMeta);

    if (bytesWritten < 0)
    {
        traceW(L"fault: writeObjectResultToFile");
        return -1LL;
    }

    const chrono::steady_clock::time_point end{ chrono::steady_clock::now() };
    const auto duration{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };

    traceW(L"DOWNLOADTIME argObjKey=%s size=%lld duration=%lld",
        argObjKey.c_str(), bytesWritten, duration.count());

    return bytesWritten;
}

bool AwsS3::shouldDownload(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& remote, const std::wstring& localPath, bool* pNeedDownload)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pNeedDownload);

    traceW(L"argObjKey=%s localPath=%s", argObjKey.c_str(), localPath.c_str());

    bool ret = false;
    bool needDownload = false;

    FSP_FSCTL_FILE_INFO local{};

    if (std::filesystem::exists(localPath))
    {
        // �L���b�V���E�t�@�C�������݂���

        if (!std::filesystem::is_regular_file(localPath))
        {
            traceW(L"fault: is_regular_file");
            goto exit;
        }

        // ���[�J���E�L���b�V���̑��������擾

        if (!PathToFileInfo(localPath, &local))
        {
            traceW(L"fault: PathToFileInfo");
            goto exit;
        }

        traceW(L"LOCAL: size=%llu create=%s write=%s access=%s",
            local.FileSize,
            WinFileTime100nsToLocalTimeStringW(local.CreationTime).c_str(),
            WinFileTime100nsToLocalTimeStringW(local.LastWriteTime).c_str(),
            WinFileTime100nsToLocalTimeStringW(local.LastAccessTime).c_str()
        );

        traceW(L"REMOTE: size=%llu create=%s",
            remote.FileSize,
            WinFileTime100nsToLocalTimeStringW(remote.CreationTime).c_str());

        // ���[�J���E�t�@�C���̍X�V�����Ɣ�r

        // TODO:
        // 
        // !!! ���� !!! �X�V�n�̏������������Ƃ��ɂ͉��߂čl�������K�v������
        //
        if (remote.CreationTime > local.CreationTime)
        {
            // �����[�g�E�t�@�C�����X�V����Ă���̂ōĎ擾

            traceW(L"remote file changed");
            needDownload = true;
        }

        //
        // �ȑO�̃L���b�V���쐬���ɃG���[�ƂȂ��Ă������蒼�����K�v
        //
        if (remote.FileSize != local.FileSize)
        {
            traceW(L"filesize unmatch remote=%llu local=%llu", remote.FileSize, local.FileSize);
            needDownload = true;
        }
    }
    else
    {
        // �L���b�V���E�t�@�C�������݂��Ȃ�

        traceW(L"no cache file");
        needDownload = true;
    }

    ret = true;
    *pNeedDownload = needDownload;

exit:
    return ret;
}

// EOF