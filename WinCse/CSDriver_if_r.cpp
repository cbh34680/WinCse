#include "WinCseLib.h"
#include "CSDriver.hpp"

using namespace WCSE;


NTSTATUS CSDriver::DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoGetFileInfo);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);

	if (wcscmp(FileContext->FileName, L"\\") != 0)
	{
		traceW(L"FileName=%s", FileContext->FileName);
	}

	*FileInfo = FileContext->FileInfo;

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
	PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize)
{
	StatsIncr(DoGetSecurity);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	if (wcscmp(FileContext->FileName, L"\\") != 0)
	{
		traceW(L"FileName=%s", FileContext->FileName);
	}

	const bool isDir = FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes);
	const HANDLE Handle = isDir ? mRefDir.handle() : mRefFile.handle();

	return HandleToSecurityInfo(Handle, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS CSDriver::DoRead(PTFS_FILE_CONTEXT* FileContext,
	PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
	StatsIncr(DoRead);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	traceW(L"FileName=%s, FileAttributes=%u, FileSize=%llu, Offset=%llu, Length=%lu",
		FileContext->FileName, FileContext->FileInfo.FileAttributes, FileContext->FileInfo.FileSize, Offset, Length);

	//APP_ASSERT(Offset <= FileContext->FileInfo.FileSize);

	// �t�@�C�����쐬����K�v������̂ŁA�T�C�Y==0 �ł� readObject �͌Ăяo��

	return mCSDevice->readObject(START_CALLER (CSDeviceContext*)FileContext->UParam,
		Buffer, Offset, Length, PBytesTransferred);
}

NTSTATUS CSDriver::DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern,
	PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
	StatsIncr(DoReadDirectory);
    NEW_LOG_BLOCK();

    APP_ASSERT(FileContext && Buffer && PBytesTransferred);
	APP_ASSERT(FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));

    std::unique_ptr<std::wregex> re;

    if (Pattern)
    {
        // �����̃p�^�[���𐳋K�\���ɕϊ�

        re = std::make_unique<std::wregex>(WildcardToRegexW(Pattern));
    }

    // �f�B���N�g���̒��̈ꗗ�擾

    PCWSTR FileName = FileContext->FileName;
    DirInfoListType dirInfoList;

    if (wcscmp(FileName, L"\\") == 0)
    {
        // "\" �ւ̃A�N�Z�X�̓o�P�b�g�ꗗ���

        if (!mCSDevice->listBuckets(START_CALLER &dirInfoList))
        {
            traceW(L"not fouund/1");

            return STATUS_OBJECT_NAME_INVALID;
        }

        if (dirInfoList.empty())
        {
            traceW(L"empty buckets");
            return STATUS_SUCCESS;
        }
    }
    else
    {
        // "\bucket" �܂��� "\bucket\key"

        const auto objKey{ ObjectKey::fromWinPath(FileName) };
        if (objKey.invalid())
        {
            traceW(L"invalid FileName=%s", FileName);

            return STATUS_OBJECT_NAME_INVALID;
        }

        const auto listObjKey{ objKey.toDir() };

        if (listObjKey.isObject())
        {
            traceW(L"listObjKey=%s", listObjKey.c_str());
        }

        // �L�[����̏ꍇ)		bucket & ""     �Ō���
        // �L�[����łȂ��ꍇ)	bucket & "key/" �Ō���

        if (!mCSDevice->listDisplayObjects(START_CALLER listObjKey, &dirInfoList))
        {
            traceW(L"not found/2");

            return STATUS_OBJECT_NAME_INVALID;
        }

        // ���Ȃ��Ƃ� "." �͂���̂ŋ�ł͂Ȃ��͂�

        //traceW(L"object count: %zu", dirInfoList.size());
    }

    APP_ASSERT(!dirInfoList.empty());

    // �擾�������̂� WinFsp �ɓ]������

    NTSTATUS DirBufferResult = STATUS_SUCCESS;

    if (FspFileSystemAcquireDirectoryBuffer(&FileContext->DirBuffer, 0 == Marker, &DirBufferResult))
    {
        for (const auto& dirInfo: dirInfoList)
        {
            if (shouldIgnoreFileName(dirInfo->FileNameBuf))
            {
                continue;
            }

            if (re)
            {
                if (!std::regex_match(dirInfo->FileNameBuf, *re))
                {
                    continue;
                }
            }

            if (!FspFileSystemFillDirectoryBuffer(&FileContext->DirBuffer, dirInfo->data(), &DirBufferResult))
            {
                break;
            }
        }

        FspFileSystemReleaseDirectoryBuffer(&FileContext->DirBuffer);
    }

    if (!NT_SUCCESS(DirBufferResult))
    {
        return DirBufferResult;
    }

    FspFileSystemReadDirectoryBuffer(&FileContext->DirBuffer,
        Marker, Buffer, BufferLength, PBytesTransferred);

    return STATUS_SUCCESS;
}

// EOF
