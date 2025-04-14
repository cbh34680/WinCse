#pragma once

#include "WinCseLib.h"

struct FileOutputParams
{
	const std::wstring mPath;
	const DWORD mCreationDisposition;
	const UINT64 mOffset;
	const ULONG mLength;

	explicit FileOutputParams(std::wstring argPath, DWORD argCreationDisposition,
		UINT64 argOffset, ULONG argLength) noexcept
		:
		mPath(argPath),
		mCreationDisposition(argCreationDisposition),
		mOffset(argOffset),
		mLength(argLength)
	{
	}

	explicit FileOutputParams(std::wstring argPath, DWORD argCreationDisposition) noexcept
		:
		mPath(argPath),
		mCreationDisposition(argCreationDisposition),
		mOffset(0ULL),
		mLength(0UL)
	{
	}

	UINT64 getOffsetEnd() const noexcept
	{
		// GetObject.SetRange の指定は "開始オフセット - 終了オフセット" なので
		// 先頭の 1 byte を指定する場合は "bytes=0-0" となる
		//
		// mLength が 0 の場合にこれを当てはめると "mOffset + 0 - 1" となり
		// これは不自然であるが UINT64 のため、エラー値として負値も戻せない
		//
		// このため、mLength の値により動作を変えている

		if (mLength > 0)
		{
			return mOffset + mLength - 1;
		}
		else
		{
			return mOffset;
		}
	}

	std::wstring str() const noexcept;
};

// EOF