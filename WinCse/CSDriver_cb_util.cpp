#include "CSDriver.hpp"

using namespace CSELIB;


static FILEIO_LENGTH_T getFileSize(const std::filesystem::path& argPath)
{
    WIN32_FILE_ATTRIBUTE_DATA cacheFileInfo{};

    if (!::GetFileAttributesExW(argPath.c_str(), GetFileExInfoStandard, &cacheFileInfo))
    {
        return -1LL;
    }

    LARGE_INTEGER li{};
    li.HighPart = cacheFileInfo.nFileSizeHigh;
    li.LowPart = cacheFileInfo.nFileSizeLow;

    return li.QuadPart;
}

static bool syncFileTimes(HANDLE hFile, const FSP_FSCTL_FILE_INFO& fileInfo)
{
    NEW_LOG_BLOCK();

    FILETIME ftCreation;
    FILETIME ftLastAccess;
    FILETIME ftLastWrite;

    WinFileTime100nsToWinFile(fileInfo.CreationTime, &ftCreation);
    ::GetSystemTimeAsFileTime(&ftLastAccess);
    WinFileTime100nsToWinFile(fileInfo.LastWriteTime, &ftLastWrite);

    if (!::SetFileTime(hFile, &ftCreation, &ftLastAccess, &ftLastWrite))
    {
        traceW(L"fault: SetFileTime");
        return false;
    }

    traceW(L"ftCreation=%s",  WinFileTimeToLocalTimeStringW(ftCreation).c_str());
    traceW(L"ftLastAccess=%s", WinFileTimeToLocalTimeStringW(ftLastAccess).c_str());
    traceW(L"ftLastWrite=%s", WinFileTimeToLocalTimeStringW(ftLastWrite).c_str());

    return true;
}

class FilePart
{
    EventHandle mDone;
    CSELIB::FILEIO_LENGTH_T mResult = -1LL;

public:
    const CSELIB::FILEIO_OFFSET_T mOffset;
    const CSELIB::FILEIO_LENGTH_T mLength;

    std::atomic<bool> mInterrupt = false;

    explicit FilePart(CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) noexcept
        :
        mOffset(argOffset),
        mLength(argLength)
    {
        mDone = ::CreateEventW(NULL,
            TRUE,				// �蓮���Z�b�g�C�x���g
            FALSE,				// ������ԁF��V�O�i�����
            NULL);

        APP_ASSERT(mDone.valid());
    }

    HANDLE getEvent() noexcept
    {
        return mDone.handle();
    }

    void setResult(CSELIB::FILEIO_LENGTH_T argResult) noexcept
    {
        mResult = argResult;
        const auto b = ::SetEvent(mDone.handle());					// �V�O�i����Ԃɐݒ�
        APP_ASSERT(b);
    }

    CSELIB::FILEIO_LENGTH_T getResult() const noexcept
    {
        return mResult;
    }

    bool isError() const noexcept
    {
        return mResult < 0;
    }

    ~FilePart()
    {
        mDone.close();
    }
};

struct ReadPartTask : public IOnDemandTask
{
    ICSDevice* mDevice;
    const ObjectKey mObjKey;
    const std::filesystem::path mOutputPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(
        ICSDevice* argDevice,
        const ObjectKey& argObjKey,
        const std::filesystem::path& argOutputPath,
        std::shared_ptr<FilePart> argFilePart)
        :
        mDevice(argDevice),
        mObjKey(argObjKey),
        mOutputPath(argOutputPath),
        mFilePart(argFilePart)
    {
    }

    void run(int argThreadIndex) override
    {
        NEW_LOG_BLOCK();

        CSELIB::FILEIO_LENGTH_T readBytes = -1LL;

        try
        {
            if (mFilePart->mInterrupt)
            {
                traceW(L"@%d Interruption request received", argThreadIndex);
            }
            else
            {
                traceW(L"@%d getObjectAndWriteFile", argThreadIndex);

                readBytes = mDevice->getObjectAndWriteFile(START_CALLER mObjKey, mOutputPath, mFilePart->mOffset, mFilePart->mLength);
            }
        }
        catch (const std::exception& ex)
        {
            traceA("catch exception: what=[%s]", ex.what());
        }
        catch (...)
        {
            traceW(L"catch unknown");
        }

        // ���ʂ�ݒ肵�A�V�O�i����ԂɕύX
        // --> WaitForSingleObject �őҋ@���Ă���X���b�h�̃��b�N�����������

        mFilePart->setResult(readBytes);
    }

    void cancelled(CALLER_ARG0) noexcept
    {
        NEW_LOG_BLOCK();

        traceW(L"set Interrupt");

        mFilePart->mInterrupt = true;
    }
};

namespace CSEDRV
{

bool makeCacheFilePath(const std::filesystem::path& argDir, const std::wstring& argName, std::filesystem::path* pPath)
{
    if (!std::filesystem::is_directory(argDir))
    {
        return false;
    }

    std::wstring nameSha256;

    const auto ntstatus = ComputeSHA256W(argName, &nameSha256);
    if (!NT_SUCCESS(ntstatus))
    {
        return false;
    }

    // �擪�� 2Byte �̓f�B���N�g����

    auto filePath{ argDir / nameSha256.substr(0, 2) };

    std::error_code ec;
    std::filesystem::create_directory(filePath, ec);

    if (ec)
    {
        return false;
    }

    filePath.append(nameSha256.substr(2));

    *pPath = std::move(filePath);

    return true;
}

NTSTATUS updateFileInfo(HANDLE hFile, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    // �t�@�C���E�n���h���̏����擾

    FSP_FSCTL_FILE_INFO fileInfo;

    const auto ntstatus = GetFileInfoInternal(hFile, &fileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: WriteFile");
        return ntstatus;
    }

    if (fileInfo.FileSize < pFileInfo->FileSize)
    {
        // �_�E�����[�h����Ă��Ȃ�����������̂ŁA�T�C�Y�̓����[�g�̂��̂��̗p

        fileInfo.FileSize       = pFileInfo->FileSize;
        fileInfo.AllocationSize = pFileInfo->AllocationSize;
    }

    *pFileInfo = fileInfo;

    return STATUS_SUCCESS;
}

NTSTATUS syncAttributes(const std::filesystem::path& cacheFilePath, const DirInfoPtr& remoteDirInfo)
{
    NEW_LOG_BLOCK();

    FileHandle file = ::CreateFileW(
        cacheFilePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (file.invalid())
    {
        traceW(L"fault: CreateFileW");
        return FspNtStatusFromWin32(::GetLastError());
    }

    FSP_FSCTL_FILE_INFO localInfo;
    const auto ntstatus = GetFileInfoInternal(file.handle(), &localInfo);

    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return ntstatus;
    }

    if (localInfo.CreationTime  == remoteDirInfo->FileInfo.CreationTime &&
        localInfo.LastWriteTime == remoteDirInfo->FileInfo.LastWriteTime)
    {
        // ��̃^�C���X�^���v�������Ƃ��͓������ƍl����

        traceW(L"In sync");
    }
    else
    {
        // �^�C���X�^���v���قȂ�Ƃ��́A�_�E�����[�h�𑣂����߂Ƀt�@�C����؂�l�߂�

        if (localInfo.FileSize > 0)
        {
            // �t�@�C���|�C���^���ړ�
#if 0
            if (::SetFilePointer(file.handle(), 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
            {
                traceW(L"fault: SetFilePointer");
                return FspNtStatusFromWin32(::GetLastError());
            }
#endif

            // �t�@�C����؂�l�߂�

            if (!::SetEndOfFile(file.handle()))
            {
                traceW(L"fault: SetEndOfFile");
                return FspNtStatusFromWin32(::GetLastError());
            }
        }

        // �^�C���X�^���v�𓯊�

        if (!syncFileTimes(file.handle(), remoteDirInfo->FileInfo))
        {
            traceW(L"fault: syncFileTime");
            return FspNtStatusFromWin32(::GetLastError());
        }
    }

    return STATUS_SUCCESS;

}   // syncAttributes

NTSTATUS syncContent(CSDriver* that, FileContext* ctx, FILEIO_OFFSET_T argReadOffset, FILEIO_LENGTH_T argReadLength)
{
    NEW_LOG_BLOCK();

    if (ctx->mFileInfoRef->FileSize == 0)
    {
        traceW(L"Empty content");
        return STATUS_SUCCESS;
    }

    // �t�@�C���E�n���h�����烍�[�J���E�L���b�V���̃t�@�C�������擾

    std::filesystem::path cacheFilePath;

    if (!GetFileNameFromHandle(ctx->getHandle(), &cacheFilePath))
    {
        traceW(L"fault: GetFileNameFromHandle");
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"cacheFilePath=%s", cacheFilePath.c_str());

    // �L���b�V���E�t�@�C���̃T�C�Y���擾

    const auto cacheFileSize = getFileSize(cacheFilePath);
    if (cacheFileSize < 0)
    {
        traceW(L"fault: getFileSize");
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"cacheFileSize=%lld", cacheFileSize);

    // �����[�g�̑������ƃt�@�C���E�T�C�Y���r

    if (cacheFileSize >= (FILEIO_LENGTH_T)ctx->mFileInfoRef->FileSize)
    {
        // �S�ă_�E�����[�h�ςȂ̂� OK

        traceW(L"All content has been downloaded");
        return STATUS_SUCCESS;
    }

    // Read ����͈͂ƃt�@�C���E�T�C�Y���r

    const FILEIO_LENGTH_T fileSizeToRead = argReadOffset + argReadLength;
    if (cacheFileSize >= fileSizeToRead)
    {
        // Read �͈͂̃f�[�^�͑��݂���̂� OK

        traceW(L"Download not required");
        return STATUS_SUCCESS;
    }

    traceW(L"fileSizeToRead=%lld", fileSizeToRead);

    // Read �͈͂̃f�[�^���s�����Ă���̂Ń_�E�����[�h�����{

    //const auto BYTE_PART_SIZE = CSELIB::FILESIZE_1MiBll * that->mRuntimeEnv->TransferPerSizeMib;
    const auto BYTE_PART_SIZE = CSELIB::FILESIZE_1Bll   * 10;

    traceW(L"BYTE_PART_SIZE=%lld", BYTE_PART_SIZE);

    // �K�v�ƂȂ�t�@�C���E�T�C�Y������ɑ��݂���T�C�Y������
    // --> ���݂̃L���b�V���E�t�@�C���ɒǉ�����̂ŁA�����t�@�C���̃T�C�Y���J�n�_�ƂȂ�

    const auto requiredSizeBytes = min(fileSizeToRead, (FILEIO_LENGTH_T)ctx->mFileInfoRef->FileSize) - cacheFileSize;

    traceW(L"requiredSizeBytes=%lld", requiredSizeBytes);

    // �����擾����̈���쐬

    const auto numParts = (int)((requiredSizeBytes + BYTE_PART_SIZE - 1) / BYTE_PART_SIZE);

    traceW(L"numParts=%d", numParts);

    std::list<std::shared_ptr<FilePart>> fileParts;

    for (int i=0; i<numParts; i++)
    {
        // �����T�C�Y���Ƃ� FilePart ���쐬
        // ���̂Ƃ��A���ۂ̃t�@�C���E�T�C�Y���傫�Ȕ͈͂� SetRange �Ɏw�肷�邱�ƂɂȂ邪
        // ���X�|���X�����̂̓t�@�C���E�T�C�Y�܂łȂ̂Ŗ��͂Ȃ�

        const auto partOffset = cacheFileSize + BYTE_PART_SIZE * i;

        traceW(L"partOffset[%d]=%lld", i, partOffset);

        fileParts.emplace_back(std::make_shared<FilePart>(partOffset, (ULONG)BYTE_PART_SIZE));
    }

    // �}���`�p�[�g�̓ǂݍ��݂�x���^�X�N�ɓo�^

    auto* const worker = that->getWorker(L"delayed");

    for (auto& filePart: fileParts)
    {
        auto task{ new ReadPartTask{ that->mDevice, *ctx->mOptObjKey, cacheFilePath, filePart } };
        APP_ASSERT(task);

        worker->addTask(task);
    }

    // �^�X�N�̊�����ҋ@

    FILEIO_LENGTH_T sumReadBytes = 0;
    bool errorExists = false;

    for (auto& filePart: fileParts)
    {
        traceW(L"wait: mOffset=%lld", filePart->mOffset);

        const auto reason = ::WaitForSingleObject(filePart->getEvent(), INFINITE);
        APP_ASSERT(reason == WAIT_OBJECT_0);

        if (filePart->isError())
        {
            // �G���[������p�[�g�𔭌�

            traceW(L"isError: mOffset=%lld", filePart->mOffset);

            errorExists = true;
            break;
        }

        // �p�[�g���Ƃɓǂݎ�����T�C�Y���W�v

        const auto readBytes = filePart->getResult();

        traceW(L"readBytes=%lld", readBytes);

        sumReadBytes += readBytes;
    }

    if (errorExists)
    {
        // �}���`�p�[�g�̈ꕔ�ɃG���[�����݂����̂ŁA�S�Ă̒x���^�X�N�𒆒f���ďI��

        for (auto& filePart: fileParts)
        {
            // �S�Ẵp�[�g�ɒ��f�t���O�𗧂Ă�

            traceW(L"set mInterrupt mOffset=%lld", filePart->mOffset);

            filePart->mInterrupt = true;
        }

        for (auto& filePart: fileParts)
        {
            // �^�X�N�̊�����ҋ@

            const auto reason = ::WaitForSingleObject(filePart->getEvent(), INFINITE);
            APP_ASSERT(reason == WAIT_OBJECT_0);

            if (filePart->isError())
            {
                traceW(L"isError: mOffset=%lld result=%lld", filePart->mOffset, filePart->getResult());
            }
        }

        traceW(L"error exists");
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    // Read �͈͂𖞂����Ă��邩�`�F�b�N

    if (sumReadBytes < requiredSizeBytes)
    {
        traceW(L"The data is insufficient");
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    // �^�C���X�^���v�𓯊�

    FileHandle file = ::CreateFileW(
        cacheFilePath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        traceW(L"fault: CreateFileW");
        return FspNtStatusFromWin32(::GetLastError());
    }

    if (!syncFileTimes(file.handle(), *ctx->mFileInfoRef))
    {
        traceW(L"fault: syncFileTime");
        const auto lerr = ::GetLastError();
        return FspNtStatusFromWin32(lerr);
    }

    return STATUS_SUCCESS;

}   // syncContent

}   // namespace CSEDRV

// EOF