#include "WinCseLib.h"
#include <sstream>
#include <fstream>
#include <dbghelp.h>

#pragma comment(lib, "Crypt32.lib")             // CryptBinaryToStringA
#pragma comment(lib, "Dbghelp.lib")             // SymInitialize


namespace WinCseLib {

std::wstring WinCseLib::ITask::synonymString()
{
	static std::atomic<int> aint(0);

	std::wstringstream ss;

	ss << MB2WC(typeid(*this).name());
	ss << L"; ";
	ss << aint++;

	return ss.str();
}

void AbnormalEnd(const char* file, const int line, const char* func, const int signum)
{
	wchar_t tempPath[MAX_PATH];
	::GetTempPathW(MAX_PATH, tempPath);

	const DWORD pid = ::GetCurrentProcessId();
	const DWORD tid = ::GetCurrentThreadId();

	std::wstring fpath;

	{
		std::wstringstream ss;
		ss << tempPath;
		ss << L"WinCse-abend-";
		ss << pid;
		ss << L'-';
		ss << tid;
		ss << L".log";

		fpath = ss.str();
	}

	std::ofstream ofs{ fpath, std::ios_base::app };

	{
		std::stringstream ss;

		ss << std::endl;
		ss << file;
		ss << "(";
		ss << line;
		ss << "); signum(";
		ss << signum;
		ss << "); ";
		ss << func;
		ss << std::endl;

		const std::string ss_str{ ss.str() };

		::OutputDebugStringA(ss_str.c_str());

		if (ofs)
		{
			ofs << ss_str;
		}
	}

	const int maxFrames = 62;
	void* stack[maxFrames];
	HANDLE process = ::GetCurrentProcess();
	::SymInitialize(process, NULL, TRUE);

	USHORT frames = ::CaptureStackBackTrace(0, maxFrames, stack, NULL);
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (USHORT i = 0; i < frames; i++)
	{
		::SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);

		std::stringstream ss;
		ss << frames - i - 1;
		ss << ": ";
		ss << symbol->Name;
		ss << " - 0x";
		ss << symbol->Address;
		ss << std::endl;

		const std::string ss_str{ ss.str() };

		::OutputDebugStringA(ss_str.c_str());

		if (ofs)
		{
			ofs << ss_str;
		}
	}

	free(symbol);

	ofs.close();

	abort();
}

#define INI_LINE_BUFSIZ		(1024)

bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue)
{
	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	std::vector<wchar_t> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringW(argSection, keyName, L"", buf.data(), (DWORD)buf.size(), confPath.c_str());

	if (::GetLastError() != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::wstring(buf.data());

	return true;
}

bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue)
{
	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	std::vector<char> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringA(argSection, keyName, "", buf.data(), (DWORD)buf.size(), confPath.c_str());

	if (::GetLastError() != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::string(buf.data());

	return true;
}

//
// LogBlock
//
static std::atomic<int> mCounter(0);
static thread_local int mDepth = 0;


LogBlock::LogBlock(const wchar_t* argFile, const int argLine, const wchar_t* argFunc)
	: file(argFile), line(argLine), func(argFunc)
{
	mCounter++;

	GetLogger()->traceW_impl(mDepth, file, line, func, L"{enter}");
	mDepth++;
}

LogBlock::~LogBlock()
{
	mDepth--;
	GetLogger()->traceW_impl(mDepth, file, -1, func, L"{leave}");
}

int LogBlock::depth()
{
	return mDepth;
}

int LogBlock::getCount()
{
	return mCounter.load();
}

} // namespace WinCseLib

// EOF
