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

// �X���b�h�E���[�J���ϐ��̏�����

ILogger* CreateLogger(PCWSTR argTraceLogDir)
{
	// �v���O�������� "-T" �Ŏw�肳�ꂽ�f�B���N�g�������O�o�͗p�ɕۑ�����

	if (!Logger::mInstance)
	{
		std::filesystem::path dir;

		if (argTraceLogDir)
		{
			// "-T" �����ŏo�̓f�B���N�g���̎w�肪����

			std::error_code ec;
			const auto ok = std::filesystem::is_directory(argTraceLogDir, ec);

			if (ok)
			{
				// �w�肳�ꂽ�f�B���N�g�������p�ł���Ƃ� (�ʏ�͂���)

				dir = argTraceLogDir;

				std::wcout << L"set trace-log-dir=" << dir.wstring() << std::endl;
			}
			else
			{
				// �w�肳�ꂽ�f�B���N�g�������p�ł��Ȃ��Ƃ��́A��ւƂ��ăV�X�e���̃e���|�����E�f�B���N�g����
				// ���O�o�͗p�f�B���N�g�����쐬���A����𗘗p����

				wchar_t tmpdir[MAX_PATH];
				const auto len = ::GetTempPathW(MAX_PATH, tmpdir);

				if (len)
				{
					if (tmpdir[len - 1] == L'\\')
					{
						tmpdir[len - 1] = L'\0';
					}

					wcscat_s(tmpdir, L"\\WinCse\\log");

					if (mkdirIfNotExists(tmpdir))
					{
						dir = tmpdir;

						std::wcout << L"set trace-log-dir=" << dir.wstring() << std::endl;
					}
					else
					{
						std::wcerr << L"fault: mkdirIfNotExists tmpdir=" << tmpdir << std::endl;
					}
				}
				else
				{
					std::wcerr << L"fault: GetTempPath" << std::endl;
				}
			}
		}
		else
		{
			std::wcout << L"no logger" << std::endl;
		}

		Logger::mInstance = new Logger{ dir.empty() ? std::nullopt : std::optional{ dir } };
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

// �O���[�o���̈�

struct OutputTarget
{
	// ����) �قƂ�ǎQ��

	const std::optional<std::filesystem::path>&		outputDir;
	PCWSTR											prefix;
	std::wofstream&									stream;
	bool&											streamOK;
	UTC_MILLIS_T&									flushTime;

	OutputTarget(const std::optional<std::filesystem::path>& argOutputDir, PCWSTR argPrefix, std::wofstream& argStream, bool& argStreamOK, UTC_MILLIS_T& argFlushTime)
		:
		outputDir(argOutputDir),
		prefix(argPrefix),
		stream(argStream),
		streamOK(argStreamOK),
		flushTime(argFlushTime)
	{
	}
};

#pragma warning(suppress: 4100)
static void writeTextToTarget(const std::wstring& argText, const OutputTarget& target)
{
	LastErrorBackup _backup;

	const auto pid = ::GetCurrentProcessId();
	const auto tid = ::GetCurrentThreadId();
	const auto now = GetCurrentUtcMillis();

	SYSTEMTIME st;
	::GetLocalTime(&st);

#ifdef _DEBUG
	std::wostringstream ssDebug;

	ssDebug << L"| ";
	ssDebug << std::setw(3) << (tid % 1000);
	ssDebug << L' ' << argText;

	::OutputDebugStringW(ssDebug.str().c_str());

#endif
	if (!target.outputDir)
	{
		return;
	}

	//
	// mLog.TLFile �̓X���b�h�E���[�J���Ȃ̂ŁA���s���ꂽ�X���b�h���Ƃ�
	// ���O�t�@�C�����쐬����A�����ɏo�͂��s���邽�ߔr������͕s�v
	//
	if (target.streamOK)
	{
		if (!target.stream.is_open())
		{
			std::wostringstream ss;

#ifdef _DEBUG
			ss << target.prefix;
			ss << L"-";
			ss << tid % 1000 << L'-';

#else
			ss << L"WinCse-";
			ss << target.prefix;
			ss << L"-";
			ss << std::setw(4) << std::setfill(L'0') << st.wYear;
			ss << std::setw(2) << std::setfill(L'0') << st.wMonth;
			ss << std::setw(2) << std::setfill(L'0') << st.wDay;
			ss << L'-';
#endif
			ss << pid << L'-' << tid << L".log";

			const auto path{ (*target.outputDir / ss.str()).wstring() };

#ifdef _RELEASE
			std::wcout << L"Open trace log file: " << path << std::endl;
#endif
			// ���ꂪ�Ȃ��Ɠ��{�ꂪ�o�͂ł��Ȃ�

			target.stream.imbue(std::locale("", LC_ALL));

			// �ǉ��������݂ŃI�[�v��

			target.stream.open(path, std::ios_base::app);

			if (!target.stream)
			{
				// �J���Ȃ�������ȍ~�͎��݂Ȃ�

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
		// �f�o�b�O�ɕs�ւȂ̂Ŗ�����Ă��܂�

		target.stream.close();
		target.flushTime = now;

#else
		if (now - target.flushTime > TIMEMILLIS_1MINll)
		{
			// 1 ���Ɉ�x���x�� flush ����

			target.stream.flush();
			target.flushTime = now;
		}

#endif
	}
}


// Logger �N���X�̎���

thread_local std::wofstream	Logger::mTraceLogStream;
thread_local bool			Logger::mTraceLogStreamOK = true;
thread_local UTC_MILLIS_T	Logger::mTraceLogFlushTime = 0;

thread_local std::wofstream	Logger::mErrorLogStream;
thread_local bool			Logger::mErrorLogStreamOK = true;
thread_local UTC_MILLIS_T	Logger::mErrorLogFlushTime = 0;

Logger* Logger::mInstance = nullptr;


void Logger::writeToTraceLog(std::optional<std::wstring> optText)
{
	if (optText)
	{
		writeTextToTarget(*optText, { mOutputDir, L"trace", mTraceLogStream, mTraceLogStreamOK, mTraceLogFlushTime });
	}
}

void Logger::writeToErrorLog(std::optional<std::wstring> optText)
{
	if (optText)
	{
		writeTextToTarget(*optText, { mOutputDir, L"error", mErrorLogStream, mErrorLogStreamOK, mErrorLogFlushTime });
	}
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

	LastErrorBackup _backup;

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

	LastErrorBackup _backup;

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