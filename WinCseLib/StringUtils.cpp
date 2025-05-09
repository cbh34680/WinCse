#include "WinCseLib.h"
#include <iomanip>
#include <cwctype>
#include <algorithm>


namespace CSELIB {

// wstring ���� string �ւ̕ϊ�
std::string WC2MB(const std::wstring& wstr)
{
	LastErrorBackup _backup;

	if (wstr.empty())
	{
		return "";
	}

	const auto pWstr = wstr.c_str();

	::SetLastError(ERROR_SUCCESS);

	const int need = ::WideCharToMultiByte(CP_UTF8, 0, pWstr, -1, NULL, 0, NULL, NULL);
	auto lerr = ::GetLastError();
	if (lerr != ERROR_SUCCESS)
	{
		throw FatalError(__FUNCTION__, lerr);
	}

	std::vector<char> buff(need);
	char* pStr = buff.data();

	::WideCharToMultiByte(CP_UTF8, 0, pWstr, -1, pStr, need, NULL, NULL);
	lerr = ::GetLastError();
	if (lerr != ERROR_SUCCESS)
	{
		throw FatalError(__FUNCTION__, lerr);
	}

	return std::string{ pStr };
}

// string ���� wstring �ւ̕ϊ�
std::wstring MB2WC(const std::string& str)
{
	LastErrorBackup _backup;

	if (str.empty())
	{
		return L"";
	}

	const auto pStr = str.c_str();

	::SetLastError(ERROR_SUCCESS);

	const int need = ::MultiByteToWideChar(CP_UTF8, 0, pStr, -1, NULL, 0);
	auto lerr = ::GetLastError();
	if (lerr != ERROR_SUCCESS)
	{
		throw FatalError(__FUNCTION__, lerr);
	}

	std::vector<WCHAR> buff(need);
	WCHAR* pWstr = buff.data();

	::MultiByteToWideChar(CP_UTF8, 0, pStr, -1, pWstr, need);
	lerr = ::GetLastError();
	if (lerr != ERROR_SUCCESS)
	{
		throw FatalError(__FUNCTION__, lerr);
	}

	return std::wstring{ pWstr };
}

// argKey                       parentDir       filename
// ------------------------------------------------------
// ""						NG
// "/"						NG
// "/root"					NG
// "dir"					OK	""				"dir"       
// "dir/"					OK	""				"dir"
// "dir/key.txt"			OK	"dir/"			"key.txt"
// "dir/key.txt/"			OK	"dir/"			"key.txt/"
// "dir/subdir/key.txt"		OK	"dir/subdir/"	"key.txt"
// "dir/subdir/key.txt/"	OK	"dir/subdir/"	"key.txt/"

bool SplitObjectKey(const std::wstring& argKey, std::wstring* pParentDir /* nullable */, std::wstring* pFileName /* nullable */)
{
	// �L�[����e�f�B���N�g�����擾

	if (argKey.empty())
	{
		return false;
	}

	if (argKey.at(0) == L'/')
	{
		// "/" ����n�܂�p�X�͖���

		return false;
	}

	auto tokens{ SplitString(argKey, L'/', true) };
	if (tokens.empty())
	{
		return false;
	}

	auto fileName{ tokens.back() };
	if (fileName.empty())
	{
		return false;
	}

	tokens.pop_back();

	// �����Ώۂ̐e�f�B���N�g��

	auto parentDir{ JoinStrings(tokens, L'/', true) };
	if (parentDir.empty())
	{
		// �o�P�b�g�̃��[�g�E�f�B���N�g�����猟��

		// "" --> ""
	}
	else
	{
		// �T�u�f�B���N�g�����猟��

		// "dir"        --> "dir/"
		// "dir/subdir" --> "dir/subdir/"

		parentDir += L'/';
	}

	// �����Ώۂ̃t�@�C���� (�f�B���N�g����)

	if (argKey.back() == L'/')
	{
		// SplitString() �� "/" ��������Ă��܂��̂ŁAargKey �� "dir/" �� "dir/file.txt/"
		// ���w�肳��Ă���Ƃ��� filename �� "/" ��t�^

		fileName += L'/';
	}

	if (pParentDir)
	{
		*pParentDir = std::move(parentDir);
	}

	if (pFileName)
	{
		*pFileName = std::move(fileName);
	}

	return true;
}

std::wstring FileTypeEnumToStringW(FileTypeEnum argFileType)
{
	switch (argFileType)
	{
		case FileTypeEnum::None:			return L"None";
		case FileTypeEnum::Root:			return L"Root";
		case FileTypeEnum::Bucket:			return L"Bucket";
		case FileTypeEnum::Directory:		return L"Directory";
		case FileTypeEnum::File:			return L"File";

		default:
		{
			APP_ASSERT(0);
		}
	}

	return L"@";
}

#define ADDSTR_IF_FLAG_ON(fa, name)	if ((fa) & FILE_ATTRIBUTE_ ## name) strs.push_back(L#name)

std::wstring FileAttributesToStringW(DWORD dwFlagsAndAttributes)
{
	std::list<std::wstring> strs;

	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, READONLY);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, HIDDEN);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, SYSTEM);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, DIRECTORY);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, ARCHIVE);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, DEVICE);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, NORMAL);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, TEMPORARY);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, SPARSE_FILE);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, REPARSE_POINT);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, COMPRESSED);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, OFFLINE);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, NOT_CONTENT_INDEXED);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, ENCRYPTED);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, INTEGRITY_STREAM);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, VIRTUAL);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, NO_SCRUB_DATA);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, EA);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, PINNED);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, UNPINNED);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, RECALL_ON_OPEN);
	ADDSTR_IF_FLAG_ON(dwFlagsAndAttributes, RECALL_ON_DATA_ACCESS);

	return JoinStrings(strs, L',', true);
}

std::wstring FileInfoToStringW(const FSP_FSCTL_FILE_INFO& argFileInfo)
{
	std::wstringstream ss;

	ss << L"FileAttributes=" << argFileInfo.FileAttributes << L' ';
	ss << L"FileSize=" << argFileInfo.FileSize << L' ';
	ss << L"CreationTime=" << WinFileTime100nsToLocalTimeStringW(argFileInfo.CreationTime) << L' ';
	ss << L"LastAccessTime=" << WinFileTime100nsToLocalTimeStringW(argFileInfo.LastAccessTime) << L' ';
	ss << L"LastWriteTime=" << WinFileTime100nsToLocalTimeStringW(argFileInfo.LastWriteTime) << L' ';
	ss << L"ChangeTime=" << WinFileTime100nsToLocalTimeStringW(argFileInfo.ChangeTime) << L' ';

	return ss.str();
}

// ������̃n�b�V���l���Z�o
size_t HashString(const std::wstring& arg)
{
	std::hash<std::wstring> f;
	return f(arg);
}

bool Base64DecodeA(const std::string& src, std::string* pDst)
{
	DWORD dstSize = 0;

	BOOL b = ::CryptStringToBinaryA(
		src.c_str(), (DWORD)src.size(), CRYPT_STRING_BASE64,
		NULL, &dstSize, NULL, NULL);

	if (!b)
	{
		return false;
	}

	std::vector<BYTE> dst(dstSize);

	b = ::CryptStringToBinaryA(
		src.c_str(), (DWORD)src.size(), CRYPT_STRING_BASE64,
		dst.data(), &dstSize, NULL, NULL);

	if (!b)
	{
		return false;
	}

	*pDst = std::string(dst.cbegin(), dst.cend());

	return true;
}

// �O��̋󔒂��g��������֐�
std::wstring TrimW(const std::wstring& str)
{
	std::wstring trimmedStr = str;

	// �擪�̋󔒂��g����
	trimmedStr.erase(trimmedStr.cbegin(), std::find_if(trimmedStr.cbegin(), trimmedStr.cend(), [](wchar_t ch)
	{
		return !std::isspace(ch);
	}));

	// �����̋󔒂��g����
	trimmedStr.erase(std::find_if(trimmedStr.crbegin(), trimmedStr.crend(), [](wchar_t ch)
	{
		return !std::isspace(ch);

	}).base(), trimmedStr.cend());

	return trimmedStr;
}

std::wstring WildcardToRegexW(const std::wstring& wildcard)
{
	std::wostringstream ss;

	ss << L'^';

	for (wchar_t ch : wildcard)
	{
		if (ch == L'\0')
		{
			break;
		}

		switch (ch)
		{
			case L'*':
				ss << L".*";
				break;

			case L'?':
				ss << L'.';
				break;

			default:
				if (std::iswpunct(ch))
				{
					ss << L'\\';
				}
				ss << ch;
				break;
		}
	}

	ss << L'$';

	return ss.str();
}

std::vector<std::wstring> SplitString(const std::wstring& input, wchar_t sep, bool ignoreEmpty)
{
    std::wistringstream ss{ input };
    std::wstring token;
	std::vector<std::wstring> strs;

    while (std::getline(ss, token, sep))
    {
		if (ignoreEmpty)
		{
			if (token.empty())
			{
				continue;
			}
		}

		strs.push_back(token);
    }

	return strs;
}

} // CSELIB

// EOF