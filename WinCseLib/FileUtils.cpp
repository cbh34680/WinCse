#include "WinCseLib.h"
#include <fstream>
#include <filesystem>
#include <sddl.h>


namespace WCSE {

BOOL DeleteFilePassively(PCWSTR argPath)
{
	// 開いているファイル・ハンドルがない状態の時に削除する

	HANDLE handle = ::CreateFile(argPath, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}

	::CloseHandle(handle);

	return TRUE;
}

std::wstring GetCacheFilePath(const std::wstring& argDir, const std::wstring& argName)
{
	std::wstring nameSha256;

	const auto ntstatus = ComputeSHA256W(argName, &nameSha256);
	if (!NT_SUCCESS(ntstatus))
	{
		throw FatalError(__FUNCTION__, ntstatus);
	}

	// 先頭の 2Byte はディレクトリ名

	std::filesystem::path filePath{ argDir };
	filePath.append(nameSha256.substr(0, 2));

	std::error_code ec;
	std::filesystem::create_directory(filePath, ec);

	if (ec)
	{
		throw FatalError(__FUNCTION__);
	}

	filePath.append(nameSha256.substr(2));

	return filePath.wstring();
}

NTSTATUS PathToFileInfo(const std::wstring& path, FSP_FSCTL_FILE_INFO* pFileInfo)
{
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
		const auto lerr = ::GetLastError();
		return FspNtStatusFromWin32(lerr);
	}

	return GetFileInfoInternal(hFile.handle(), pFileInfo);
}

bool MkdirIfNotExists(const std::wstring& arg)
{
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

bool forEachFiles(const std::wstring& argDir, const std::function<void(const WIN32_FIND_DATA& wfd, const std::wstring& fullPath)>& callback)
{
	const auto dir{ std::filesystem::absolute(argDir) };

	WIN32_FIND_DATA wfd;
	HANDLE Handle = ::FindFirstFileW((dir.wstring() + L"\\*").c_str(), &wfd);

	if (Handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	do
	{
		if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0)
		{
			continue;
		}

		const auto curPath{ dir / wfd.cFileName };

		if (FA_IS_DIRECTORY(wfd.dwFileAttributes))
		{
			if (!forEachFiles(curPath, callback))
			{
				return false;
			}
		}
		else
		{
			callback(wfd, curPath.wstring());
		}
	}
	while (::FindNextFile(Handle, &wfd) != 0);

	::FindClose(Handle);

	return true;
}

bool forEachDirs(const std::wstring& argDir, const std::function<void(const WIN32_FIND_DATA& wfd, const std::wstring& fullPath)>& callback)
{
	const auto dir{ std::filesystem::absolute(argDir) };

	WIN32_FIND_DATA wfd;
	HANDLE Handle = ::FindFirstFileW((dir.wstring() + L"\\*").c_str(), &wfd);

	if (Handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	do
	{
		if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0)
		{
			continue;
		}

		const auto curPath{ dir / wfd.cFileName };

		if (FA_IS_DIRECTORY(wfd.dwFileAttributes))
		{
			if (!forEachDirs(curPath, callback))
			{
				return false;
			}

			callback(wfd, curPath.wstring());
		}
	}
	while (::FindNextFile(Handle, &wfd) != 0);

	::FindClose(Handle);

	return true;
}

} // WCSE

// EOF