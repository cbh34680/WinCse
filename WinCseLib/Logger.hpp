#pragma once

#include <fstream>


namespace WCSE {

class Logger : public WCSE::ILogger
{
private:
	std::wstring mTempDir;

	// ���O�o�̓f�B���N�g�� (�v���O�������� "-T")
	std::wstring mTraceLogDir;
	bool mTraceLogEnabled = false;

	// ���O�p�t�@�C�� (�X���b�h�E���[�J��)
	static thread_local std::wofstream mTLFile;
	static thread_local bool mTLFileOK;
	static thread_local UINT64 mTLFlushTime;

	// hidden
	Logger() = default;
	~Logger() = default;

	bool internalInit(const std::wstring& argTempDir,
		const std::wstring& argTrcDir, const std::wstring& dllType) noexcept;

protected:
	void traceW_write(const SYSTEMTIME* st, PCWSTR buf) const noexcept;

public:
	PCWSTR getOutputDirectory() const noexcept override
	{
		if (mTraceLogEnabled)
		{
			return mTraceLogDir.c_str();
		}

		return nullptr;
	}

	// ���O�o��
	void traceA_impl(int indent, PCSTR, int, PCSTR, PCSTR format, ...) const noexcept override;
	void traceW_impl(int indent, PCWSTR, int, PCWSTR, PCWSTR format, ...) const noexcept override;

	// friend
	friend bool CreateLogger(PCWSTR argTempDir, PCWSTR argTrcDir, PCWSTR argDllType);
	friend ILogger* GetLogger();
	friend void DeleteLogger();
};

}

// EOF