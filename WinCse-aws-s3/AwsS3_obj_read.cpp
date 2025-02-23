#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <filesystem>
#include <fstream>
#include <cinttypes>


using namespace WinCseLib;


//
// GetObject() �Ŏ擾�������e���t�@�C���ɏo��
//
static bool writeObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& result, const wchar_t* path)
{
    NEW_LOG_BLOCK();

    bool ret = false;

    const auto pbuf = result.GetBody().rdbuf();

    // �t�@�C���T�C�Y
    auto fileSize = result.GetContentLength();

    // �X�V����
    const auto lastModified = result.GetLastModified().Millis();

    FILETIME ft;
    UtcMillisToWinFileTime(lastModified, &ft);

    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);

    traceW(L"CreateFile: path=%s", path);

    // result �̓��e���t�@�C���ɏo�͂���
    HANDLE hFile = ::CreateFileW(path,
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        traceW(L"fault: CreateFileW");
        goto exit;
    }

    while (fileSize > 0)
    {
        DWORD bytesWritten = 0;
        char buffer[4096] = {};

        // �o�b�t�@�Ƀf�[�^��ǂݍ���
        std::streamsize bytesRead = pbuf->sgetn(buffer, min(fileSize, sizeof(buffer)));

        // �t�@�C���Ƀf�[�^����������
        if (!::WriteFile(hFile, buffer, (DWORD)bytesRead, &bytesWritten, NULL))
        {
            traceW(L"fault: WriteFile");
            goto exit;
        }

        fileSize -= bytesRead;
    }

    if (!::SetFileTime(hFile, &ft, &ftNow, &ft))
    {
        traceW(L"fault: SetFileTime");
        goto exit;
    }

    ::CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    ret = true;

exit:
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hFile);
    }

    traceW(L"return %s", ret ? L"true" : L"false");

    return ret;
}

//
// �����Ŏw�肳�ꂽ���[�J���E�L���b�V�������݂��Ȃ��A���� �΂��� s3 �I�u�W�F�N�g��
// �X�V�������Â��ꍇ�͐V���� GetObject() �����s���ăL���b�V���E�t�@�C�����쐬����
//
bool AwsS3::prepareLocalCacheFile(CALLER_ARG const std::wstring& bucket, const std::wstring& key, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();

    bool needGet = false;

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(localPath, ec);

    if (ec)
    {
        // ���[�J���ɃL���b�V���E�t�@�C�������݂��Ȃ�
        APP_ASSERT(!std::filesystem::exists(localPath));

        traceW(L"no local cache");
        needGet = true;
    }
    else
    {
        // ���[�J���ɃL���b�V���E�t�@�C�������݂���
        APP_ASSERT(std::filesystem::is_regular_file(localPath));

        // ���[�J���E�L���b�V���̍쐬�������擾

        FILETIME ftCreate = {};
        if (!HandleToWinFileTimes(localPath, &ftCreate, NULL, NULL))
        {
            traceW(L"fault: HandleToWinFileTimes");
            return false;
        }

        const auto createMillis = WinFileTimeIn100ns(ftCreate);

        // ctx->fileInfo �̓��e�͌Â��\���������̂ŁA���߂� HeadObject �����s����

        const auto dirInfo = unsafeHeadObject(CONT_CALLER bucket, key);
        if (!dirInfo)
        {
            traceW(L"fault: unsafeHeadObject");
            return false;
        }

        // �t�@�C���̃I�[�v�������ł���A��r����Εp�x�͒Ⴂ�͂��Ȃ̂ŃL���b�V���ւ̕ۑ��͂��Ȃ�
        // (���b�N���K�v�ƂȂ�ʓ|�Ȃ̂�)

        // ���[�J���E�t�@�C���̍X�V�����Ɣ�r

        //
        // !!! ���� !!! �X�V�n�̏������������Ƃ��ɂ͉��߂čl�������K�v������
        //

        traceW(L"compare: REMOTE=%" PRIu64 " LOCAL=%" PRIu64, dirInfo->FileInfo.CreationTime, createMillis);

        if (dirInfo->FileInfo.CreationTime > createMillis)
        {
            // �����[�g�E�t�@�C�����X�V����Ă���̂ōĎ擾

            traceW(L"detected update remote file");
            needGet = true;
        }
    }

    traceW(L"needGet: %s", needGet ? L"true" : L"false");

    if (needGet)
    {
        traceW(L"create or update cache-file: %s", localPath.c_str());

        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(WC2MB(bucket).c_str());
        request.SetKey(WC2MB(key).c_str());

        const auto outcome = mClient.ptr->GetObject(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: GetObject");
            return false;
        }

        const auto& result = outcome.GetResult();

        // result �̓��e���t�@�C���ɏo�͂��A�^�C���X�^���v��ύX����

        if (!writeObjectResultToFile(CONT_CALLER result, localPath.c_str()))
        {
            traceW(L"fault: writeObjectResultToFile");
            return false;
        }

        //::Sleep(10 * 1000);

        traceW(L"cache-file written done.");
    }

    return true;
}

//
// openFIle() ���Ă΂ꂽ�Ƃ��� CSData �Ƃ��� PTFS_FILE_CONTEXT �ɕۑ�����������
// closeFile() �ō폜�����
//
struct ReadFileContext
{
    std::wstring bucket;
    std::wstring key;
    UINT32 createOptions;
    UINT32 grantedAccess;
    FSP_FSCTL_FILE_INFO fileInfo;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    ReadFileContext(
        const std::wstring& argBucket, const std::wstring& argKey,
        const UINT32 argCreateOptions, const UINT32 argGrantedAccess,
        const FSP_FSCTL_FILE_INFO& argFileInfo) :
        bucket(argBucket), key(argKey), createOptions(argCreateOptions),
        grantedAccess(argGrantedAccess), fileInfo(argFileInfo)
    {
    }

    ~ReadFileContext()
    {
        if (hFile != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(hFile);
        }
    }
};

bool AwsS3::openFile(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    UINT32 CreateOptions, UINT32 GrantedAccess,
    const FSP_FSCTL_FILE_INFO& fileInfo, 
    PVOID* pCSData)
{
    // DoOpen() ����Ăяo����邪�A�t�@�C�����J��=�_�E�����[�h�ɂȂ��Ă��܂�����
    // �����ł� CSData �ɏ��݂̂�ۑ����ADoRead() ����Ăяo����� readFile() ��
    // �t�@�C���̃_�E�����[�h���� (�L���b�V���E�t�@�C��) ���s���B

    ReadFileContext* ctx = new ReadFileContext{ argBucket, argKey, CreateOptions, GrantedAccess, fileInfo };
    APP_ASSERT(ctx);

    *pCSData = (PVOID*)ctx;

    return true;
}

void AwsS3::closeFile(CALLER_ARG PVOID CSData)
{
    APP_ASSERT(CSData);

    ReadFileContext* ctx = (ReadFileContext*)CSData;

    delete ctx;
}

//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
bool AwsS3::readFile(CALLER_ARG PVOID CSData,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(CSData);

    ReadFileContext* ctx = (ReadFileContext*)CSData;

    APP_ASSERT(!ctx->bucket.empty());
    APP_ASSERT(!ctx->key.empty());
    APP_ASSERT(ctx->key.back() != L'/');

    traceW(L"success: HANDLE=%p, Offset=%" PRIu64 " Length=%ul", ctx->hFile, Offset, Length);

    bool ret = false;
    OVERLAPPED Overlapped = { };

    if (ctx->hFile == INVALID_HANDLE_VALUE)
    {
        // openFile() ��̏���̌Ăяo��

        const std::wstring localPath{ mCacheDir + L'\\' + EncodeFileNameToLocalNameW(ctx->bucket + L'/' + ctx->key) };

        // �L���b�V���E�t�@�C���̏���

        if (!prepareLocalCacheFile(CONT_CALLER ctx->bucket, ctx->key, localPath))
        {
            traceW(L"fault: prepareLocalCacheFile");
            goto exit;
        }

        APP_ASSERT(std::filesystem::exists(localPath));

        // �L���b�V���E�t�@�C�����J���AHANDLE ���R���e�L�X�g�ɕۑ�

        ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
        if (ctx->createOptions & FILE_DELETE_ON_CLOSE)
            CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

        HANDLE hFile = ::CreateFileW(localPath.c_str(),
            ctx->grantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
            OPEN_EXISTING, CreateFlags, 0);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            //return FspNtStatusFromWin32(GetLastError());
            traceW(L"fault: CreateFileW");
            goto exit;
        }

        ctx->hFile = hFile;
    }

    APP_ASSERT(ctx->hFile);
    APP_ASSERT(ctx->hFile != INVALID_HANDLE_VALUE);

    // Offset, Length �ɂ��t�@�C����ǂ�

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->hFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        //return FspNtStatusFromWin32(GetLastError());
        traceW(L"fault: ReadFile");
        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%" PRIu64 " Length=%ul, PBytesTransferred=%ul",
        ctx->hFile, Offset, Length, *PBytesTransferred);

    ret = true;

exit:
    if (!ret)
    {
        ::CloseHandle(ctx->hFile);
        ctx->hFile = INVALID_HANDLE_VALUE;
    }

    return ret;
}

// EOF