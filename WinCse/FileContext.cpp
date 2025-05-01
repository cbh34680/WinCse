#include "FileContext.hpp"

using namespace CSELIB;
using namespace CSEDRV;


FileContext::~FileContext()
{
    FspFileSystemDeleteDirectoryBuffer(&mDirBuffer);
}


#define TO_LITERAL(name)                L#name
#define ADD_LIST(flags, slist, name)    if ((flags) & (FCTX_FLAGS_ ## name)) slist.push_back(TO_LITERAL(name))

static std::wstring flagsToStr(DWORD argFlags)
{
    std::list<std::wstring> strs;

    ADD_LIST(argFlags, strs, OPEN);
    ADD_LIST(argFlags, strs, CLEANUP);
    ADD_LIST(argFlags, strs, READ);
    ADD_LIST(argFlags, strs, FLUSH);
    ADD_LIST(argFlags, strs, GET_FILE_INFO);
    ADD_LIST(argFlags, strs, RENAME);
    ADD_LIST(argFlags, strs, GET_SECURITY);
    ADD_LIST(argFlags, strs, READ_DIRECTORY);
    ADD_LIST(argFlags, strs, SET_DELETE);
    ADD_LIST(argFlags, strs, CLOSE);

    ADD_LIST(argFlags, strs, M_CREATE);
    ADD_LIST(argFlags, strs, M_WRITE);
    ADD_LIST(argFlags, strs, M_OVERWRITE);
    ADD_LIST(argFlags, strs, M_SET_BASIC_INFO);
    ADD_LIST(argFlags, strs, M_SET_FILE_SIZE);
    ADD_LIST(argFlags, strs, M_SET_SECURITY);

    return JoinStrings(strs, L", ", true);
}

std::wstring FileContext::str() const noexcept
{
    std::wostringstream ss;

    ss << L"mFlags=" << flagsToStr(mFlags);

    return ss.str();
}

// EOF