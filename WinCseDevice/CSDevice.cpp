#include "CSDevice.hpp"

using namespace CSELIB;

namespace CSEDVC {

CSELIB::FILEIO_LENGTH_T writeStreamFromFile(CALLER_ARG const std::ostream* argOutputStream,
    const std::filesystem::path& argInputPath, CSELIB::FILEIO_OFFSET_T argInputOffset, CSELIB::FILEIO_LENGTH_T argInputLength)
{
    NEW_LOG_BLOCK();

    CSELIB::FileHandle file = ::CreateFileW(
        argInputPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: CreateFileW lerr=%lu", lerr);
        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argInputOffset;

    if (::SetFilePointerEx(file.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: SetFilePointerEx lerr=%lu file=%s", lerr, file.str().c_str());

        return -1LL;
    }

    static thread_local char buffer[CSELIB::FILEIO_BUFFER_SIZE];
    const CSELIB::FILEIO_LENGTH_T bufferSize = _countof(buffer);

    auto* pbuf = argOutputStream->rdbuf();
    auto remainingTotal = argInputLength;

    while (remainingTotal > 0)
    {
        if (!argOutputStream->good())
        {
            errorW(L"fault: no good");
            return -1LL;
        }

        DWORD bytesRead;
        if (!::ReadFile(file.handle(), buffer, static_cast<DWORD>(min(bufferSize, remainingTotal)), &bytesRead, NULL))
        {
            const auto lerr = ::GetLastError();

            errorW(L"fault: ReadFile lerr=%lu", lerr);
            return -1LL;
        }

        traceW(L"bytesRead=%lu", bytesRead);

        auto remainingWrite = static_cast<std::streamsize>(bytesRead);
        auto* pos = buffer;

        while (remainingWrite > 0)
        {
            traceW(L"remainingWrite=%lld", remainingWrite);

            const auto bytesWritten = pbuf->sputn(pos, remainingWrite);
            if (bytesWritten <= 0)
            {
                errorW(L"fault: sputn");
                return -1LL;
            }

            pos += bytesWritten;
            remainingWrite -= bytesWritten;

            traceW(L"bytesWritten=%lld remainingWrite=%lld", bytesWritten, remainingWrite);
        }

        remainingTotal -= bytesRead;

        traceW(L"remainingTotal=%lld", remainingTotal);
    }

    APP_ASSERT(remainingTotal == 0);

    return argInputLength;
}

CSELIB::FILEIO_LENGTH_T writeFileFromStream(CALLER_ARG
    const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOutputOffset,
    const std::istream* argInputStream, CSELIB::FILEIO_LENGTH_T argInputLength)
{
    NEW_LOG_BLOCK();

    // �t�@�C�����J�� argOffset �̈ʒu�Ƀ|�C���^���ړ�

    CSELIB::FileHandle file = ::CreateFileW
    (
        argOutputPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: CreateFileW lerr=%lu argOutputPath=%s argOffset=%lld", lerr, argOutputPath.c_str(), argOutputOffset);

        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argOutputOffset;

    if (::SetFilePointerEx(file.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: SetFilePointerEx lerr=%lu file=%s", lerr, file.str().c_str());

        return -1LL;
    }

    // �擾�������e���t�@�C���ɏo��

    auto* pbuf = argInputStream->rdbuf();
    auto remainingTotal = argInputLength;

    static thread_local char buffer[CSELIB::FILEIO_BUFFER_SIZE];
    const CSELIB::FILEIO_LENGTH_T bufferSize = _countof(buffer);

    while (remainingTotal > 0)
    {
        // �o�b�t�@�Ƀf�[�^��ǂݍ���

        if (!argInputStream->good())
        {
            errorW(L"fault: no good");
            return -1LL;
        }

        const auto bytesRead = pbuf->sgetn(buffer, min(remainingTotal, bufferSize));
        if (bytesRead <= 0)
        {
            errorW(L"fault: sgetn");
            return -1LL;
        }

        traceW(L"bytesRead=%lld", bytesRead);

        // �t�@�C���Ƀf�[�^����������

        auto* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            traceW(L"remainingWrite=%lld", remainingWrite);

            DWORD bytesWritten;
            if (!::WriteFile(file.handle(), pos, static_cast<DWORD>(remainingWrite), &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                errorW(L"fault: WriteFile lerr=%lu", lerr);

                return -1LL;
            }

            pos += bytesWritten;
            remainingWrite -= bytesWritten;

            traceW(L"bytesWritten=%lu remainingWrite=%lld", bytesWritten, remainingWrite);
        }

        remainingTotal -= bytesRead;

        traceW(L"remainingTotal=%lld", remainingTotal);
    }

    return argInputLength;
}

CSDevice::~CSDevice()
{
    this->OnSvcStop();
}

struct ListBucketsTask : public IOnDemandTask
{
    CSDevice* mThat;

    ListBucketsTask(CSDevice* argThat)
        :
        mThat(argThat)
    {
    }

    void run(int) override
    {
        NEW_LOG_BLOCK();

        //traceW(L"call ListBuckets");

        mThat->listBuckets(START_CALLER nullptr);
    }
};

bool CSDevice::headBucket(CALLER_ARG const std::wstring& argBucketName, DirEntryType* pDirEntry)
{
    return mQueryBucket->qbHeadBucket(CONT_CALLER argBucketName, pDirEntry);
}

bool CSDevice::listBuckets(CALLER_ARG DirEntryListType* pDirEntryList)
{
    return mQueryBucket->qbListBuckets(CONT_CALLER pDirEntryList);
}

bool CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    APP_ASSERT(argObjKey.isObject());

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂ŁA��̃f�B���N�g���E�I�u�W�F�N�g�����݂��Ȃ��Ƃ���
    // ListObjects() �����s���āA���W�b�N�Ŕ��f���邱�ƂɂȂ�B
    // ����ł͗��p���鑤���Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    if (argObjKey.meansDir())
    {
        return mQueryObject->qoHeadObjectOrListObjects(CONT_CALLER argObjKey, pDirEntry);
    }
    else
    {
        APP_ASSERT(argObjKey.meansFile());

        return mQueryObject->qoHeadObject(CONT_CALLER argObjKey, pDirEntry);
    }
}

bool CSDevice::headObjectOrCache_(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    // listDisplayObjects �̒��ł̂ݗ��p�����֐�

    if (mRuntimeEnv->StrictFileTimestamp)
    {
        // HeadObject ���擾

        return this->headObject(CONT_CALLER argObjKey, pDirEntry);
    }
    else
    {
        // HeadObject �̃L���b�V��������

        return mQueryObject->qoHeadObjectFromCache(CONT_CALLER argObjKey, pDirEntry);
    }
}

bool CSDevice::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());
    APP_ASSERT(pDirEntryList);

    DirEntryListType dirEntryList;

    if (!this->listObjects(CONT_CALLER argObjKey, &dirEntryList))
    {
        errorW(L"fault: listObjects");

        return false;
    }

    // dirEntryList �̓��e�� HeadObject �Ŏ擾�����L���b�V���ƃ}�[�W

    for (auto& dirEntry: dirEntryList)
    {
        APP_ASSERT(dirEntry->mName != L"." && dirEntry->mName != L"..");

        // �f�B���N�g���Ƀt�@�C������t�^

        const auto searchObjKey{ argObjKey.append(dirEntry->mName) };
        APP_ASSERT(searchObjKey.isObject());

        DirEntryType mergeDirEntry;
        if (this->headObjectOrCache_(CONT_CALLER searchObjKey, &mergeDirEntry))
        {
            // �L���b�V������擾�o�����獷���ւ�

            traceW(L"merge searchObjKey=%s mergeDirEntry=%s", searchObjKey.c_str(), mergeDirEntry->str().c_str());
            dirEntry = std::move(mergeDirEntry);
        }

        if (mQueryObject->qoIsInNegativeCache(CONT_CALLER searchObjKey))
        {
            // ���[�W�����Ⴂ�Ȃǂ� HeadObject �����s�������̂� HIDDEN ������ǉ�

            traceW(L"set hidden searchObjKey=%s", searchObjKey.c_str());
            dirEntry->mFileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
        }
    }

    // �h�b�g�G���g���̒ǉ� (CMD �Ή�)

    const auto it = std::min_element(dirEntryList.cbegin(), dirEntryList.cend(), [](const auto& l, const auto& r)
    {
        return l->mFileInfo.LastWriteTime < r->mFileInfo.LastWriteTime;
    });

    const FILETIME_100NS_T defaultFileTime = it == dirEntryList.cend()
        ? mRuntimeEnv->DefaultCommonPrefixTime : (*it)->mFileInfo.LastWriteTime;

    if (!argObjKey.isBucket())
    {
        // CMD �̓���ɍ��킹��
        //      C:\             ... ".", ".." �͕\������Ȃ�
        //      C:\dir          ... "." �͕\�������
        //      C:\dir\subdir   ... ".", ".." ���\�������

        DirEntryType dirEntry;

        const auto optParentDir{ argObjKey.toParentDir() };
        if (optParentDir)
        {
            if (optParentDir->isBucket())
            {
                this->headBucket(CONT_CALLER optParentDir->bucket(), &dirEntry);
            }
            else
            {
                this->headObjectOrCache_(CONT_CALLER *optParentDir, &dirEntry);
            }
        }

        const FILETIME_100NS_T fileTime = dirEntry ? dirEntry->mFileInfo.LastWriteTime : defaultFileTime;

        dirEntryList.push_front(DirectoryEntry::makeDotEntry(L"..", fileTime));
    }

    DirEntryType dirEntry;

    if (argObjKey.isBucket())
    {
        this->headBucket(CONT_CALLER argObjKey.bucket(), &dirEntry);
    }
    else
    {
        this->headObjectOrCache_(CONT_CALLER argObjKey, &dirEntry);
    }

    const FILETIME_100NS_T fileTime = dirEntry ? dirEntry->mFileInfo.LastWriteTime : defaultFileTime;

    dirEntryList.push_front(DirectoryEntry::makeDotEntry(L".", fileTime));

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool CSDevice::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
    APP_ASSERT(argObjKey.meansDir());

    return mQueryObject->qoListObjects(CONT_CALLER argObjKey, pDirEntryList);
}

FILEIO_LENGTH_T CSDevice::getObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey,
    const std::filesystem::path& argOutputPath, FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength)
{
    return mApiClient->GetObjectAndWriteFile(CONT_CALLER argObjKey, argOutputPath, argOffset, argLength);
}

bool CSDevice::putObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->PutObject(CONT_CALLER argObjKey, argFileInfo, argInputPath))
    {
        errorW(L"fault: PutObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    // �L���b�V���E����������폜

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool CSDevice::copyObject(CALLER_ARG const ObjectKey& argSrcObjKey, const ObjectKey& argDstObjKey)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->CopyObject(CONT_CALLER argSrcObjKey, argDstObjKey))
    {
        errorW(L"fault: CopyObject argSrcObjKey=%s argDstObjKey=%s", argSrcObjKey.c_str(), argDstObjKey.c_str());
        return false;
    }

    // �L���b�V���E����������폜

    const auto numSrc = mQueryObject->qoDeleteCache(CONT_CALLER argSrcObjKey);
    traceW(L"cache delete numSrc=%d, argSrcObjKey=%s", numSrc, argSrcObjKey.c_str());


    const auto numDst = mQueryObject->qoDeleteCache(CONT_CALLER argDstObjKey);
    traceW(L"cache delete numDst=%d, argDstObjKey=%s", numDst, argDstObjKey.c_str());

    return true;
}

bool CSDevice::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->DeleteObject(CONT_CALLER argObjKey))
    {
        errorW(L"fault: DeleteObject");
        return false;
    }

    // �L���b�V���E����������폜

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool CSDevice::deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->DeleteObjects(CONT_CALLER argBucket, argKeys))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    // �L���b�V���E����������폜

    for (const auto& key: argKeys)
    {
        const auto optObjKey{ ObjectKey::fromObjectPath(argBucket, key) };
        if (optObjKey)
        {
            const auto num = mQueryObject->qoDeleteCache(CONT_CALLER *optObjKey);
            traceW(L"cache delete num=%d, optObjKey=%s", num, optObjKey->c_str());
        }
        else
        {
            errorW(L"fault: fromObjectPath argBucket=%s key=%s", argBucket.c_str(), key.c_str());
        }
    }

    return true;
}

}   // namespace CSEDVC

// EOF
