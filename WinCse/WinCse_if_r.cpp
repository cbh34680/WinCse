#include "WinCseLib.h"
#include "WinCse.hpp"
#include <cinttypes>
#include <sstream>
#include <filesystem>
#include <mutex>

using namespace WinCseLib;

#undef traceA


struct ListObjectsTask : public ITask
{
	ICloudStorage* mStorage;
	const std::wstring bucket;
	const std::wstring key;

	ListObjectsTask(ICloudStorage* arg, const std::wstring& argBucket, const std::wstring& argKey) :
		mStorage(arg), bucket(argBucket), key(argKey) { }

	std::wstring synonymString()
	{
		std::wstringstream ss;
		ss << L"ListObjectsTask; ";
		ss << bucket;
		ss << "; ";
		ss << key;
		
		return ss.str();
	}

	void run(CALLER_ARG IWorker* worker, const int indent) override
	{
		GetLogger()->traceW_impl(indent, __FUNCTIONW__, __LINE__, __FUNCTIONW__, L"Request ListObjects");

		mStorage->listObjects(CONT_CALLER bucket, key, nullptr, 0, true);
	}
};


#if 0
// 限定的な状況でしか動かないので注意
static std::mutex gGuard;

#define THREAD_SAFE_4DEBUG() \
	std::lock_guard<std::mutex> lock_(gGuard); \
    traceW(L"!!! *** DANGER *** !!! THREAD_SAFE_4DEBUG() ENABLE")

#else
#define THREAD_SAFE_4DEBUG()

#endif


NTSTATUS WinCse::DoGetSecurityByName(
	const wchar_t* FileName, PUINT32 PFileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');

	traceW(L"FileName: %s", FileName);

	if (isIgnoreFileName(FileName))
	{
		// "desktop.ini" などは無視させる

		traceW(L"ignore pattern");
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	bool isDir = false;
	bool isFile = false;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" へのアクセスは参照用ディレクトリの情報を提供

		isDir = true;
		traceW(L"detect directory/1");
	}
	else
	{
		// ここを通過するときは FileName が "\bucket\key" のようになるはず

		const BucketKey bk{ FileName };
		if (!bk.OK)
		{
			traceW(L"illegal FileName: %s", FileName);
			return STATUS_INVALID_PARAMETER;
		}

		if (bk.HasKey)
		{
			// "\bucket\key" のパターン

			// "key/" で一件のみ取得して、存在したらディレクトリが存在すると判定し
			// その情報をディレクトリ属性として採用

			std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

			if (mStorage->listObjects(INIT_CALLER bk.bucket, bk.key + L'/', &dirInfoList, 1, false))
			{
				APP_ASSERT(!dirInfoList.empty());
				APP_ASSERT(dirInfoList.size() == 1);

				// ディレクトリを採用
				isDir = true;
				traceW(L"detect directory/2");

				// ディレクトリ内のオブジェクトを先読みし、キャッシュを作成しておく
				// 優先度は低く、無視できる
				mDelayedWorker->addTask(new ListObjectsTask{ mStorage, bk.bucket, bk.key + L'/' }, CanIgnore::YES, Priority::LOW);
			}

			if (!isDir)
			{
				// ファイル名の完全一致で検索

				if (mStorage->headObject(INIT_CALLER bk.bucket, bk.key, nullptr))
				{
					// ファイルを採用
					isFile = true;
					traceW(L"detect file");
				}
			}
		}
		else // !bk.HasKey
		{
			// "\bucket" のパターン

			if (mStorage->headBucket(INIT_CALLER bk.bucket))
			{
				// ディレクトリを採用
				isDir = true;
				traceW(L"detect directory/3");

				// ディレクトリ内のオブジェクトを先読みし、キャッシュを作成しておく
				// 優先度は低く、無視できる
				mDelayedWorker->addTask(new ListObjectsTask{ mStorage, bk.bucket, L"" }, CanIgnore::YES, Priority::LOW);
			}
		}
	}

	if (!isDir && !isFile)
	{
		traceW(L"not found");
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	const HANDLE handle = isFile ? mFileRefHandle : mDirRefHandle;

#ifdef _DEBUG
	std::wstring path;
	HandleToPath(handle, path);
	traceW(L"selected path is %s", path.c_str());

	std::wstring sdstr;
	PathToSDStr(path, sdstr);
	traceW(L"sdstr is %s", sdstr.c_str());
#endif

	return HandleToInfo(handle, PFileAttributes, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!isIgnoreFileName(FileName));
	APP_ASSERT(PFileContext);
	APP_ASSERT(FileInfo);

	traceW(L"FileName: %s", FileName);

	PTFS_FILE_CONTEXT* FileContext = nullptr;
	FSP_FSCTL_FILE_INFO fileInfo = {};
	NTSTATUS Result = STATUS_UNSUCCESSFUL;

	Result = FileNameToFileInfo(FileName, &fileInfo);
	if (!NT_SUCCESS(Result))
	{
		traceW(L"fault: FileNameToFileInfo");
		goto exit;
	}

	// 念のため検査
	APP_ASSERT(fileInfo.FileAttributes);
	APP_ASSERT(fileInfo.CreationTime);

	// WinFsp に保存されるファイル・コンテキストを生成
	// このメモリは WinFsp の Close() で削除されるため解放不要

	FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof *FileContext);
	if (!FileContext)
	{
		traceW(L"fault: allocate FileContext");
		Result = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->Open.FileName = _wcsdup(FileName);
	if (!FileContext->Open.FileName)
	{
		traceW(L"fault: allocate FileContext->OpenFileName");
		Result = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	if (wcscmp(FileName, L"\\") == 0)
	{
		traceW(L"root access");

		APP_ASSERT(fileInfo.FileSize == 0);
	}
	else
	{
		const BucketKey bk{ FileName };

		if (!bk.OK)
		{
			traceW(L"illegal FileName: %s", FileName);
			return STATUS_INVALID_PARAMETER;
		}

		if (fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// ディレクトリへのアクセス

			APP_ASSERT(fileInfo.FileSize == 0);
		}
		else
		{
			// ファイルへのアクセス

			APP_ASSERT(bk.HasKey);

			// マルチパート処理次第で最大ファイル・サイズの制限をなくす

			traceW(L"FileSize: %" PRIu64, fileInfo.FileSize);

			if (mMaxFileSize > 0)
			{
				if (fileInfo.FileSize > 1024ULL * 1024 * mMaxFileSize)
				{
					Result = STATUS_DEVICE_NOT_READY;
					traceW(L"%" PRIu64 ": When a file size exceeds the maximum size that can be opened.", fileInfo.FileSize);
					goto exit;
				}
			}

			// クラウド・ストレージのコンテキストを CSData に保存させる

			if (!mStorage->openFile(INIT_CALLER
				bk.bucket, bk.key, CreateOptions, GrantedAccess, fileInfo, &FileContext->Open.CSData))
			{
				traceW(L"fault: openFile");
				Result = STATUS_DEVICE_NOT_READY;
				goto exit;
			}
		}
	}

	FileContext->Open.FileInfo = fileInfo;

	// SUCSESS RETURN

	*PFileContext = FileContext;
	FileContext = nullptr;

	*FileInfo = fileInfo;

exit:
	if (FileContext)
	{
		free(FileContext->Open.FileName);
	}
	free(FileContext);

	traceW(L"return %ld", Result);

	return Result;
}

NTSTATUS WinCse::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);

	traceW(L"Open.FileName: %s", FileContext->Open.FileName);

	if (FileContext->Open.CSData)
	{
		// クラウド・ストレージに CSData を解放させる

		mStorage->closeFile(INIT_CALLER FileContext->Open.CSData);
	}

	free(FileContext->Open.FileName);

	// FileContext は呼び出し元で free している

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);
	APP_ASSERT(FileInfo);

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);
	PCWSTR FileName = FileContext->Open.FileName;

	FSP_FSCTL_FILE_INFO fileInfo = {};

	NTSTATUS Result = FileNameToFileInfo(FileName, &fileInfo);
	if (!NT_SUCCESS(Result))
	{
		traceW(L"fault: FileNameToFileInfo");
		goto exit;
	}

	*FileInfo = fileInfo;

	Result = STATUS_SUCCESS;

exit:

	return Result;
}

NTSTATUS WinCse::DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);
	traceW(L"OpenFileAttributes: %u", FileContext->Open.FileInfo.FileAttributes);

	const bool isFile = !(FileContext->Open.FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

	traceW(L"isFile: %s", isFile ? L"true" : L"false");

	const HANDLE handle = isFile ? mFileRefHandle : mDirRefHandle;

	return HandleToInfo(handle, nullptr, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(Path);
	APP_ASSERT(VolumeInfo);

	traceW(L"Path: %s", Path);
	traceW(L"FreeSize: %" PRIu64, VolumeInfo->FreeSize);
	traceW(L"TotalSize: %" PRIu64, VolumeInfo->TotalSize);

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoRead(PTFS_FILE_CONTEXT* FileContext,
	PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);
	APP_ASSERT(Buffer);
	APP_ASSERT(PBytesTransferred);
	APP_ASSERT(!(FileContext->Open.FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY));

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);
	traceW(L"OpenFileAttributes: %u", FileContext->Open.FileInfo.FileAttributes);

	bool ret = mStorage->readFile(INIT_CALLER FileContext->Open.CSData,
		Buffer, Offset, Length, PBytesTransferred);

	traceW(L"readFile return %s", ret ? L"true" : L"false");

	if (!ret)
	{
		return STATUS_DEVICE_DATA_ERROR;
	}

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern,
	PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);

	APP_ASSERT(FileContext->Open.FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

	std::wregex re;
	std::wregex* pRe = nullptr;

	if (Pattern)
	{
		const auto pattern = WildcardToRegexW(Pattern);
		re = std::wregex(pattern);
		pRe = &re;
	}

	// ディレクトリの中の一覧取得

	PCWSTR FileName = FileContext->Open.FileName;

	std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" へのアクセスはバケット一覧を提供

		if (!mStorage->listBuckets(INIT_CALLER &dirInfoList, {}))
		{
			traceW(L"not fouund/1");

			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

		APP_ASSERT(!dirInfoList.empty());
		traceW(L"bucket count: %zu", dirInfoList.size());
	}
	else
	{
		// "\bucket" または "\bucket\key"

		const BucketKey bk{ FileName };
		if (!bk.OK)
		{
			traceW(L"illegal FileName: %s", FileName);

			return STATUS_INVALID_PARAMETER;
		}

		// キーが空の場合)		bucket & ""     で検索
		// キーが空でない場合)	bucket & "key/" で検索

		const auto key = bk.HasKey ? bk.key + L'/' : bk.key;

		if (!mStorage->listObjects(INIT_CALLER bk.bucket, key, &dirInfoList, 0, true))
		{
			traceW(L"not found/2");

			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

		APP_ASSERT(!dirInfoList.empty());
		traceW(L"object count: %zu", dirInfoList.size());
	}

	if (!dirInfoList.empty())
	{
		// 取得したものを WinFsp に転送する

		NTSTATUS DirBufferResult = STATUS_SUCCESS;

		if (FspFileSystemAcquireDirectoryBuffer(&FileContext->DirBuffer, 0 == Marker, &DirBufferResult))
		{
			for (const auto& dirInfo: dirInfoList)
			{
				if (pRe)
				{
					if (!std::regex_match(dirInfo->FileNameBuf, *pRe))
					{
						continue;
					}
				}

				if (!FspFileSystemFillDirectoryBuffer(&FileContext->DirBuffer, dirInfo.get(), &DirBufferResult))
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
	}

	return STATUS_SUCCESS;
}

// EOF
