#include "CSDriver.hpp"

using namespace CSELIB;
using namespace CSEDRV;


ICSDriver* NewCSDriver(PCWSTR argCSDeviceType, PCWSTR argIniSection, NamedWorker argWorkers[], ICSDevice* argCSDevice, WINCSE_DRIVER_STATS* argStats)
{
    std::map<std::wstring, IWorker*> workers;

    if (NamedWorkersToMap(argWorkers, &workers) <= 0)
    {
        return nullptr;
    }

    for (const auto key: { L"delayed", L"timer", })
    {
        if (workers.find(key) == workers.cend())
        {
            return nullptr;
        }
    }

	return new CSDriver{ argCSDeviceType, argIniSection, workers, argCSDevice, argStats };
}

DirEntryType CSDriver::getDirEntryByWinPath(CALLER_ARG const std::filesystem::path& argWinPath) const
{
	if (argWinPath == L"\\")
	{
		// "\" へのアクセスは参照用ディレクトリの情報を提供

		FSP_FSCTL_FILE_INFO fileInfo;

		const auto ntstatus = GetFileInfoInternal(mRuntimeEnv->DirSecurityRef.handle(), &fileInfo);
		if (!NT_SUCCESS(ntstatus))
		{
			return nullptr;
		}

		return DirectoryEntry::makeRootEntry(fileInfo.LastWriteTime);
	}
	else
	{
		const auto optObjKey{ ObjectKey::fromWinPath(argWinPath) };
		if (!optObjKey)
		{
			return nullptr;
		}

		const auto& objKey{ *optObjKey };

		if (objKey.isBucket())
		{
			// "\bucket" のパターン

			DirEntryType dirEntry;

			if (mDevice->headBucket(CONT_CALLER objKey.bucket(), &dirEntry))
			{
				return dirEntry;
			}
		}
		else if (objKey.isObject())
		{
			// "\bucket\***" のパターン

			// 同じ名前のファイルとディレクトリが存在したときに、ディレクトリを優先するため
			// 引数の名前をディレクトリに変換しストレージを調べ、存在しないときはファイルとして調べる

			DirEntryType dirEntry;

			if (mDevice->headObjectAsDirectory(CONT_CALLER objKey, &dirEntry))
			{
				// "\bucket\dir" のパターン
				// 
				// ディレクトリを採用

				return dirEntry;
			}

			if (mDevice->headObject(CONT_CALLER objKey, &dirEntry))
			{
				// "\bucket\dir\file.txt" のパターン
				// 
				// ファイルを採用

				return dirEntry;
			}
		}
		else
		{
			APP_ASSERT(0);
		}
	}

	NEW_LOG_BLOCK();
	traceW(L"not found: argWinPath=%s", argWinPath.c_str());

	return nullptr;
}

NTSTATUS CSDriver::canCreateObject(CALLER_ARG const std::filesystem::path& argWinPath, bool argIsDir, std::optional<ObjectKey>* pOptObjKey)
{
	NEW_LOG_BLOCK();

	// 変更後の名前が無視対象かどうか確認

	const auto dirEntry{ mOpenDirEntry.get(argWinPath) };
	if (dirEntry)
	{
		traceW(L"already exist: argWinPath=%s", argWinPath.c_str());

		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;
	}

	const auto optObjKey{ ObjectKey::fromWinPath(argWinPath) };
	if (!optObjKey)
	{
		errorW(L"fault: fromWinPath argWinPath=%s", argWinPath.c_str());
		return STATUS_OBJECT_NAME_INVALID;
	}

	const auto& refObjKey{ *optObjKey };

	if (refObjKey.isBucket())
	{
		// バケットに対する操作

		if (!mDevice->headBucket(CONT_CALLER refObjKey.bucket(), nullptr))
		{
			// "md \bucket\not\exist\yet\dir" を実行するとバケットに対して create が
			// 呼び出されるが、これに対し STATUS_OBJECT_NAME_COLLISION 以外を返却すると
			// md コマンドが失敗する

			traceW(L"already exists refObjKey=%s", refObjKey.c_str());

			//return STATUS_ACCESS_DENIED;
			//return FspNtStatusFromWin32(ERROR_ACCESS_DENIED);
			//return FspNtStatusFromWin32(ERROR_WRITE_PROTECT);
			//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);

			return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
		}
		else
		{
			traceW(L"fault: headBucket refObjKey=%s", refObjKey.c_str());

			return STATUS_ACCESS_DENIED;
		}
	}

	// 変更先の名前が存在するか確認

	auto objKey{ argIsDir ? refObjKey.toDir() : refObjKey };
	APP_ASSERT(objKey.isObject());

	traceW(L"objKey=%s", objKey.c_str());

	if (mDevice->headObject(START_CALLER objKey, nullptr))
	{
		traceW(L"already exists: objKey=%s", objKey.c_str());

		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	// ファイル名、ディレクトリ名を反転させ名前が存在するか確認

	const auto revObjKey{ argIsDir ? objKey.toFile() : objKey.toDir() };
	APP_ASSERT(revObjKey.isObject());

	traceW(L"revObjKey=%s", revObjKey.c_str());

	if (mDevice->headObject(START_CALLER revObjKey, nullptr))
	{
		traceW(L"already exists: revObjKey=%s", revObjKey.c_str());

		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	*pOptObjKey= std::move(objKey);

	return STATUS_SUCCESS;
}

// EOF