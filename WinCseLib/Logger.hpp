#pragma once

#include <fstream>

namespace CSELIB {

class Logger final : public ILogger
{
private:
	const std::optional<std::filesystem::path>	mTraceLogDir;		// ���O�o�̓f�B���N�g�� (�v���O�������� "-T")

	static thread_local std::wofstream			mTLFile;			// ���O�p�t�@�C�� (�X���b�h�E���[�J��)
	static thread_local bool					mTLFileOK;
	static thread_local UTC_MILLIS_T			mTLFlushTime;
	static Logger*								mInstance;

	// hidden
	Logger(const std::optional<std::filesystem::path>& argTraceLogDir) noexcept
		:
		mTraceLogDir(argTraceLogDir)
	{
	}

	~Logger() = default;

protected:
	void traceW_write(const SYSTEMTIME* st, PCWSTR buf) const noexcept;

public:
	PCWSTR getOutputDirectory() const noexcept override
	{
		if (mTraceLogDir)
		{
			return mTraceLogDir->c_str();
		}

		return nullptr;
	}

	// ���O�o��
	void traceA_impl(int indent, PCSTR, int, PCSTR, PCSTR format, ...) const noexcept override;
	void traceW_impl(int indent, PCWSTR, int, PCWSTR, PCWSTR format, ...) const noexcept override;

	// friend
	friend ILogger* CreateLogger(PCWSTR argTraceLogDir);
	friend ILogger* GetLogger();
	friend void DeleteLogger();
};

}

// EOF