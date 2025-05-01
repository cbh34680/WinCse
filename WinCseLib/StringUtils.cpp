#include "WinCseLib.h"
#include <iomanip>
#include <cwctype>
#include <algorithm>


namespace CSELIB {

bool MeansHiddenFile(const std::wstring& argFileName)
{
	if (argFileName.empty())
	{
		return false;
	}

	return (argFileName != L"." && argFileName != L".." && argFileName.at(0) == L'.');
}

// wstring から string への変換
std::string WC2MB(const std::wstring& wstr) noexcept(false)
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

// string から wstring への変換
std::wstring MB2WC(const std::string& str) noexcept(false)
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
	// キーから親ディレクトリを取得

	if (argKey.empty())
	{
		return false;
	}

	if (argKey.at(0) == L'/')
	{
		// "/" から始まるパスは無効

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

	// 検索対象の親ディレクトリ

	auto parentDir{ JoinStrings(tokens, L'/', true) };
	if (parentDir.empty())
	{
		// バケットのルート・ディレクトリから検索

		// "" --> ""
	}
	else
	{
		// サブディレクトリから検索

		// "dir"        --> "dir/"
		// "dir/subdir" --> "dir/subdir/"

		parentDir += L'/';
	}

	// 検索対象のファイル名 (ディレクトリ名)

	if (argKey.back() == L'/')
	{
		// SplitString() で "/" が除かれてしまうので、argKey に "dir/" や "dir/file.txt/"
		// が指定されているときは filename に "/" を付与

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

std::wstring FileTypeToStr(FileTypeEnum argFileType)
{
	switch (argFileType)
	{
		case FileTypeEnum::None:			return L"None";
		case FileTypeEnum::RootDirectory:	return L"RootDirectory";
		case FileTypeEnum::Bucket:			return L"Bucket";
		case FileTypeEnum::DirectoryObject:	return L"DirectoryObject";
		case FileTypeEnum::FileObject:		return L"FileObject";

		default:
		{
			APP_ASSERT(0);
		}
	}

	return L"";
}

// 文字列のハッシュ値を算出
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

// 前後の空白をトリムする関数
std::wstring TrimW(const std::wstring& str)
{
	std::wstring trimmedStr = str;

	// 先頭の空白をトリム
	trimmedStr.erase(trimmedStr.cbegin(), std::find_if(trimmedStr.cbegin(), trimmedStr.cend(), [](wchar_t ch)
	{
		return !std::isspace(ch);
	}));

	// 末尾の空白をトリム
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