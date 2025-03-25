#include "WinCseLib.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <sddl.h>


namespace WinCseLib {

bool HandleToFileInfo(HANDLE argHandle, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	NTSTATUS ntstatus = GetFileInfoInternal(argHandle, pFileInfo);

	return NT_SUCCESS(ntstatus) ? true : false;
}

bool PathToFileInfoW(const std::wstring& path, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	LastErrorBackup _backup;

	FileHandle hFile = ::CreateFileW
	(
		path.c_str(),
		FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL
	);

	if(hFile.invalid())
	{
		return false;
	}

	return HandleToFileInfo(hFile.handle(), pFileInfo);
}

bool PathToFileInfoA(const std::string& path, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	return PathToFileInfoW(MB2WC(path), pFileInfo);
}

// パスから FILETIME の値を取得
bool PathToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite)
{
	LastErrorBackup _backup;

	FileHandle hFile = ::CreateFileW
	(
		path.c_str(),
		FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL
	);

	if(hFile.invalid())
	{
		return false;
	}

	return ::GetFileTime(hFile.handle(), pFtCreate, pFtAccess, pFtWrite);
}

bool MkdirIfNotExists(const std::wstring& arg)
{
	LastErrorBackup _backup;

	if (std::filesystem::exists(arg))
	{
		if (!std::filesystem::is_directory(arg))
		{
			return false;
		}
	}
	else
	{
		std::error_code ec;
		if (!std::filesystem::create_directories(arg, ec))
		{
			return false;
		}
	}

	// 書き込みテスト

	WCHAR tmpfile[MAX_PATH];
	if (!::GetTempFileName(arg.c_str(), L"tst", 0, tmpfile))
	{
		return false;
	}

	std::ofstream ofs(tmpfile);
	if (!ofs)
	{
		return false;
	}
	ofs.close();

	std::error_code ec;
	std::filesystem::remove(tmpfile, ec);

	return true;
}

bool forEachFiles(const std::wstring& directory, const std::function<void(const WIN32_FIND_DATA& wfd)>& callback)
{
	LastErrorBackup _backup;

	WIN32_FIND_DATA wfd = {};
	HANDLE hFind = ::FindFirstFileW((directory + L"\\*").c_str(), &wfd);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	do
	{
		//if (wcscmp(wfd.cFileName, L".") != 0 && wcscmp(wfd.cFileName, L"..") != 0)
		{
			callback(wfd);
		}
	}
	while (::FindNextFile(hFind, &wfd) != 0);

	::FindClose(hFind);

	return true;
}

#if 0
/*
* example
* 
	std::wstring path;
	HandleToPath(handle, path);
	traceW(L"selected path is %s", path.c_str());

	std::wstring sdstr;
	PathToSDStr(path, sdstr);
	traceW(L"sdstr is %s", sdstr.c_str());
*/

// ファイル・ハンドルからファイル・パスに変換
bool HandleToPath(HANDLE Handle, std::wstring& ref)
{
	::SetLastError(ERROR_SUCCESS);

	const auto needLen = ::GetFinalPathNameByHandle(Handle, nullptr, 0, FILE_NAME_NORMALIZED);
	APP_ASSERT(::GetLastError() == ERROR_NOT_ENOUGH_MEMORY);

	const auto needSize = needLen + 1;
	std::wstring path(needSize, 0);

	::GetFinalPathNameByHandle(Handle, path.data(), needSize, FILE_NAME_NORMALIZED);
	APP_ASSERT(::GetLastError() == ERROR_SUCCESS);

	ref = std::move(path);

	return true;
}

// SDDL 文字列関連
static const SECURITY_INFORMATION DEFAULT_SECURITY_INFO = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;

static LPWSTR SDToSDStr_(PSECURITY_DESCRIPTOR pSecDesc)
{
	LPWSTR pStringSD = nullptr;

	// セキュリティ記述子を文字列に変換
	if (::ConvertSecurityDescriptorToStringSecurityDescriptorW(
		pSecDesc,
		SDDL_REVISION_1,
		DEFAULT_SECURITY_INFO,
		&pStringSD,
		nullptr))
	{
		return pStringSD;
	}

	return nullptr;
}

static LPWSTR HandleToSDStr_(HANDLE Handle)
{
	APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

	SECURITY_DESCRIPTOR secDesc = {};
	DWORD lengthNeeded = 0;

	PSECURITY_DESCRIPTOR pSecDesc = nullptr;
	LPWSTR pStringSD = nullptr;

	// 最初の呼び出しで必要なバッファサイズを取得
	BOOL result = ::GetKernelObjectSecurity(Handle, DEFAULT_SECURITY_INFO, &secDesc, 0, &lengthNeeded);

	if (!result && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		// 必要なバッファサイズを持つバッファを確保
		pSecDesc = (PSECURITY_DESCRIPTOR)::LocalAlloc(LPTR, lengthNeeded);
		if (pSecDesc)
		{
			// 再度呼び出してセキュリティ情報を取得
			result = ::GetKernelObjectSecurity(Handle, DEFAULT_SECURITY_INFO, pSecDesc, lengthNeeded, &lengthNeeded);
			if (result)
			{
				pStringSD = SDToSDStr_(pSecDesc);
			}

			goto success;
		}
	}

	if (pStringSD)
	{
		::LocalFree(pStringSD);
		pStringSD = nullptr;
	}

success:
	if (pSecDesc)
	{
		::LocalFree(pSecDesc);
		pSecDesc = nullptr;
	}

	return pStringSD;
}

static LPWSTR PathToSDStr_(LPCWSTR Path)
{
	APP_ASSERT(Path);

	LPWSTR pStringSD = nullptr;

	FileHandle hFile = ::CreateFileW
	(
		Path,
		FILE_READ_ATTRIBUTES | READ_CONTROL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL
	);

	if (hFile.valid())
	{
		pStringSD = HandleToSDStr_(hFile.handle());

		hFile.close();
	}

	return pStringSD;
}

bool PathToSDStr(const std::wstring& path, std::wstring& sdstr)
{
	const auto p = PathToSDStr_(path.c_str());
	if (!p)
	{
		return false;
	}

	sdstr = p;
	LocalFree(p);

	return true;
}
#endif

} // WinCseLib

// EOF