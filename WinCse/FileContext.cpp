#include "FileContext.hpp"

using namespace CSELIB;
using namespace CSEDRV;


#define TO_LITERAL(name)                L#name
#define ADD_LIST(flags, slist, name)    if ((flags) & (FCTX_FLAGS_ ## name)) slist.push_back(TO_LITERAL(name))

static std::wstring flagsToStringW(DWORD argFlags)
{
    std::list<std::wstring> strs;

    ADD_LIST(argFlags, strs, OPEN);
    ADD_LIST(argFlags, strs, CLEANUP);
    ADD_LIST(argFlags, strs, READ);
    ADD_LIST(argFlags, strs, FLUSH);
    ADD_LIST(argFlags, strs, GET_FILE_INFO);
    ADD_LIST(argFlags, strs, GET_SECURITY);
    ADD_LIST(argFlags, strs, READ_DIRECTORY);
    ADD_LIST(argFlags, strs, SET_DELETE);
    ADD_LIST(argFlags, strs, CLOSE);

    ADD_LIST(argFlags, strs, M_CREATE);
    ADD_LIST(argFlags, strs, M_WRITE);
    ADD_LIST(argFlags, strs, M_OVERWRITE);
    ADD_LIST(argFlags, strs, M_RENAME);
    ADD_LIST(argFlags, strs, M_SET_BASIC_INFO);
    ADD_LIST(argFlags, strs, M_SET_FILE_SIZE);
    ADD_LIST(argFlags, strs, M_SET_SECURITY);

    return JoinStrings(strs, L", ", true);
}

std::wstring FileContext::str() const
{
    LastErrorBackup _backup;

    std::wostringstream ss;

    ss << L"mFlags=" << flagsToStringW(mFlags) << L' ';
    ss << L"mWinPath=" << mWinPath.wstring() << L' ';
    ss << L"mDirEntry=" << mDirEntry->str();

    return ss.str();
}

ObjectKey FileContext::getObjectKey() const
{
    const auto optObjKey{ ObjectKey::fromWinPath(mWinPath) };

    switch (mDirEntry->mFileType)
    {
        case FileTypeEnum::Bucket:
        case FileTypeEnum::File:
        {
            break;
        }

        case FileTypeEnum::Directory:
        {
            return optObjKey->toDir();
            break;
        }

        default:
        {
            throw FatalError(__FUNCTION__);
        }
    }

    return *optObjKey;
}

std::wstring OpenFileContext::str() const
{
    LastErrorBackup _backup;

    std::wostringstream ss;

    ss << FileContext::str();
    ss << L" mFile=" << mFile.str();

    return ss.str();
}

// EOF