#pragma once

#include "CSDriverCommon.h"

namespace CSEDRV
{

class FileContext : public CSELIB::IFileContext
{
private:
	std::filesystem::path	mWinPath;
	CSELIB::DirEntryType	mDirEntry;

public:
	PVOID					mDirBuffer = nullptr;
	mutable DWORD			mFlags = 0;

	FileContext(const std::filesystem::path& argWinPath, const CSELIB::DirEntryType& argDirEntry)
		:
		mWinPath(argWinPath),
		mDirEntry(argDirEntry)
	{
	}

	void rename(const std::filesystem::path& argWinPath, const CSELIB::DirEntryType& argDirEntry)
	{
		mWinPath = argWinPath;
		mDirEntry = argDirEntry;
	}

	virtual ~FileContext()
	{
		FspFileSystemDeleteDirectoryBuffer(&mDirBuffer);
	}

	const std::filesystem::path& getWinPath() const
	{
		return mWinPath;
	}

	const CSELIB::DirEntryType& getDirEntry() const
	{
		return mDirEntry;
	}

	virtual HANDLE getHandle() = 0;
	virtual void closeHandle() = 0;

	virtual HANDLE getWritableHandle()
	{
		return INVALID_HANDLE_VALUE;
	}

	CSELIB::ObjectKey getObjectKey() const;

	virtual std::wstring str() const;
};

class RefFileContext final : public FileContext
{
	HANDLE mHandle;

public:
	RefFileContext(const std::filesystem::path& argWinPath,
		const CSELIB::DirEntryType& argDirEntry, HANDLE argHandle)
		:
		FileContext(argWinPath, argDirEntry),
		mHandle(argHandle)
	{
	}

	HANDLE getHandle() override
	{
		return mHandle;
	}

	void closeHandle() override
	{
		mHandle = INVALID_HANDLE_VALUE;
	}
};

class OpenFileContext final : public FileContext
{
	CSELIB::FileHandle mFile;

public:
	OpenFileContext(const std::filesystem::path& argWinPath,
		const CSELIB::DirEntryType& argDirEntry, CSELIB::FileHandle&& argFile)
		:
		FileContext(argWinPath, argDirEntry),
		mFile(std::move(argFile))
	{
	}

	HANDLE getHandle() override
	{
		return mFile.handle();
	}

	HANDLE getWritableHandle() override
	{
		return mFile.handle();
	}

	void closeHandle() override
	{
		mFile.close();
	}

	std::wstring str() const override;
};

}	// namespace CSELIB

#define FCTX_FLAGS_MODIFY					(0x1)

#define FCTX_FLAGS_M_CREATE					(0x10)
#define FCTX_FLAGS_M_WRITE									(FCTX_FLAGS_M_CREATE << 1)
#define FCTX_FLAGS_M_OVERWRITE								(FCTX_FLAGS_M_CREATE << 2)
#define FCTX_FLAGS_M_RENAME									(FCTX_FLAGS_M_CREATE << 3)
#define FCTX_FLAGS_M_SET_BASIC_INFO							(FCTX_FLAGS_M_CREATE << 4)
#define FCTX_FLAGS_M_SET_FILE_SIZE							(FCTX_FLAGS_M_CREATE << 5)
#define FCTX_FLAGS_M_SET_SECURITY							(FCTX_FLAGS_M_CREATE << 6)

#define FCTX_FLAGS_OPEN						(0x10000)
#define FCTX_FLAGS_CLEANUP					(FCTX_FLAGS_OPEN << 1)
#define FCTX_FLAGS_READ						(FCTX_FLAGS_OPEN << 2)
#define FCTX_FLAGS_FLUSH					(FCTX_FLAGS_OPEN << 3)
#define FCTX_FLAGS_GET_FILE_INFO			(FCTX_FLAGS_OPEN << 4)
#define FCTX_FLAGS_GET_SECURITY				(FCTX_FLAGS_OPEN << 5)
#define FCTX_FLAGS_READ_DIRECTORY			(FCTX_FLAGS_OPEN << 6)
#define FCTX_FLAGS_SET_DELETE				(FCTX_FLAGS_OPEN << 7)
#define FCTX_FLAGS_CLOSE					(FCTX_FLAGS_OPEN << 8)

#define FCTX_FLAGS_CREATE					(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_CREATE)
#define FCTX_FLAGS_WRITE					(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_WRITE)
#define FCTX_FLAGS_OVERWRITE				(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_OVERWRITE)
#define FCTX_FLAGS_RENAME					(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_RENAME)
#define FCTX_FLAGS_SET_BASIC_INFO			(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_SET_BASIC_INFO)
#define FCTX_FLAGS_SET_FILE_SIZE			(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_SET_FILE_SIZE)
#define FCTX_FLAGS_SET_SECURITY				(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_SET_SECURITY)

// EOF