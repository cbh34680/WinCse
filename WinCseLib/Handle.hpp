#pragma once

namespace CSELIB {

// HANDLE —p RAII

template<HANDLE InvalidHandleValue>
class HandleInternal
{
protected:
	HANDLE mHandle;

public:
	HandleInternal() : mHandle(InvalidHandleValue) { }

	HandleInternal(HANDLE argHandle) : mHandle(argHandle) { }

	HandleInternal(HandleInternal& other) = delete;

	HandleInternal(HandleInternal&& other) noexcept : mHandle(other.mHandle)
	{
		other.mHandle = InvalidHandleValue;
	}

	HandleInternal& operator=(HandleInternal& other) = delete;

	HandleInternal& operator=(HandleInternal&& other)
	{
		if (this != &other)
		{
			this->close();

			mHandle = other.mHandle;
			other.mHandle = InvalidHandleValue;
		}

		return *this;
	}

	HANDLE handle() const { return mHandle; }
	bool invalid() const { return mHandle == InvalidHandleValue; }
	bool valid() const { return !invalid(); }

	HANDLE release()
	{
		const auto ret = mHandle;
		mHandle = InvalidHandleValue;
		return ret;
	}

	void close()
	{
		if (mHandle != InvalidHandleValue)
		{
			::CloseHandle(mHandle);
			mHandle = InvalidHandleValue;
		}
	}

	virtual ~HandleInternal()
	{
		this->close();
	}
};

class FileHandle final : public HandleInternal<INVALID_HANDLE_VALUE>
{
public:
	using HandleInternal::HandleInternal;

	WINCSELIB_API std::wstring str() const;
};

class EventHandle final : public HandleInternal<(HANDLE)NULL>
{
public:
	using HandleInternal<(HANDLE)NULL>::HandleInternal;
};


}	// namespace CSELIB

// EOF