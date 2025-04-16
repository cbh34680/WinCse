#pragma once

#include "WinCseLib.h"

//
// open() ‚ªŒÄ‚Î‚ê‚½‚Æ‚«‚É UParam ‚Æ‚µ‚Ä PTFS_FILE_CONTEXT ‚É•Û‘¶‚·‚é“à•”î•ñ
// close() ‚Åíœ‚³‚ê‚é
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