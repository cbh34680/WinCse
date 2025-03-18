#include "WinCseLib.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <sddl.h>


namespace WinCseLib {

bool PathToFileInfo(const std::wstring& path, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	//FILETIME ftCreate, ftAccess, ftWrite;
	//LARGE_INTEGER fileSize;
	BOOL ret = FALSE;
	NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

	FileHandleRAII hFile = ::CreateFileW
	(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);

	if(hFile.invalid())
	{
		goto exit;
	}

	ntstatus = GetFileInfoInternal(hFile.handle(), pFileInfo);
	if (!NT_SUCCESS(ntstatus))
	{
		goto exit;
	}

	ret = true;

exit:
	hFile.close();

	return ret;
}

bool TouchIfNotExists(const std::wstring& arg)
{
	if (!std::filesystem::exists(arg))
	{
		// ��t�@�C���̍쐬
		std::wofstream ofs(arg);
		if (!ofs)
		{
			return false;
		}

		ofs.close();
	}

	if (std::filesystem::exists(arg))
	{
		// �t�@�C���ł͂Ȃ�
		if (!std::filesystem::is_regular_file(arg))
		{
			return false;
		}
	}
	else
	{
		// ���݂��Ȃ�
		return false;
	}

	return true;
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
#if 0
	const auto tmpfile{ _wtempnam(arg.c_str(), L"wtest") };
	APP_ASSERT(tmpfile);

	std::ofstream ofs(tmpfile);
	if (!ofs)
	{
		return false;
	}
	ofs.close();

	std::error_code ec;
	std::filesystem::remove(tmpfile, ec);
	free(tmpfile);

#else
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
#endif

	return true;
}

std::string EncodeFileNameToLocalNameA(const std::string& str)
{
	return URLEncodeA(Base64EncodeA(str));
}

std::string DecodeLocalNameToFileNameA(const std::string& str)
{
	return Base64DecodeA(URLDecodeA(str));
}

std::wstring EncodeFileNameToLocalNameW(const std::wstring& str)
{
	return MB2WC(URLEncodeA(Base64EncodeA(WC2MB(str))));
}

std::wstring DecodeLocalNameToFileNameW(const std::wstring& str)
{
	return MB2WC(Base64DecodeA(URLDecodeA(WC2MB(str))));
}

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

	FileHandleRAII hFile = ::CreateFileW
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

} // WinCseLib

// EOF