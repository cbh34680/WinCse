#include "CSDriver.hpp"

using namespace CSELIB;

static bool syncFileTimes(const FSP_FSCTL_FILE_INFO& fileInfo, HANDLE hFile)
{
    FILETIME ftCreation;
    FILETIME ftLastAccess;
    FILETIME ftLastWrite;

    WinFileTime100nsToWinFile(fileInfo.CreationTime, &ftCreation);
    ::GetSystemTimeAsFileTime(&ftLastAccess);
    WinFileTime100nsToWinFile(fileInfo.LastWriteTime, &ftLastWrite);

    return ::SetFileTime(hFile, &ftCreation, &ftLastAccess, &ftLastWrite);
}

using ReadFilePartType = CSELIB::FilePart<FILEIO_LENGTH_T>;

struct ReadFilePartTask : public IOnDemandTask
{
    ICSDevice* mThat;
    const ObjectKey mObjKey;
    const std::filesystem::path mOutputPath;
    std::shared_ptr<ReadFilePartType> mFilePart;

    ReadFilePartTask(
        ICSDevice* argThat,
        const ObjectKey& argObjKey,
        const std::filesystem::path& argOutputPath,
        const std::shared_ptr<ReadFilePartType>& argFilePart)
        :
        mThat(argThat),
        mObjKey(argObjKey),
        mOutputPath(argOutputPath),
        mFilePart(argFilePart)
    {
    }

    void run(int argThreadIndex) override
    {
        NEW_LOG_BLOCK();

        FILEIO_LENGTH_T result = -1LL;

        try
        {
            if (mFilePart->mInterrupt)
            {
                errorW(L"@%d Interruption request received", argThreadIndex);
            }
            else
            {
                traceW(L"@%d getObjectAndWriteFile", argThreadIndex);

                result = mThat->getObjectAndWriteFile(START_CALLER mObjKey, mOutputPath, mFilePart->mOffset, mFilePart->mLength);
            }
        }
        catch (const std::exception& ex)
        {
            errorA("catch exception: what=[%s]", ex.what());
        }
        catch (...)
        {
            errorW(L"catch unknown");
        }

        // ���ʂ�ݒ肵�A�V�O�i����ԂɕύX
        // --> WaitForSingleObject �őҋ@���Ă���X���b�h�̃��b�N�����������

        mFilePart->setResult(result);
    }
};

namespace CSEDRV {

bool resolveCacheFilePath(const std::filesystem::path& argDir, const std::wstring& argWinPath, std::filesystem::path* pPath)
{
    NEW_LOG_BLOCK();

    if (!std::filesystem::is_directory(argDir))
    {
        errorW(L"fault: is_directory argDir=%s", argDir.c_str());
        return false;
    }

    std::wstring nameSha256;

    const auto ntstatus = ComputeSHA256W(argWinPath, &nameSha256);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: ComputeSHA256W argWinPath=%s", argWinPath.c_str());
        return false;
    }

    // �擪�� 2Byte �̓f�B���N�g����

    auto filePath{ argDir / SafeSubStringW(nameSha256, 0, 2) };

    std::error_code ec;
    std::filesystem::create_directory(filePath, ec);

    if (ec)
    {
        errorW(L"fault: create_directory filePath=%s", filePath.c_str());
        return false;
    }

    filePath.append(SafeSubStringW(nameSha256, 2));

    *pPath = std::move(filePath);

    return true;
}

NTSTATUS syncAttributes(const DirEntryType& remoteDirEntry, const std::filesystem::path& cacheFilePath)
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
        const auto lerr = ::GetLastError();

        errorW(L"fault: CreateFileW lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
        return FspNtStatusFromWin32(lerr);
    }

    FSP_FSCTL_FILE_INFO localInfo;
    const auto ntstatus = GetFileInfoInternal(file.handle(), &localInfo);

    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: GetFileInfoInternal file=%s", file.str().c_str());
        return ntstatus;
    }

    if (localInfo.CreationTime  == remoteDirEntry->mFileInfo.CreationTime &&
        localInfo.LastWriteTime == remoteDirEntry->mFileInfo.LastWriteTime)
    {
        // ��̃^�C���X�^���v�������Ƃ��͓������ƍl����

        traceW(L"In sync");
    }
    else
    {
        // �^�C���X�^���v���قȂ�Ƃ��́A�_�E�����[�h�𑣂����߂Ƀt�@�C����؂�l�߂�

        if (localInfo.FileSize > 0)
        {
            // �t�@�C���|�C���^��擪�Ɉړ� (�K�v�Ȃ����ǁA�O�̂���)

            if (::SetFilePointer(file.handle(), 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
            {
                const auto lerr = ::GetLastError();

                errorW(L"fault: SetFilePointer lerr=%lu file=%s", lerr, file.str().c_str());
                return FspNtStatusFromWin32(lerr);
            }

            // �t�@�C����؂�l�߂�

            if (!::SetEndOfFile(file.handle()))
            {
                const auto lerr = ::GetLastError();

                errorW(L"fault: SetEndOfFile lerr=%lu file=%s", lerr, file.str().c_str());
                return FspNtStatusFromWin32(lerr);
            }
        }

        // �^�C���X�^���v�𓯊�

        if (!syncFileTimes(remoteDirEntry->mFileInfo, file.handle()))
        {
            const auto lerr = ::GetLastError();

            errorW(L"fault: syncFileTime lerr=%lu file=%s", lerr, file.str().c_str());
            return FspNtStatusFromWin32(lerr);
        }
    }

    return STATUS_SUCCESS;

}   // syncAttributes

NTSTATUS CSDriver::updateFileInfo(CALLER_ARG FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo, bool argRemoteSizeAware)
{
    NEW_LOG_BLOCK();

    // �L���b�V���t�@�C���̏����擾

    FSP_FSCTL_FILE_INFO cacheFileInfo;

    const auto ntstatus = GetFileInfoInternal(ctx->getWritableHandle(), &cacheFileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: GetFileInfoInternal ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    const auto& dirEntry{ ctx->getDirEntry() };

    if (argRemoteSizeAware)
    {
        if (cacheFileInfo.FileSize < dirEntry->mFileInfo.FileSize)
        {
            // �_�E�����[�h����Ă��Ȃ�����������̂ŁA�T�C�Y�̓����[�g�̏����㏑��

            cacheFileInfo.FileSize       = dirEntry->mFileInfo.FileSize;
            cacheFileInfo.AllocationSize = dirEntry->mFileInfo.AllocationSize;
        }
    }

    // ctx ���o�R���AOpenDirEntry �� mFileInfo ���X�V����

    dirEntry->mFileInfo = cacheFileInfo;
    *pFileInfo          = cacheFileInfo;

    return STATUS_SUCCESS;
}

NTSTATUS CSDriver::syncContent(CALLER_ARG FileContext* ctx, FILEIO_OFFSET_T argReadOffset, FILEIO_LENGTH_T argReadLength)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s argReadOffset=%lld argReadLength=%lld", ctx->str().c_str(), argReadOffset, argReadLength);

    if (argReadLength == 0)
    {
        traceW(L"Empty read");
        return STATUS_SUCCESS;
    }

    const auto& fileInfo{ ctx->getDirEntry()->mFileInfo };

    if (fileInfo.FileSize == 0)
    {
        traceW(L"Empty content");
        return STATUS_SUCCESS;
    }

    // �t�@�C���E�n���h�����烍�[�J���̃t�@�C�������擾

    std::filesystem::path filePath;

    if (!GetFileNameFromHandle(ctx->getHandle(), &filePath))
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: GetFileNameFromHandle lerr=%lu", lerr);
        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"filePath=%s", filePath.c_str());

    // �t�@�C���T�C�Y���擾

    const auto fileSize = GetFileSize(filePath);
    if (fileSize < 0)
    {
        errorW(L"fault: getFileSize");
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"fileSize=%lld", fileSize);

    // �t�@�C���T�C�Y�ƃ����[�g�̑��������r

    if (fileSize >= (FILESIZE_T)fileInfo.FileSize)
    {
        // �S�ă_�E�����[�h�ςȂ̂� OK
        // 
        // --> �t�@�C����؂�l�߂��ꍇ�̓f�B���N�g���G���g�����ύX���Ă���

        traceW(L"All content has been downloaded");
        return STATUS_SUCCESS;
    }

    // �t�@�C���E�T�C�Y�� Read �Ώ۔͈͂��r

    FILEIO_LENGTH_T fileSizeToRead = argReadOffset + argReadLength;

    if (fileSize >= fileSizeToRead)
    {
        // Read �͈͂̃f�[�^�͑��݂���̂� OK

        traceW(L"Download not required");
        return STATUS_SUCCESS;
    }

    traceW(L"fileSizeToRead=%lld", fileSizeToRead);

    // Read �͈͂̃f�[�^���s�����Ă���̂Ń_�E�����[�h�����{

    APP_ASSERT(fileSize < static_cast<FILESIZE_T>(fileInfo.FileSize));
    APP_ASSERT(fileSize < fileSizeToRead);

    // �}���`�p�[�g�����̂̃p�[�g�T�C�Y

#if 0
    traceW(L"!!");
    traceW(L"!! WARNING: PART SIZE !!");
    traceW(L"!!");

    const auto PART_SIZE_BYTE = ILESIZE_1Bll * 10;

#else
    auto PART_SIZE_BYTE = FILESIZE_1MiBll * mRuntimeEnv->TransferReadSizeMib;

    if (argReadOffset == 0 && fileSizeToRead <= FILESIZE_1MiBll)
    {
        // �G�N�X�v���[���Ńv���p�e�B���J���ƃ��^�f�[�^���ǂݎ���邱�ƂɑΉ�
        // --> �擪�� 1MiB �܂ł� Read �̏ꍇ�̓p�[�g�T�C�Y�� 1MiB �ɐݒ�

        PART_SIZE_BYTE = FILESIZE_1MiBll;
    }

#endif
    traceW(L"PART_SIZE_BYTE=%lld", PART_SIZE_BYTE);

    const auto& objKey{ ctx->getObjectKey() };

    // �A���C�����g�T�C�Y�ɒ���

    auto alignedFileSizeToRead = ALIGN_TO_UNIT(fileSizeToRead, PART_SIZE_BYTE);

    if (static_cast<FILESIZE_T>(fileInfo.FileSize) < alignedFileSizeToRead)
    {
        // �����[�g�̃T�C�Y�� Read �Ώۂ̏���Ƃ���

        alignedFileSizeToRead = fileInfo.FileSize;
    }

    APP_ASSERT(alignedFileSizeToRead <= static_cast<FILESIZE_T>(fileInfo.FileSize));

    // �e�ϐ��̒l�̊֌W��
    // 
    // [fileSize] < [fileSizeToRead] <= [alignedFileSizeToRead] <= [fileInfo.FileSize]

    // �K�v�ƂȂ�T�C�Y

    const auto requiredSizeBytes = alignedFileSizeToRead - fileSize;

    traceW(L"requiredSizeBytes=%lld", requiredSizeBytes);

    // �����擾����̈���쐬

    const auto partCount = UNIT_COUNT(requiredSizeBytes, PART_SIZE_BYTE);

    std::list<std::shared_ptr<ReadFilePartType>> fileParts;

    auto remaining = requiredSizeBytes;
        
    for (int i=0; i<partCount; i++)
    {
        // �����T�C�Y���Ƃ� FilePart ���쐬

        const auto partNumber = i + 1;
        const auto partOffset = fileSize + PART_SIZE_BYTE * i;
        const auto partLength = min(PART_SIZE_BYTE, remaining);

        fileParts.emplace_back(std::make_shared<ReadFilePartType>(partNumber, partOffset, partLength, -1LL));

        remaining -= partLength;
    }

    if (fileParts.size() == 1)
    {
        const auto& filePart{ *fileParts.begin() };

        // ��x�őS�ēǂ߂Ă��܂��̂ŕ��G�Ȃ��Ƃ͂��Ȃ�

        const auto readBytes = mDevice->getObjectAndWriteFile(START_CALLER objKey, filePath, filePart->mOffset, filePart->mLength);

        if (filePart->mLength != readBytes)
        {
            errorW(L"fault: getObjectAndWriteFile mLength=%lld readBytes=%lld", filePart->mLength, readBytes);

            return FspNtStatusFromWin32(ERROR_IO_DEVICE);
        }
    }
    else
    {
        // �}���`�p�[�g�̓ǂݍ��݂�x���^�X�N�ɓo�^

        auto* const worker = this->getWorker(L"delayed");

        for (const auto& filePart: fileParts)
        {
            traceW(L"addTask filePart=%s", filePart->str().c_str());

            worker->addTask(new ReadFilePartTask{ mDevice, objKey, filePath, filePart });
        }

        // �^�X�N�̊�����ҋ@

        FILEIO_LENGTH_T sumReadBytes = 0;

        for (const auto& filePart: fileParts)
        {
            // �p�[�g���Ƃɓǂݎ�����T�C�Y���W�v

            const auto result = filePart->getResult();

            traceW(L"getResult filePart=%s result=%lld", filePart->str().c_str(), result);

            if (result != filePart->mLength)
            {
                sumReadBytes = -1LL;

                errorW(L"fault: mPartNumber=%d", filePart->mPartNumber);
                break;
            }

            sumReadBytes += result;
        }

        if (sumReadBytes < requiredSizeBytes)
        {
            // �}���`�p�[�g�̈ꕔ�ɃG���[�����݂����̂ŁA�S�Ă̒x���^�X�N�𒆒f���ďI��

            errorW(L"The data is insufficient sumReadBytes=%lld", sumReadBytes);

            for (auto& filePart: fileParts)
            {
                // �S�Ẵp�[�g�ɒ��f�t���O�𗧂Ă�

                traceW(L"set mInterrupt mPartNumber=%lld", filePart->mPartNumber);

                filePart->mInterrupt = true;
            }

            for (auto& filePart: fileParts)
            {
                // �^�X�N�̊�����ҋ@

                const auto result = filePart->getResult();
                if (result != filePart->mLength)
                {
                    errorW(L"fault: mPartNumber=%d", filePart->mPartNumber);
                }
            }

            traceW(L"error exists");
            return FspNtStatusFromWin32(ERROR_IO_DEVICE);
        }
    }

    // �^�C���X�^���v�𓯊�

    FileHandle file = ::CreateFileW(
        filePath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: CreateFileW lerr=%lu filePath=%s", lerr, filePath.c_str());
        return FspNtStatusFromWin32(lerr);
    }

    if (!syncFileTimes(fileInfo, file.handle()))
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: syncFileTime file=%s", file.str().c_str());
        return FspNtStatusFromWin32(lerr);
    }

    return STATUS_SUCCESS;

}   // syncContent

}   // namespace CSEDRV

// EOF