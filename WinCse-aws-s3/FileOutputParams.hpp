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
		// GetObject.SetRange �̎w��� "�J�n�I�t�Z�b�g - �I���I�t�Z�b�g" �Ȃ̂�
		// �擪�� 1 byte ���w�肷��ꍇ�� "bytes=0-0" �ƂȂ�
		//
		// mLength �� 0 �̏ꍇ�ɂ���𓖂Ă͂߂�� "mOffset + 0 - 1" �ƂȂ�
		// ����͕s���R�ł��邪 UINT64 �̂��߁A�G���[�l�Ƃ��ĕ��l���߂��Ȃ�
		//
		// ���̂��߁AmLength �̒l�ɂ�蓮���ς��Ă���

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