#include "WinCseLib.h"
#include "Logger.hpp"
#include <iostream>

#undef traceW
#undef traceA

#define FORMAT_DT		"%02d:%02d:%02d.%03d"
#define FORMAT_ERR		"%-6lu"
#define FORMAT_SRC		"%-32s"
#define FORMAT_FUNC		"%-64s"

#define FORMAT1			FORMAT_DT "\t" FORMAT_ERR "\t" FORMAT_SRC "\t" FORMAT_FUNC "\t"
#define FORMAT2			"\n"

namespace CSELIB {

// スレッド・ローカル変数の初期化

Logger* NewLogger(PCWSTR argLogDir)
{
	std::optional<std::filesystem::path> optLogDir;

	if (argLogDir)
	{
		// "-T" 引数で出力ディレクトリの指定がある

		std::error_code ec;
		const auto ok = std::filesystem::is_directory(argLogDir, ec);

		if (ok)
		{
			// 指定されたディレクトリが利用できるとき (通常はここ)

			optLogDir = argLogDir;
		}
		else
		{
			std::wcerr << argLogDir << L":  not directory" << std::endl;
		}
	}

	if (!optLogDir)
	{
		// 指定されたディレクトリが利用できないときは、代替としてシステムのテンポラリ・ディレクトリに
		// ログ出力用ディレクトリを作成し、それを利用する

		wchar_t sysTempDir[MAX_PATH];
		const auto len = ::GetTempPathW(MAX_PATH, sysTempDir);

		if (len)
		{
			if (sysTempDir[len - 1] == L'\\')
			{
				sysTempDir[len - 1] = L'\0';
			}

			std::filesystem::path appTempDir{ sysTempDir };
			appTempDir.append(L"WinCse\\log");

			if (mkdirIfNotExists(appTempDir))
			{
				// -T の指定がない場合はここ

				optLogDir = appTempDir;
			}
			else
			{
				const auto lerr = ::GetLastError();
				std::wcerr << L"fault: mkdirIfNotExists tmpdir=" << appTempDir << " lerr=" << lerr << std::endl;

				optLogDir = sysTempDir;
			}
		}
		else
		{
			const auto lerr = ::GetLastError();
			std::wcerr << L"fault: GetTempPath lerr=" << lerr << std::endl;
		}
	}

	if (optLogDir)
	{
		std::wcout << L"set log-directory=" << optLogDir->wstring() << std::endl;
	}
	else
	{
		std::wcout << L"no logger" << std::endl;
	}

	return new Logger{ optLogDir };
}

ILogger* CreateLogger(PCWSTR argLogDir)
{
	// プログラム引数 "-T" で指定されたディレクトリをログ出力用に保存する

	if (!Logger::mInstance)
	{
		Logger::mInstance = NewLogger(argLogDir);
	}

	return Logger::mInstance;
}

ILogger* GetLogger()
{
	APP_ASSERT(Logger::mInstance);

	return Logger::mInstance;
}

void DeleteLogger()
{
	delete Logger::mInstance;
	Logger::mInstance = nullptr;
}

} // namespace CSELIB


using namespace CSELIB;

// グローバル領域

struct OutputTarget
{
	// 注意) ほとんど参照

	const std::optional<std::filesystem::path>&		outputDir;
	PCWSTR											prefix;
	const bool										forceFlush;
	std::wofstream&									stream;
	bool&											streamOK;
	UTC_MILLIS_T&									flushTime;

	OutputTarget(const std::optional<std::filesystem::path>& argOutputDir, PCWSTR argPrefix, bool argForceFlush, std::wofstream& argStream, bool& argStreamOK, UTC_MILLIS_T& argFlushTime)
		:
		outputDir(argOutputDir),
		prefix(argPrefix),
		forceFlush(argForceFlush),
		stream(argStream),
		streamOK(argStreamOK),
		flushTime(argFlushTime)
	{
	}
};

#pragma warning(suppress: 4100)
static void writeTextToTarget(const std::wstring& argText, const OutputTarget& target)
{
	KeepLastError _keep;

	const auto pid = ::GetCurrentProcessId();
	const auto tid = ::GetCurrentThreadId();
	const auto now = GetCurrentUtcMillis();

	SYSTEMTIME st;
	::GetLocalTime(&st);

#ifdef _DEBUG
	std::wostringstream ssDebug;

	ssDebug << L"|" << target.prefix << L"| ";
	ssDebug << std::setw(3) << (tid % 1000);
	ssDebug << L' ' << argText;

	::OutputDebugStringW(ssDebug.str().c_str());

#endif
	if (!target.outputDir)
	{
		return;
	}

	//
	// mLog.TLFile はスレッド・ローカルなので、実行されたスレッドごとに
	// ログファイルが作成され、そこに出力が行われるため排他制御は不要
	//
	if (target.streamOK)
	{
		if (!target.stream.is_open())
		{
			std::wostringstream ss;

			ss << L"WinCse-" << target.prefix << L"-";
			ss << std::setw(4) << std::setfill(L'0') << st.wYear;
			ss << std::setw(2) << std::setfill(L'0') << st.wMonth;
			ss << std::setw(2) << std::setfill(L'0') << st.wDay;
			ss << L'-' << pid << L'-' << tid << L".log";

			const auto path{ (*target.outputDir / ss.str()).wstring() };

#ifdef _RELEASE
			std::wcout << L"Open trace log file: " << path << std::endl;
#endif
			// これがないと日本語が出力できない

			target.stream.imbue(std::locale("", LC_ALL));

			// 追加書き込みでオープン

			target.stream.open(path, std::ios_base::app);

			if (!target.stream)
			{
				// 開けなかったら以降は試みない

				std::wcerr << L"fault: open errno=" << errno << std::endl;
				target.streamOK = false;

				return;
			}

			target.flushTime = now;
		}
	}

	if (target.stream)
	{
		target.stream << argText;

#ifdef _DEBUG
		// デバッグに不便なので毎回閉じてしまう

		target.stream.close();
		target.flushTime = now;

#else
		bool doFlush = false;

		if (target.forceFlush)
		{
			doFlush = true;
		}
		else if (now - target.flushTime > TIMEMILLIS_1MINll)
		{
			// 1 分に一度程度は flush する

			doFlush = true;
		}

		if (doFlush)
		{
			target.stream.flush();
			target.flushTime = now;
		}
#endif
	}
}


// Logger クラスの実装

thread_local std::wofstream	Logger::mTraceLogStream;
thread_local bool			Logger::mTraceLogStreamOK = true;
thread_local UTC_MILLIS_T	Logger::mTraceLogFlushTime = 0;

thread_local std::wofstream	Logger::mErrorLogStream;
thread_local bool			Logger::mErrorLogStreamOK = true;
thread_local UTC_MILLIS_T	Logger::mErrorLogFlushTime = 0;

Logger* Logger::mInstance = nullptr;


void Logger::writeToTraceLog(std::optional<std::wstring> optText)
{
	if (!optText)
	{
		return;
	}

	if (this->mPrintScreen)
	{
		std::wcout << L"|trace| " << *optText;
	}

	writeTextToTarget(*optText, { mOutputDir, L"trace", false, mTraceLogStream, mTraceLogStreamOK, mTraceLogFlushTime });
}

void Logger::writeToErrorLog(std::optional<std::wstring> optText)
{
	if (!optText)
	{
		return;
	}

	if (this->mPrintScreen)
	{
		std::wcout << L"|error| " << *optText;
	}

	writeTextToTarget(*optText, { mOutputDir, L"error", true, mErrorLogStream, mErrorLogStreamOK, mErrorLogFlushTime });
}

std::optional<std::wstring> Logger::makeTextW(int argIndent, PCWSTR argPath, int argLine, PCWSTR argFunc, DWORD argLastError, PCWSTR argFormat, ...) const 
{
	APP_ASSERT(argIndent >= 0);

#ifdef _RELEASE
	if (!mOutputDir)
	{
		return std::nullopt;
	}
#endif

	KeepLastError _keep;

	SYSTEMTIME st;
	::GetLocalTime(&st);

	const auto src{ std::filesystem::path(argPath).filename().wstring() + L'(' + std::to_wstring(argLine) + L')' };

	va_list args;
	va_start(args, argFormat);

	size_t bufsiz = 1;	// terminate
	bufsiz += swprintf(nullptr, 0, L"" FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, argLastError, src.c_str(), argFunc);
	bufsiz += argIndent;
	bufsiz += vswprintf(nullptr, 0, argFormat, args);
	bufsiz += wcslen(L"" FORMAT2);

	std::vector<wchar_t> vbuf(bufsiz);
	auto* buf = vbuf.data();

	long remain = static_cast<long>(bufsiz);
	remain -= swprintf(&buf[bufsiz - remain], remain, L"" FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, argLastError, src.c_str(), argFunc);

	auto* pos = &buf[bufsiz - remain];
	for (int i = 0; i < argIndent; i++, pos++)
	{
		*pos = L'\t';

	}
	remain -= argIndent;

	remain -= vswprintf(&buf[bufsiz - remain], remain, argFormat, args);
	remain -= swprintf(&buf[bufsiz - remain], remain, L"" FORMAT2);
	APP_ASSERT(remain == 1);

	va_end(args);

	return buf;
}

std::optional<std::wstring> Logger::makeTextA(int argIndent, PCSTR argPath, int argLine, PCSTR argFunc, DWORD argLastError, PCSTR argFormat, ...) const 
{
	APP_ASSERT(argIndent >= 0);

#ifdef _RELEASE
	if (!mOutputDir)
	{
		return std::nullopt;
	}
#endif

	KeepLastError _keep;

	SYSTEMTIME st;
	::GetLocalTime(&st);

	const auto src{ std::filesystem::path(argPath).filename().string() + '(' + std::to_string(argLine) + ')' };

	va_list args;
	va_start(args, argFormat);

	size_t bufsiz = 1;	// terminate
	bufsiz += snprintf(nullptr, 0, FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, argLastError, src.c_str(), argFunc);
	bufsiz += argIndent;
	bufsiz += vsnprintf(nullptr, 0, argFormat, args);
	bufsiz += strlen(FORMAT2);

	std::vector<char> vbuf(bufsiz);
	auto* buf = vbuf.data();

	long remain = static_cast<long>(bufsiz);
	remain -= snprintf(&buf[bufsiz - remain], remain, FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, argLastError, src.c_str(), argFunc);

	auto* pos = &buf[bufsiz - remain];
	for (int i = 0; i < argIndent; i++, pos++)
	{
		*pos = '\t';

	}
	remain -= argIndent;

	remain -= vsnprintf(&buf[bufsiz - remain], remain, argFormat, args);
	remain -= snprintf(&buf[bufsiz - remain], remain, FORMAT2);
	APP_ASSERT(remain == 1);

	va_end(args);

	return MB2WC(buf);
}

// EOF