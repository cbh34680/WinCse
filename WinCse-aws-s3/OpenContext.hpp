#pragma once

#include "WinCseLib.h"

//
// open() ���Ă΂ꂽ�Ƃ��� UParam �Ƃ��� PTFS_FILE_CONTEXT �ɕۑ�����������
// close() �ō폜�����
//
struct OpenContext : public WCSE::CSDeviceContext
{
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;

	explicit OpenContext(
		const std::wstring& argCacheDataDir,
		const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const UINT32 argCreateOptions,
		const UINT32 argGrantedAccess) noexcept
		:
		CSDeviceContext(argCacheDataDir, argObjKey, argFileInfo),
		mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess)
	{
	}

	NTSTATUS openFileHandle(CALLER_ARG DWORD argDesiredAccess, DWORD argCreationDisposition) noexcept;
};

// EOF