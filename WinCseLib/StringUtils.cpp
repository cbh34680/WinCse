#include "WinCseLib.h"
#include <Windows.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cwctype>

namespace WinCseLib {

// wstring から string への変換
//#define CNV_CODEPAGE		(CP_ACP)
#define CNV_CODEPAGE		(CP_UTF8)

std::string WC2MB(const std::wstring& wstr)
{
	if (wstr.empty())
	{
		return "";
	}

	const wchar_t* pWstr = wstr.c_str();

	::SetLastError(ERROR_SUCCESS);
	const int need = ::WideCharToMultiByte(CNV_CODEPAGE, 0, pWstr, -1, NULL, 0, NULL, NULL);
	APP_ASSERT(::GetLastError() == ERROR_SUCCESS);

	std::vector<char> buff(need);
	char* pStr = buff.data();

	::WideCharToMultiByte(CNV_CODEPAGE, 0, pWstr, -1, pStr, need, NULL, NULL);
	APP_ASSERT(::GetLastError() == ERROR_SUCCESS);

	return std::string{ pStr };
}

// string から wstring への変換
std::wstring MB2WC(const std::string& str)
{
	if (str.empty())
	{
		return L"";
	}

	const char* pStr = str.c_str();

	::SetLastError(ERROR_SUCCESS);
	const int need = ::MultiByteToWideChar(CNV_CODEPAGE, 0, pStr, -1, NULL, 0);
	APP_ASSERT(::GetLastError() == ERROR_SUCCESS);

	std::vector<wchar_t> buff(need);
	wchar_t* pWstr = buff.data();

	::MultiByteToWideChar(CNV_CODEPAGE, 0, pStr, -1, pWstr, need);
	APP_ASSERT(::GetLastError() == ERROR_SUCCESS);

	return std::wstring{ pWstr };
}

// 文字列のハッシュ値を算出
size_t HashString(const std::wstring& arg)
{
	static std::hash<std::wstring> hashStr;

	return hashStr(arg);
}

std::string Base64EncodeA(const std::string& data)
{
	DWORD encodedSize = 0;
	BOOL b = ::CryptBinaryToStringA(reinterpret_cast<const BYTE*>(data.c_str()), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &encodedSize);
	APP_ASSERT(b);

	std::vector<char> encodedData(encodedSize);
	b = ::CryptBinaryToStringA(reinterpret_cast<const BYTE*>(data.c_str()), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encodedData.data(), &encodedSize);
	APP_ASSERT(b);

	return std::string(encodedData.begin(), encodedData.end() - 1);
}

std::string Base64DecodeA(const std::string& encodedData)
{
	DWORD decodedSize = 0;
	BOOL b = ::CryptStringToBinaryA(encodedData.c_str(), (DWORD)encodedData.size(), CRYPT_STRING_BASE64, NULL, &decodedSize, NULL, NULL);
	APP_ASSERT(b);

	std::vector<BYTE> decodedData(decodedSize);
	b = ::CryptStringToBinaryA(encodedData.c_str(), (DWORD)encodedData.size(), CRYPT_STRING_BASE64, decodedData.data(), &decodedSize, NULL, NULL);
	APP_ASSERT(b);

	return std::string(decodedData.begin(), decodedData.end());
}

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

// 前後の空白をトリムする関数
std::wstring TrimW(const std::wstring& str)
{
	std::wstring trimmedStr = str;

	// 先頭の空白をトリム
	trimmedStr.erase(trimmedStr.begin(), std::find_if(trimmedStr.begin(), trimmedStr.end(), [](wchar_t ch) {
		return !std::isspace(ch);
	}));

	// 末尾の空白をトリム
	trimmedStr.erase(std::find_if(trimmedStr.rbegin(), trimmedStr.rend(), [](wchar_t ch) {
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

std::vector<std::wstring> SplitW(const std::wstring& input, const wchar_t sep, const bool ignoreEmpty)
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

} // WInCseLib

// EOF