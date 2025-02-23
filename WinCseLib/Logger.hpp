#pragma once

#include <string>
#include <fstream>


namespace WinCseLib {

class Logger : public WinCseLib::ILogger
{
private:
	std::wstring mTempDir;

	// ���O�o�̓f�B���N�g�� (�v���O�������� "-T")
	std::wstring mTraceLogDir;
	bool mTraceLogEnabled = false;

	// ���O�p�t�@�C�� (�X���b�h�E���[�J��)
	static thread_local std::wofstream mTLFile;
	static thread_local bool mTLFileOK;
	static thread_local uint64_t mTLFlushTime;

	// hidden
	Logger() = default;
	~Logger() = default;

	bool init(const std::wstring& argTempDir, const std::wstring& argTrcDir, const std::wstring& dllType);

protected:
	void traceW_write(const SYSTEMTIME* st, const wchar_t* buf) const;

public:

	// ���O�o��
	void traceA_impl(const int indent, const char*, const int, const char*, const char* format, ...) override;
	void traceW_impl(const int indent, const wchar_t*, const int, const wchar_t*, const wchar_t* format, ...) override;

	// friend
	friend WINCSELIB_API bool CreateLogger(const wchar_t* argTempDir, const wchar_t* argTrcDir, const wchar_t* argDllType);
	friend WINCSELIB_API ILogger* GetLogger();
	friend WINCSELIB_API void DeleteLogger();
};

}

// EOF