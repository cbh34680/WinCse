#include "WinCseLib.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <sddl.h>


namespace WinCseLib {

bool GetCacheFilePath(const std::wstring& argDir, const std::wstring& argName, std::wstring* pPath)
{
	std::wstring nameSha256;

	if (!ComputeSHA256W(argName, &nameSha256))
	{
		return false;
	}

	std::filesystem::path filePath{ argDir };
	filePath.append(nameSha256.substr(0, 2));

	std::error_code ec;
	std::filesystem::create_directory(filePath, ec);

	if (ec)
	{
		return false;
	}

	filePath.append(nameSha256.substr(2));

	*pPath = filePath.wstring();

	return true;
}

bool PathToFileInfoW(const std::wstring& path, FSP_FSCTL_FILE_INFO* pFileInfo)
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
		return false;
	}

	NTSTATUS ntstatus = GetFileInfoInternal(hFile.handle(), pFileInfo);
	return NT_SUCCESS(ntstatus);
}

bool PathToFileInfoA(const std::string& path, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	return PathToFileInfoW(MB2WC(path), pFileInfo);
}

// �p�X���� FILETIME �̒l���擾
bool PathToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite)
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
		return false;
	}

	return ::GetFileTime(hFile.handle(), pFtCreate, pFtAccess, pFtWrite);
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

	// �������݃e�X�g

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
	WIN32_FIND_DATA wfd = {};
	HANDLE hFind = ::FindFirstFileW((argDir + L"\\*").c_str(), &wfd);

	const std::filesystem::path dir{ argDir };

	if (hFind == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	do
	{
		if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0)
		{
			continue;
		}

		const std::filesystem::path curPath{ dir / wfd.cFileName };

		if (FA_IS_DIR(wfd.dwFileAttributes))
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

// �t�@�C���E�n���h������t�@�C���E�p�X�ɕϊ�
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

// SDDL ������֘A
static const SECURITY_INFORMATION DEFAULT_SECURITY_INFO = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;

static LPWSTR SDToSDStr_(PSECURITY_DESCRIPTOR pSecDesc)
{
	LPWSTR pStringSD = nullptr;

	// �Z�L�����e�B�L�q�q�𕶎���ɕϊ�
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

	// �ŏ��̌Ăяo���ŕK�v�ȃo�b�t�@�T�C�Y���擾
	BOOL result = ::GetKernelObjectSecurity(Handle, DEFAULT_SECURITY_INFO, &secDesc, 0, &lengthNeeded);

	if (!result && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		// �K�v�ȃo�b�t�@�T�C�Y�����o�b�t�@���m��
		pSecDesc = (PSECURITY_DESCRIPTOR)::LocalAlloc(LPTR, lengthNeeded);
		if (pSecDesc)
		{
			// �ēx�Ăяo���ăZ�L�����e�B�����擾
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