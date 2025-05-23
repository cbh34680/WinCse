#pragma once

#include <fstream>

namespace CSELIB {

class Logger final : public ILogger
{
private:
	const std::optional<std::filesystem::path>	mOutputDir;				// ログ出力ディレクトリ (プログラム引数 "-T")

	bool										mPrintScreen = false;	// 画面にも出力する

	static thread_local std::wofstream			mTraceLogStream;		// ログ用ファイル (スレッド・ローカル)
	static thread_local bool					mTraceLogStreamOK;
	static thread_local UTC_MILLIS_T			mTraceLogFlushTime;

	static thread_local std::wofstream			mErrorLogStream;		// ログ用ファイル (スレッド・ローカル)
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

	// ログ出力

	void printAlsoOnScreen(bool argPrintScreen) override
	{
		mPrintScreen = argPrintScreen;
	}

	void writeToTraceLog(std::optional<std::wstring> buf) override;
	void writeToErrorLog(std::optional<std::wstring> buf) override;

	std::optional<std::wstring> makeTextW(int argIndent, PCWSTR argPath, int argLine, PCWSTR argFunc, DWORD argLastError, PCWSTR argFormat, ...) const override;
	std::optional<std::wstring> makeTextA(int argIndent, PCSTR argPath, int argLine, PCSTR argFunc, DWORD argLastError, PCSTR argFormat, ...) const override;

	// friend
	friend ILogger* CreateLogger(PCWSTR argLogDir);
	friend Logger* NewLogger(PCWSTR argLogDir);
	friend ILogger* GetLogger();
	friend void DeleteLogger();
};

}

// EOF