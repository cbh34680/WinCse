#include "WinCseLib.h"
#include "Logger.hpp"
#include <filesystem>
#include <sstream>
#include <iostream>

#undef traceW
#undef traceA


namespace WinCseLib {

//
// �X���b�h�E���[�J���ϐ��̏�����
//
thread_local std::wofstream Logger::mTLFile;
thread_local bool Logger::mTLFileOK = true;
thread_local uint64_t Logger::mTLFlushTime = 0;

//
// �v���O�������� "-T" �Ŏw�肳�ꂽ�f�B���N�g�������O�o�͗p�ɕۑ�����
//
bool Logger::internalInit(const std::wstring& argTempDir, const std::wstring& argTrcDir, const std::wstring& argDllType)
{
	namespace fs = std::filesystem;

	APP_ASSERT(fs::exists(argTempDir) && fs::is_directory(argTempDir));

	mTempDir = argTempDir;

	if (!argTrcDir.empty())
	{
		// "-T" �����ŏo�̓f�B���N�g���̎w�肪����

		const auto trcDir{ fs::weakly_canonical(fs::path(argTrcDir)) };

		if (fs::exists(trcDir) && fs::is_directory(trcDir))
		{
			// �w�肳�ꂽ�f�B���N�g�������p�ł���Ƃ� (�ʏ�͂���)

			mTraceLogDir = trcDir.wstring();
		}
		else
		{
			// �w�肳�ꂽ�f�B���N�g�������p�ł��Ȃ��Ƃ��́A��ւƂ��ăV�X�e����
			// �A�v���P�[�V�����p�ꎞ�f�B���N�g�� (argTempDir) �Ƀ��O�p��
			// �f�B���N�g�����쐬���A����𗘗p����

			auto altTrcDir{ mTempDir + L'\\' + argDllType + L"\\log" };

			if (MkdirIfNotExists(altTrcDir))
			{
				mTraceLogDir = altTrcDir;
			}
			else
			{
				// ������o���Ȃ��Ƃ��� argTempDir �����O�p�ɗ��p����

				mTraceLogDir = argTempDir;
			}
		}

		APP_ASSERT(!mTraceLogDir.empty());
		mTraceLogEnabled = true;
	}

	return true;
}

// �f�o�b�O�p���O�o��

#define FORMAT_DT	"%02d:%02d:%02d.%03d"
#define FORMAT_ERR	"%-3lu"
#define FORMAT_SRC	"%-32s"
#define FORMAT_FUNC	"%-36s"

#define FORMAT1		FORMAT_DT "\t" FORMAT_ERR "\t" FORMAT_SRC "\t" FORMAT_FUNC "\t"
#define FORMAT2		"\n"

void Logger::traceW_impl(const int indent, const wchar_t* fullPath, const int line, const wchar_t* func, const wchar_t* format, ...)
{
#ifdef _RELEASE
	if (!mTraceLogEnabled)
		return;
#endif

	APP_ASSERT(indent >= 0);

	const auto err = ::GetLastError();
	const auto callFromFile(std::filesystem::path(fullPath).filename().wstring());
	const wchar_t* file = callFromFile.c_str();

	SYSTEMTIME st;
	::GetLocalTime(&st);

	std::wstringstream ss;
	ss << file << L'(' << line << L')';
	const auto src{ ss.str() };

	va_list args;
	va_start(args, format);

	size_t bufsiz = 1;	// terminate
	bufsiz += swprintf(nullptr, 0, L"" FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, err, src.c_str(), func);
	bufsiz += indent;
	bufsiz += vswprintf(nullptr, 0, format, args);
	bufsiz += wcslen(L"" FORMAT2);

	std::vector<wchar_t> vbuf(bufsiz);
	wchar_t* buf = vbuf.data();

	auto remain = bufsiz;
	remain -= swprintf(&buf[bufsiz - remain], remain, L"" FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, err, src.c_str(), func);

	wchar_t* pos = &buf[bufsiz - remain];
	for (int i = 0; i < indent; i++, pos++)
	{
		*pos = L'\t';

	}
	remain -= indent;

	remain -= vswprintf(&buf[bufsiz - remain], remain, format, args);
	remain -= swprintf(&buf[bufsiz - remain], remain, L"" FORMAT2);
	APP_ASSERT(remain == 1);

	va_end(args);

	traceW_write(&st, buf);
}

void Logger::traceA_impl(const int indent, const char* fullPath, const int line, const char* func, const char* format, ...)
{
#ifdef _RELEASE
	if (!mTraceLogEnabled)
		return;
#endif

	APP_ASSERT(indent >= 0);

	const auto err = ::GetLastError();
	const auto callFromFile(std::filesystem::path(fullPath).filename().string());
	const char* file = callFromFile.c_str();

	SYSTEMTIME st;
	::GetLocalTime(&st);

	std::stringstream ss;
	ss << file << '(' << line << ')';
	const auto src{ ss.str() };

	va_list args;
	va_start(args, format);

	size_t bufsiz = 1;	// terminate
	bufsiz += snprintf(nullptr, 0, FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, err, src.c_str(), func);
	bufsiz += indent;
	bufsiz += vsnprintf(nullptr, 0, format, args);
	bufsiz += strlen(FORMAT2);

	std::vector<char> vbuf(bufsiz);
	char* buf = vbuf.data();

	auto remain = bufsiz;
	remain -= snprintf(&buf[bufsiz - remain], remain, FORMAT1, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, err, src.c_str(), func);

	char* pos = &buf[bufsiz - remain];
	for (int i = 0; i < indent; i++, pos++)
	{
		*pos = '\t';

	}
	remain -= indent;

	remain -= vsnprintf(&buf[bufsiz - remain], remain, format, args);
	remain -= snprintf(&buf[bufsiz - remain], remain, FORMAT2);
	APP_ASSERT(remain == 1);

	va_end(args);

	traceW_write(&st, MB2WC(buf).c_str());
}

#pragma warning(suppress: 4100)
void Logger::traceW_write(const SYSTEMTIME* st, const wchar_t* buf) const
{
	const auto pid = ::GetCurrentProcessId();
	const auto tid = ::GetCurrentThreadId();

	{
		std::wstringstream ss;

		ss << L"| ";
		ss << std::setw(3) << (tid % 1000);
		ss << L' ' << buf;

		::OutputDebugStringW(ss.str().c_str());
	}

	if (!mTraceLogEnabled)
		return;

	//
	// mLog.TLFile �̓X���b�h�E���[�J���Ȃ̂ŁA���s���ꂽ�X���b�h���Ƃ�
	// ���O�t�@�C�����쐬����A�����ɏo�͂��s���邽�ߔr������͕s�v
	//
	if (mTLFileOK)
	{
		if (!mTLFile.is_open())
		{
			std::wstringstream ss;

#ifdef _DEBUG
			ss << mTraceLogDir << L"\\trace-";
			ss << tid % 1000 << L'-';

#else
			ss << mTraceLogDir << L"\\WinCse-trace-";
			ss << std::setw(4) << std::setfill(L'0') << st->wYear;
			ss << std::setw(2) << std::setfill(L'0') << st->wMonth;
			ss << std::setw(2) << std::setfill(L'0') << st->wDay;
			ss << L'-';
#endif

			ss << pid << L'-' << tid << L".log";
			const std::wstring path = ss.str();

#ifdef _RELEASE
			std::wcout << L"Open trace log file: " << path << std::endl;
#endif
			// ������Ȃ��Ɠ��{�ꂪ�o�͂ł��Ȃ�
			mTLFile.imbue(std::locale("", LC_ALL));

			mTLFile.open(path, std::ios_base::app);
			::SetLastError(ERROR_SUCCESS);

			if (!mTLFile)
			{
				// �J���Ȃ�������ȍ~�͎��݂Ȃ�

				std::wcerr << L"errno: " << errno << std::endl;

				mTLFileOK = false;
				return;
			}

			mTLFlushTime = GetCurrentUtcMillis();
		}
	}

	if (mTLFile)
	{
		mTLFile << buf;

#ifdef _DEBUG
		// �f�o�b�O�ɕs�ւȂ̂ŕ��Ă��܂�
		mTLFile.close();

#else
		const auto now{ GetCurrentUtcMillis() };

		if (now - mTLFlushTime > (60ULL * 1 * 1000))
		{
			// 1 ���Ɉ�x���x�� flush ����

			mTLFile.flush();

			mTLFlushTime = now;
		}
#endif
	}
}

// single instance
static Logger* gLogger;

bool CreateLogger(const wchar_t* argTempDir, const wchar_t* argTrcDir, const wchar_t* argDllType)
{
	APP_ASSERT(argTempDir);
	APP_ASSERT(argDllType);

	if (gLogger)
	{
		return false;
	}

	const std::wstring tmpDir{ argTempDir };
	const std::wstring trcDir{ argTrcDir ? argTrcDir : L"" };
	const std::wstring dllType{ argDllType };

	Logger* logger = new Logger();
	if (!logger->internalInit(tmpDir, trcDir, dllType))
	{
		return false;
	}

	gLogger = logger;
	return true;
}

ILogger* GetLogger()
{
	APP_ASSERT(gLogger);

	return gLogger;
}

void DeleteLogger()
{
	delete gLogger;
	gLogger = nullptr;
}

} // namespace WinCseLib

// EOF