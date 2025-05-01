#pragma once

#include "CSDriverCommon.h"

#define FCTX_FLAGS_MODIFY					(0x1)

#define FCTX_FLAGS_M_CREATE					(0x10)
#define FCTX_FLAGS_M_WRITE									(FCTX_FLAGS_M_CREATE << 1)
#define FCTX_FLAGS_M_OVERWRITE								(FCTX_FLAGS_M_CREATE << 2)
#define FCTX_FLAGS_M_SET_BASIC_INFO							(FCTX_FLAGS_M_CREATE << 3)
#define FCTX_FLAGS_M_SET_FILE_SIZE							(FCTX_FLAGS_M_CREATE << 4)
#define FCTX_FLAGS_M_SET_SECURITY							(FCTX_FLAGS_M_CREATE << 5)

#define FCTX_FLAGS_OPEN						(0x10000)
#define FCTX_FLAGS_CLEANUP					(FCTX_FLAGS_OPEN << 1)
#define FCTX_FLAGS_READ						(FCTX_FLAGS_OPEN << 2)
#define FCTX_FLAGS_FLUSH					(FCTX_FLAGS_OPEN << 3)
#define FCTX_FLAGS_GET_FILE_INFO			(FCTX_FLAGS_OPEN << 4)
#define FCTX_FLAGS_RENAME					(FCTX_FLAGS_OPEN << 5)
#define FCTX_FLAGS_GET_SECURITY				(FCTX_FLAGS_OPEN << 6)
#define FCTX_FLAGS_READ_DIRECTORY			(FCTX_FLAGS_OPEN << 7)
#define FCTX_FLAGS_SET_DELETE				(FCTX_FLAGS_OPEN << 8)
#define FCTX_FLAGS_CLOSE					(FCTX_FLAGS_OPEN << 9)

#define FCTX_FLAGS_CREATE					(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_CREATE)
#define FCTX_FLAGS_WRITE					(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_WRITE)
#define FCTX_FLAGS_OVERWRITE				(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_OVERWRITE)
#define FCTX_FLAGS_SET_BASIC_INFO			(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_SET_BASIC_INFO)
#define FCTX_FLAGS_SET_FILE_SIZE			(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_SET_FILE_SIZE)
#define FCTX_FLAGS_SET_SECURITY				(FCTX_FLAGS_MODIFY | FCTX_FLAGS_M_SET_SECURITY)

namespace CSEDRV
{

class FileContext : public CSELIB::IFileContext
{
public:
	const std::filesystem::path				mWinPath;
	const CSELIB::FileTypeEnum				mFileType;
	const std::optional<CSELIB::ObjectKey>	mOptObjKey;

	FSP_FSCTL_FILE_INFO* const				mFileInfoRef;				// èëÇ´ä∑Ç¶â¬î\ (not const)
	PVOID									mDirBuffer = nullptr;		// ÅV
	mutable DWORD							mFlags = 0;					// ÅV

	FileContext(
		const std::filesystem::path argWinPath,
		CSELIB::FileTypeEnum argFileType,
		FSP_FSCTL_FILE_INFO* argFileInfoRef,
		const std::optional<CSELIB::ObjectKey>& argOptObjKey) noexcept
		:
		mWinPath(argWinPath),
		mFileType(argFileType),
		mFileInfoRef(argFileInfoRef),
		mOptObjKey(argOptObjKey)
	{
		switch (argFileType)
		{
			case CSELIB::FileTypeEnum::RootDirectory:
			{
				break;
			}

			case CSELIB::FileTypeEnum::Bucket:
			case CSELIB::FileTypeEnum::DirectoryObject:
			case CSELIB::FileTypeEnum::FileObject:
			{
				APP_ASSERT(mOptObjKey != std::nullopt);
				break;
			}

			default:
			{
				APP_ASSERT(0);
				break;
			}
		}
	}

	~FileContext();

	virtual HANDLE getHandle() = 0;
	virtual HANDLE getHandleWrite()
	{
		return INVALID_HANDLE_VALUE;
	}

	virtual void closeHandle() { }

	virtual std::wstring str() const noexcept;
};

class RefFileContext final : public FileContext
{
	HANDLE mHandle;

public:
	RefFileContext(
		const std::filesystem::path& argWinPath,
		CSELIB::FileTypeEnum argFileType,
		FSP_FSCTL_FILE_INFO* argFileInfo,
		const std::optional<CSELIB::ObjectKey>& argOptObjKey,
		HANDLE argHandle) noexcept
		:
		FileContext(argWinPath, argFileType, argFileInfo, argOptObjKey),
		mHandle(argHandle)
	{
	}

	HANDLE getHandle() override
	{
		return mHandle;
	}
};

class OpenFileContext final : public FileContext
{
	CSELIB::FileHandle mFile;

public:
	OpenFileContext(
		const std::filesystem::path& argWinPath,
		CSELIB::FileTypeEnum argFileType,
		FSP_FSCTL_FILE_INFO* argFileInfo,
		const std::optional<CSELIB::ObjectKey>& argOptObjKey,
		CSELIB::FileHandle&& argFile) noexcept
		:
		FileContext(argWinPath, argFileType, argFileInfo, argOptObjKey),
		mFile(std::move(argFile))
	{
	}

	HANDLE getHandle() override
	{
		return mFile.handle();
	}

	HANDLE getHandleWrite() override
	{
		return mFile.handle();
	}

	void closeHandle() override
	{
		mFile.close();
	}
};

}	// namespace CSELIB

// EOF