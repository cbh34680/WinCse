#pragma once

#include <fstream>


namespace WCSE {

class Logger : public WCSE::ILogger
{
private:
	std::wstring mTempDir;

	// ログ出力ディレクトリ (プログラム引数 "-T")
	std::wstring mTraceLogDir;
	bool mTraceLogEnabled = false;

	// ログ用ファイル (スレッド・ローカル)
	static thread_local std::wofstream mTLFile;
	static thread_local bool mTLFileOK;
	static thread_local uint64_t mTLFlushTime;

	// hidden
	Logger() = default;
	~Logger() = default;

	bool internalInit(const std::wstring& argTempDir, const std::wstring& argTrcDir, const std::wstring& dllType);

protected:
	void traceW_write(const SYSTEMTIME* st, const wchar_t* buf) const;

public:
	const wchar_t* getOutputDirectory() override
	{
		if (mTraceLogEnabled)
		{
			return mTraceLogDir.c_str();
		}

		return nullptr;
	}

	// ログ出力
	void traceA_impl(const int indent, const char*, const int, const char*, const char* format, ...) override;
	void traceW_impl(const int indent, const wchar_t*, const int, const wchar_t*, const wchar_t* format, ...) override;

	// friend
	friend bool CreateLogger(const wchar_t* argTempDir, const wchar_t* argTrcDir, const wchar_t* argDllType);
	friend ILogger* GetLogger();
	friend void DeleteLogger();
};

}

// EOF