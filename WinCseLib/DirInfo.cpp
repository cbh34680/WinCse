#include "WinCseLib.h"

using namespace CSELIB;


#define TO_LITERAL(name)        L#name

#define KV_WSTR(name)           std::wstring(TO_LITERAL(name)) + L'=' + name
#define KV_BOOL(name)           std::wstring(TO_LITERAL(name)) + L'=' + BOOL_CSTRW(name)
#define KV_TO_WSTR(name)        std::wstring(TO_LITERAL(name)) + L'=' + std::to_wstring(name)

std::wstring DirInfo::str() const noexcept
{
	return JoinStrings(std::initializer_list<std::wstring>{
		KV_WSTR(FileName),
		KV_WSTR(FileNameBuf),
		std::wstring(L"FileType=") + FileTypeToStr(FileType),
		KV_TO_WSTR(FileInfo.FileSize),
		KV_TO_WSTR(FileInfo.CreationTime),
		KV_TO_WSTR(FileInfo.LastAccessTime),
		KV_TO_WSTR(FileInfo.LastWriteTime)
	}, L", ", true);
}

// EOF