#include "WinCseLib.h"
#include <fstream>
#include <sddl.h>

namespace CSELIB {

FILESIZE_T GetFileSize(const std::filesystem::path& argPath)
{
	NEW_LOG_BLOCK();

	WIN32_FILE_ATTRIBUTE_DATA cacheFileInfo{};

	if (!::GetFileAttributesExW(argPath.c_str(), GetFileExInfoStandard, &cacheFileInfo))
	{
		errorW(L"fault: GetFileAttributesExW argPath=%s", argPath.c_str());
		return -1LL;
	}

	LARGE_INTEGER li{};
	li.HighPart = cacheFileInfo.nFileSizeHigh;
	li.LowPart = cacheFileInfo.nFileSizeLow;

	return li.QuadPart;
}

BOOL DeleteFilePassively(const std::filesystem::path& argPath)
{
	// 開いているファイル・ハンドルがない状態の時に削除する

	HANDLE handle = ::CreateFileW(argPath.c_str(), 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}

	::CloseHandle(handle);

	return TRUE;
}

bool GetFileNameFromHandle(HANDLE hFile, std::filesystem::path* pPath)
{
	WCHAR filePath[MAX_PATH];

	const auto result = ::GetFinalPathNameByHandleW(hFile, filePath, MAX_PATH, FILE_NAME_NORMALIZED);
	if (result == 0)
	{
		return false;
	}

	*pPath = filePath;

	return true;
}

bool mkdirIfNotExists(const std::filesystem::path& argDir)
{
	if (std::filesystem::exists(argDir))
	{
		if (!std::filesystem::is_directory(argDir))
		{
			return false;
		}
	}
	else
	{
		std::error_code ec;
		if (!std::filesystem::create_directories(argDir, ec))
		{
			return false;
		}
	}

	// 書き込みテスト

	WCHAR tmpfile[MAX_PATH];
	if (!::GetTempFileNameW(argDir.c_str(), L"tst", 0, tmpfile))
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

bool forEachFiles(const std::filesystem::path& argDir, const std::function<void(const WIN32_FIND_DATA&, const std::filesystem::path&)>& callback)
{
	const auto findFileName{ argDir / L"*" };

	WIN32_FIND_DATA wfd;
	HANDLE hFile = ::FindFirstFileW(findFileName.wstring().c_str(), &wfd);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	do
	{
		if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0)
		{
			continue;
		}

		const auto curPath{ argDir / wfd.cFileName };

		if (FA_IS_DIR(wfd.dwFileAttributes))
		{
			// ドットエントリ以外のディレクトリは再帰

			if (!forEachFiles(curPath, callback))
			{
				::FindClose(hFile);

				return false;
			}
		}
		else
		{
			callback(wfd, curPath);
		}
	}
	while (::FindNextFileW(hFile, &wfd) != 0);

	::FindClose(hFile);

	return true;
}

bool forEachDirs(const std::filesystem::path& argDir, const std::function<void(const WIN32_FIND_DATA& wfd, const std::filesystem::path& fullPath)>& callback)
{
	const auto findFileName{ argDir / L"*" };

	WIN32_FIND_DATA wfd;
	HANDLE hFile = ::FindFirstFileW(findFileName.wstring().c_str(), &wfd);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	do
	{
		if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0)
		{
			continue;
		}

		const auto curPath{ argDir / wfd.cFileName };

		if (FA_IS_DIR(wfd.dwFileAttributes))
		{
			// ドットエントリ以外のディレクトリは再帰

			if (!forEachDirs(curPath, callback))
			{
				::FindClose(hFile);

				return false;
			}

			callback(wfd, curPath);
		}
	}
	while (::FindNextFileW(hFile, &wfd) != 0);

	::FindClose(hFile);

	return true;
}

} // CSELIB

// EOF