#include "WinCseLib.h"
#include <iostream>
#include <fstream>
#include <dbghelp.h>


namespace CSELIB {

void AbnormalEnd(PCWSTR file, int line, PCWSTR func, int signum)
{
	const auto errno_v = errno;

	SYSTEMTIME st;
	::GetLocalTime(&st);

	wchar_t szTempPath[MAX_PATH];
	::GetTempPathW(MAX_PATH, szTempPath);

	std::wstring tempPath{ szTempPath };

	const DWORD pid = ::GetCurrentProcessId();
	const DWORD tid = ::GetCurrentThreadId();

	std::wostringstream ss;
	ss << tempPath;

	if (std::filesystem::is_directory(tempPath + L"WinCse"))
	{
		ss << L"WinCse\\";
	}
	else
	{
		ss << L"WinCse-";
	}

	ss << L"abend-";
	ss << std::setw(4) << std::setfill(L'0') << st.wYear;
	ss << std::setw(2) << std::setfill(L'0') << st.wMonth;
	ss << std::setw(2) << std::setfill(L'0') << st.wDay;
	ss << L'-';
	ss << pid;
	ss << L'-';
	ss << tid;
	ss << L".log";

	const auto strPath{ ss.str() };
	
	std::wcerr << L"output file=" << strPath << std::endl;
	std::wofstream ofs{ strPath, std::ios_base::app };

	ss.str(L"");
	ss << L"cause; ";
	ss << file;
	ss << L"(";
	ss << line;
	ss << L"); signum=";
	ss << signum;
	ss << L"; ";
	ss << func;
	ss << std::endl;
	ss << L"errno=";
	ss << errno_v;
	ss << std::endl;
	ss << L"GetLastError()=";
	ss << ::GetLastError();
	ss << std::endl;

	const auto causeStr{ ss.str() };

#ifdef _DEBUG
	::OutputDebugStringW(causeStr.c_str());
#endif

	if (ofs)
	{
		ofs << causeStr;
	}

	//
	const int maxFrames = 62;
	void* stack[maxFrames];
	HANDLE hProcess = ::GetCurrentProcess();
	::SymInitialize(hProcess, NULL, TRUE);

	USHORT frames = ::CaptureStackBackTrace(0, maxFrames, stack, NULL);

	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (USHORT i = 0; i < frames; i++)
	{
		::SymFromAddr(hProcess, (DWORD64)(stack[i]), 0, symbol);

		ss.str(L"");
		ss << frames - i - 1;
		ss << L": ";
		ss << symbol->Name;
		ss << L" - 0x";
		ss << symbol->Address;
		ss << std::endl;

		const std::wstring ss_str{ ss.str() };

#ifdef _DEBUG
		::OutputDebugStringW(ss_str.c_str());
#endif

		if (ofs)
		{
			ofs << ss_str;
		}
	}

	if (ofs)
	{
		ofs << std::endl;
	}

	free(symbol);

	ofs.close();

	::SymCleanup(hProcess);

	abort();
}

NTSTATUS HandleToSecurityInfo(HANDLE Handle,
	PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize /* nullable */)
{
	DWORD SecurityDescriptorSizeNeeded = 0;

	if (0 != argSecurityDescriptorSize)
	{
		if (!::GetKernelObjectSecurity(Handle,
			OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			argSecurityDescriptor, (DWORD)*argSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
		{
			*argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
			return FspNtStatusFromWin32(::GetLastError());
		}

		*argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
	}

	return STATUS_SUCCESS;
}

int NamedWorkersToMap(NamedWorker workers[], std::map<std::wstring, IWorker*>* pWorkerMap)
{
	if (!workers)
	{
		return -1;
	}

	int num = 0;

	NamedWorker* cur = workers;
	while (cur->name)
	{
		pWorkerMap->emplace(cur->name, cur->worker);
		cur++;
		num++;
	}

	return num;
}

int GetIniIntW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, int defaultValue, int minValue, int maxValue)
{
	LastErrorBackup _backup;

	const auto section = argSection.c_str();

	APP_ASSERT(section);
	APP_ASSERT(section[0]);

	int ret = ::GetPrivateProfileIntW(section, keyName, defaultValue, confPath.c_str());
	if (ret < minValue)
	{
		ret = minValue;
	}
	else if (ret > maxValue)
	{
		ret = maxValue;
	}

	return ret;
}

bool GetIniBoolW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, bool defaultValue)
{
	LastErrorBackup _backup;

	const auto section = argSection.c_str();

	APP_ASSERT(section);
	APP_ASSERT(section[0]);

	int ret = ::GetPrivateProfileIntW(section, keyName, -1, confPath.c_str());
	if (ret == -1)
	{
		return defaultValue;
	}

	return ret ? true : false;
}

#define INI_LINE_BUFSIZ		(1024)

bool GetIniStringW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, std::wstring* pValue)
{
	LastErrorBackup _backup;

	const auto section = argSection.c_str();

	APP_ASSERT(section);
	APP_ASSERT(section[0]);

	std::vector<WCHAR> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringW(section, keyName, L"", buf.data(), (DWORD)buf.size(), confPath.c_str());
	const auto lerr = ::GetLastError();

	if (lerr != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::wstring(buf.data());

	return true;
}

//

std::wstring FileHandle::str() const
{
	LastErrorBackup _backup;

	if (this->invalid())
	{
		return L"INVALID_HANDLE_VALUE";
	}

	std::filesystem::path cacheFilePath;

	if (!GetFileNameFromHandle(this->handle(), &cacheFilePath))
	{
		throw FatalError(__FUNCTION__, ::GetLastError());
	}

	return cacheFilePath.wstring();
}

// LogBlock

thread_local int LogBlock::mDepth = 0;

LogBlock::LogBlock(PCWSTR argFile, int argLine, PCWSTR argFunc)
	:
	mFile(argFile), mLine(argLine), mFunc(argFunc)
{
	GetLogger()->writeToTraceLog(GetLogger()->makeTextW(mDepth, mFile, mLine, mFunc, ::GetLastError(), L"{enter}"));

	mDepth++;
}

LogBlock::~LogBlock()
{
	mDepth--;

	GetLogger()->writeToTraceLog(GetLogger()->makeTextW(mDepth, mFile, -1, mFunc, ::GetLastError(), L"{leave}"));
}

int LogBlock::depth() const
{
	return mDepth;
}

} // namespace CSELIB

// EOF
