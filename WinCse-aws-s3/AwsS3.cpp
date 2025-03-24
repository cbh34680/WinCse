#include "AwsS3.hpp"
#include <filesystem>

using namespace WinCseLib;

//
// AwsS3
//
WinCseLib::ICSDevice* NewCSDevice(
    const wchar_t* argTempDir, const wchar_t* argIniSection,
    NamedWorker argWorkers[])
{
    return new AwsS3(argTempDir, argIniSection, argWorkers);
}

AwsS3::AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection, NamedWorker argWorkers[])
    :
    mTempDir(argTempDir), mIniSection(argIniSection)
{
    APP_ASSERT(std::filesystem::exists(argTempDir));
    APP_ASSERT(std::filesystem::is_directory(argTempDir));

    NamedWorkersToMap(argWorkers, &mWorkers);

    mStats = &mStats_;
}

AwsS3::~AwsS3()
{
    NEW_LOG_BLOCK();

    this->OnSvcStop();

    // �K�v�Ȃ����A�f�o�b�O���̃������E���[�N�����̎ז��ɂȂ�̂�

    clearBuckets(START_CALLER0);
    clearObjects(START_CALLER0);

    mRefFile.close();
    mRefDir.close();
}

bool AwsS3::isInBucketFilters(const std::wstring& arg)
{
    if (mBucketFilters.empty())
    {
        return true;
    }

    const auto it = std::find_if(mBucketFilters.begin(), mBucketFilters.end(), [&arg](const auto& re)
    {
        return std::regex_match(arg, re);
    });

    return it != mBucketFilters.end();
}

DirInfoType AwsS3::makeDirInfo_attr(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime, const UINT32 argFileAttributes)
{
    auto dirInfo = makeDirInfo(argObjKey);
    APP_ASSERT(dirInfo);

    UINT32 fileAttributes = argFileAttributes | mDefaultFileAttributes;

    if (argObjKey.meansHidden())
    {
        // �B���t�@�C��

        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    dirInfo->FileInfo.FileAttributes = fileAttributes;

    dirInfo->FileInfo.CreationTime = argFileTime;
    dirInfo->FileInfo.LastAccessTime = argFileTime;
    dirInfo->FileInfo.LastWriteTime = argFileTime;
    dirInfo->FileInfo.ChangeTime = argFileTime;

    return dirInfo;
}

DirInfoType AwsS3::makeDirInfo_byName(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime)
{
    APP_ASSERT(argObjKey.valid());

    return makeDirInfo_attr(argObjKey, argFileTime, argObjKey.meansFile() ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DIRECTORY);
}

DirInfoType AwsS3::makeDirInfo_dir(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime)
{
    return makeDirInfo_attr(argObjKey, argFileTime, FILE_ATTRIBUTE_DIRECTORY);
}

HANDLE AwsS3::HandleFromContext(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    const auto remotePath{ ctx->getRemotePath() };

    traceW(L"ctx=%p mObjKey=%s HANDLE=%p, remotePath=%s", ctx, ctx->mObjKey.c_str(), ctx->mFile.handle(), remotePath.c_str());

    // �t�@�C�����ւ̎Q�Ƃ�o�^

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() ��̏���̌Ăяo��

            NTSTATUS ntstatus = ctx->openFileHandle(CONT_CALLER 0, OPEN_EXISTING);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: openFileHandle");
                return INVALID_HANDLE_VALUE;
            }

            APP_ASSERT(ctx->mFile.valid());
        }
    }   // ���O�̃��b�N������ (safeShare �̐�������)

    traceW(L"ctx=%p mObjKey=%s HANDLE=%p, remotePath=%s", ctx, ctx->mObjKey.c_str(), ctx->mFile.handle(), remotePath.c_str());

    return ctx->mFile.handle();
}

//
// FileOutputParams
//
std::wstring FileOutputParams::str() const
{
    std::wstring sCreationDisposition;

    switch (mCreationDisposition)
    {
        case CREATE_ALWAYS:     sCreationDisposition = L"CREATE_ALWAYS";     break;
        case CREATE_NEW:        sCreationDisposition = L"CREATE_NEW";        break;
        case OPEN_ALWAYS:       sCreationDisposition = L"OPEN_ALWAYS";       break;
        case OPEN_EXISTING:     sCreationDisposition = L"OPEN_EXISTING";     break;
        case TRUNCATE_EXISTING: sCreationDisposition = L"TRUNCATE_EXISTING"; break;
        default: APP_ASSERT(0);
    }

    std::wstringstream ss;

    ss << L"mPath=";
    ss << mPath;
    ss << L" mCreationDisposition=";
    ss << sCreationDisposition;
    ss << L" mOffset=";
    ss << mOffset;
    ss << L" mLength=";
    ss << mLength;
    ss << L" mSpecifyRange=";
    ss << BOOL_CSTRW(mSpecifyRange);

    return ss.str();
}

//
// OpenContext
//
NTSTATUS OpenContext::openFileHandle(CALLER_ARG const DWORD argDesiredAccess, const DWORD argCreationDisposition)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(isFile());
    APP_ASSERT(mObjKey.meansFile());

    const DWORD dwDesiredAccess = mGrantedAccess | argDesiredAccess;

    ULONG CreateFlags = 0;
    //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // �f�B���N�g���͑��삵�Ȃ�

    if (mCreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    HANDLE hFile = ::CreateFileW(getFilePathW().c_str(),
        dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        argCreationDisposition, CreateFlags, 0);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    mFile = hFile;

    return STATUS_SUCCESS;
}

// EOF
