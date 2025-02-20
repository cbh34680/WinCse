#pragma once

#include <string>
#include <fstream>

class Logger : public ILogger
{
private:
	const std::wstring mTempDir;

	// ログ出力ディレクトリ (プログラム引数 "-T")
	std::wstring mTraceLogDir;
	bool mTraceLogEnabled = false;

	// ログ用ファイル (スレッド・ローカル)
	static thread_local std::wofstream mTLFile;
	static thread_local bool mTLFileOK;
	static thread_local uint64_t mTLFlushTime;

protected:
	void traceW_write(const SYSTEMTIME* st, const wchar_t* buf) const;

public:
	Logger(const wchar_t* tmpdir) : mTempDir(tmpdir) { }
	bool SetOutputDir(const wchar_t* path);

	// ログ出力
	void traceA_impl(const int indent, const char*, const int, const char*, const char* format, ...) override;
	void traceW_impl(const int indent, const wchar_t*, const int, const wchar_t*, const wchar_t* format, ...) override;
};

// EOF