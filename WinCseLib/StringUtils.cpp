#include "WinCseLib.h"
#include <sstream>
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

bool Base64EncodeA(const std::string& src, std::string* pDst)
{
	DWORD dstSize = 0;

	BOOL b = ::CryptBinaryToStringA(
		reinterpret_cast<const BYTE*>(src.c_str()), (DWORD)src.size(),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &dstSize);

	if (!b)
	{
		return false;
	}

	std::vector<char> dst(dstSize);

	b = ::CryptBinaryToStringA(
		reinterpret_cast<const BYTE*>(src.c_str()), (DWORD)src.size(),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, dst.data(), &dstSize);

	if (!b)
	{
		return false;
	}

	*pDst = std::string(dst.begin(), dst.end() - 1);

	return true;
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

	*pDst = std::string(dst.begin(), dst.end());

	return true;
}

#if 0
std::string URLEncodeA(const std::string& str)
{
	std::ostringstream encoded;

	for (char ch : str)
	{
		if (isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
		{
			encoded << ch;
		}
		else
		{
			encoded << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)ch;
		}
	}

	return encoded.str();
}

std::string URLDecodeA(const std::string& str)
{
	std::ostringstream decoded;

	for (size_t i = 0; i < str.size(); ++i)
	{
		if (str[i] == '%' && i + 2 < str.size())
		{
			int value;

			std::istringstream iss(str.substr(i + 1, 2));
			if (iss >> std::hex >> value)
			{
				decoded << static_cast<char>(value);
				i += 2;
			}
			else
			{
				decoded << str[i];
			}
		}
		else if (str[i] == '+')
		{
			decoded << ' ';
		}
		else
		{
			decoded << str[i];
		}
	}

	return decoded.str();
}

bool EncodeFileNameToLocalNameA(const std::string& src, std::string* pDst)
{
	std::string dst;

	if (!Base64EncodeA(src, &dst))
	{
		return false;
	}

	*pDst = URLEncodeA(dst);

	return true;
}

bool DecodeLocalNameToFileNameA(const std::string& src, std::string* pDst)
{
	std::string dst;

	if (!Base64DecodeA(URLDecodeA(src), &dst))
	{
		return false;
	}

	*pDst = std::move(dst);

	return true;
}

bool EncodeFileNameToLocalNameW(const std::wstring& src, std::wstring* pDst)
{
	std::string dst;

	if (!Base64EncodeA(WC2MB(src), &dst))
	{
		return false;
	}

	*pDst = MB2WC(URLEncodeA(dst));

	return true;
}

bool DecodeLocalNameToFileNameW(const std::wstring& src, std::wstring* pDst)
{
	std::string dst;

	if (!Base64DecodeA(URLDecodeA(WC2MB(src)), &dst))
	{
		return false;
	}

	*pDst = MB2WC(dst);

	return true;
}
#endif

// 前後の空白をトリムする関数
std::wstring TrimW(const std::wstring& str)
{
	std::wstring trimmedStr = str;

	// 先頭の空白をトリム
	trimmedStr.erase(trimmedStr.begin(), std::find_if(trimmedStr.begin(), trimmedStr.end(), [](wchar_t ch)
	{
		return !std::isspace(ch);
	}));

	// 末尾の空白をトリム
	trimmedStr.erase(std::find_if(trimmedStr.rbegin(), trimmedStr.rend(), [](wchar_t ch)
	{
		return !std::isspace(ch);

	}).base(), trimmedStr.end());

	return trimmedStr;
}

std::string TrimA(const std::string& str)
{
	return WC2MB(TrimW(MB2WC(str)));
}

std::wstring WildcardToRegexW(const std::wstring& wildcard)
{
	std::wstringstream ss;

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

std::string WildcardToRegexA(const std::string& arg)
{
	return WC2MB(WildcardToRegexW(MB2WC(arg)));
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

std::wstring ToUpper(const std::wstring& input)
{
	std::wstring result{ input };

	std::transform(result.begin(), result.end(), result.begin(), 
		[](wchar_t c) { return std::towupper(c); });

	return result;
}

} // WCSE

// EOF