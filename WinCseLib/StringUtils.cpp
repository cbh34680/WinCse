#include "WinCseLib.h"
#include <iomanip>
#include <cwctype>
#include <algorithm>
//#include <locale>

namespace WCSE {

// wstring から string への変換
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

// string から wstring への変換
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

} // WCSE

// EOF