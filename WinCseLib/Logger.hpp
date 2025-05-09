#pragma once

#include <fstream>

namespace CSELIB {

class Logger final : public ILogger
{
private:
	const std::optional<std::filesystem::path>	mOutputDir;			// ���O�o�̓f�B���N�g�� (�v���O�������� "-T")

	static thread_local std::wofstream			mTraceLogStream;	// ���O�p�t�@�C�� (�X���b�h�E���[�J��)
	static thread_local bool					mTraceLogStreamOK;
	static thread_local UTC_MILLIS_T			mTraceLogFlushTime;

	static thread_local std::wofstream			mErrorLogStream;	// ���O�p�t�@�C�� (�X���b�h�E���[�J��)
	static thread_local bool					mErrorLogStreamOK;
	static thread_local UTC_MILLIS_T			mErrorLogFlushTime;

	static Logger*								mInstance;

	// hidden
	Logger(const std::optional<std::filesystem::path>& argTraceLogDir)
		:
		mOutputDir(argTraceLogDir)
	{
	}

	~Logger() = default;

public:
	PCWSTR getOutputDirectory() const override
	{
		if (mOutputDir)
		{
			return mOutputDir->c_str();
		}

		return nullptr;
	}

	// ���O�o��

	void writeToTraceLog(std::optional<std::wstring> buf) override;
	void writeToErrorLog(std::optional<std::wstring> buf) override;

	std::optional<std::wstring> makeTextW(int argIndent, PCWSTR argPath, int argLine, PCWSTR argFunc, DWORD argLastError, PCWSTR argFormat, ...) const override;
	std::optional<std::wstring> makeTextA(int argIndent, PCSTR argPath, int argLine, PCSTR argFunc, DWORD argLastError, PCSTR argFormat, ...) const override;

	// friend
	friend ILogger* CreateLogger(PCWSTR argTraceLogDir);
	friend ILogger* GetLogger();
	friend void DeleteLogger();
};

}

// EOF