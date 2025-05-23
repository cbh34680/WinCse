#pragma once

namespace CSELIB
{

template<typename ResultT>
class FilePart
{
	EventHandle				mDone;
	ResultT					mResult;

public:
	const int				mPartNumber;
	const FILEIO_OFFSET_T	mOffset;
	const FILEIO_LENGTH_T	mLength;
	const ResultT			mDefaultResult;

	std::atomic<bool>		mInterrupt = false;

	FilePart(int argPartNumber, FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength, ResultT argDefaultResult)
		:
		mPartNumber(argPartNumber),
		mOffset(argOffset),
		mLength(argLength),
		mDefaultResult(argDefaultResult),
		mResult(argDefaultResult)
	{
		mDone = ::CreateEventW(NULL,
			TRUE,				// 手動リセットイベント
			FALSE,				// 初期状態：非シグナル状態
			NULL);

		APP_ASSERT(mDone.valid());
	}

	void setResult(const ResultT& argResult)
	{
		mResult = argResult;
		const auto b = ::SetEvent(mDone.handle());					// シグナル状態に設定
		APP_ASSERT(b);
	}

	void setResult(ResultT&& argResult)
	{
		mResult = std::move(argResult);
		const auto b = ::SetEvent(mDone.handle());					// シグナル状態に設定
		APP_ASSERT(b);
	}

	ResultT getResult()
	{
		const auto reason = ::WaitForSingleObject(mDone.handle(), INFINITE);
		if (reason != WAIT_OBJECT_0)
		{
			return mDefaultResult;
		}

		return mResult;
	}

	std::wstring str() const
	{
		std::wostringstream ss;

		ss << L"mPartNumber=" << mPartNumber;
		ss << L" mOffset=" << mOffset;
		ss << L" mLength=" << mLength;

		return ss.str();
	}
};

}	// namespace CSEDVC

// EOF