#include "GcpGsClient.hpp"

using namespace CSELIB;
using namespace CSEDVC;
namespace gcs = google::cloud::storage;

bool IsSuccess(const google::cloud::Status& status)
{
	NEW_LOG_BLOCK();

	const auto ok = status.ok();

	if (ok)
	{
		traceW(L"success");
	}
	else
	{
		const auto statusCode = status.code();

		const auto code{ static_cast<int>(statusCode) };
		const auto mesg{ status.message() };

		const auto& error_info = status.error_info();

		const auto reason{ error_info.reason() };
		const auto domain{ error_info.domain() };

		std::stringstream ss;

		for (const auto& kv: error_info.metadata())
		{
			ss << kv.first << "=" << kv.second << ",";
		}

		bool warn = false;

		switch (statusCode)
		{
			case google::cloud::StatusCode::kNotFound:
			{
				warn = true;
				break;
			}
		}

		if (warn)
		{
			traceA("warn: code=%d message=%s reason=%s domain=%s metadata=%s", code, mesg.c_str(), reason.c_str(), domain.c_str(), ss.str().c_str());
		}
		else
		{
			errorA("error: code=%d message=%s reason=%s domain=%s metadata=%s", code, mesg.c_str(), reason.c_str(), domain.c_str(), ss.str().c_str());
		}
	}

	return ok;
}

namespace CSEGGS {

bool GcpGsClient::ListBuckets(CALLER_ARG DirEntryListType* pDirEntryList)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(pDirEntryList);

	DirEntryListType dirEntryList;

	for (auto&& bucket_metadata: mGsClient->ListBuckets())
	{
		if (!IsSuccess(bucket_metadata))
		{
			errorW(L"fault: ListBuckets");

			return false;
		}

		const auto bucketName{ MB2WC(bucket_metadata->name()) };

		if (!mRuntimeEnv->matchesBucketFilter(bucketName))
		{
			// バケット名によるフィルタリング

			//traceW(L"%s: is not in filters, skip", bucketName.c_str());
			continue;
		}

		// バケットの作成日時を取得

		traceW(L"bucketName=%s***, timeCreated=%s", SafeSubStringW(bucketName, 0, 3).c_str(), TimePointToLocalTimeStringW(bucket_metadata->time_created()).c_str());

		const auto timeCreated = TimePointToWinFileTime100ns(bucket_metadata->time_created());

		auto dirEntry{ DirectoryEntry::makeBucketEntry(bucketName, timeCreated) };
		APP_ASSERT(dirEntry);

		dirEntryList.emplace_back(std::move(dirEntry));

		// 最大バケット表示数の確認

		if (mRuntimeEnv->MaxDisplayBuckets > 0)
		{
			if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayBuckets)
			{
				traceW(L"The maximum display limit has been reached.");
				break;
			}
		}
	}

	*pDirEntryList = std::move(dirEntryList);

	return true;
}

bool GcpGsClient::GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pBuketRegion)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(pBuketRegion);

	const auto& bucket_metadata = mGsClient->GetBucketMetadata(WC2MB(argBucket));

	if (!IsSuccess(bucket_metadata))
	{
		errorW(L"fault: GetBucketMetadata argBucket=%s", argBucket.c_str());

		return false;
	}

	// 使わないけれど、AWS とのインターフェースを合わせる意味で取得

	*pBuketRegion = MB2WC(bucket_metadata->location());

	return true;
}

bool GcpGsClient::HeadObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(pDirEntry);

	const auto& object_metadata = mGsClient->GetObjectMetadata(argObjKey.bucketA(), argObjKey.keyA());

	if (!IsSuccess(object_metadata))
	{
		traceW(L"fault: GetObjectMetadata argObjKey=%s", argObjKey.c_str());

		return false;
	}

	std::wstring filename;
	if (!SplitObjectKey(argObjKey.key(), nullptr, &filename))
	{
		errorW(L"fault: SplitObjectKey argObjKey=%s", argObjKey.c_str());
		return false;
	}

	const auto& result{ *object_metadata };

	traceW(L"argObjKey=%s, timeCreated=%s", argObjKey.c_str(), TimePointToLocalTimeStringW(result.time_created()).c_str());
	const auto timeCreated = TimePointToWinFileTime100ns(result.time_created());

	auto dirEntry = argObjKey.meansDir()
		? DirectoryEntry::makeDirectoryEntry(filename, timeCreated)
		: DirectoryEntry::makeFileEntry(filename, result.size(), timeCreated);
	APP_ASSERT(dirEntry);

	// メタ・データを FILETIME に反映

	const auto& metadata{ result.metadata() };
	setFileInfoFromMetadata(metadata, timeCreated, result.etag(), &dirEntry);

	traceW(L"dirEntry=%s", dirEntry->str().c_str());

	*pDirEntry = std::move(dirEntry);

	return true;
}

bool GcpGsClient::ListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(pDirEntryList);
	APP_ASSERT(argObjKey.meansDir());

	traceW(L"argObjKey=%s", argObjKey.c_str());

	const auto argKeyLen = argObjKey.key().length();

	auto items = argObjKey.isObject()
		? mGsClient->ListObjectsAndPrefixes(argObjKey.bucketA(), gcs::Delimiter("/"), gcs::Prefix(argObjKey.keyA()))
		: mGsClient->ListObjectsAndPrefixes(argObjKey.bucketA(), gcs::Delimiter("/"));

	// 取得したリストの要素からプレフィックスのリストと、オブジェクトのリストを生成する
	// このときプレフィックス用のタイムスタンプも採取する

	FILETIME_100NS_T commonPrefixTime = UINT64_MAX;

	std::set<std::wstring> prefixes;
	std::list<gcs::ObjectMetadata> objects;

	for (auto&& item: items)
	{
		if (!IsSuccess(item))
		{
			errorW(L"fault: ListObjectsAndPrefixes argObjKey=%s", argObjKey.c_str());
			return false;
		}

		auto&& result = *std::move(item);

		if (absl::holds_alternative<std::string>(result))
		{
			// ディレクトリ名の収集 (CommonPrefix)

			auto&& prefix = absl::get<std::string>(result);

			const auto keyFull{ MB2WC(prefix) };
			if (keyFull == argObjKey.key())
			{
				// 引数のディレクトリ名と同じ(= "." と同義)は無視
				// --> ここは通過しないが、念のため

				continue;
			}

			// Prefix 部分を取り除く
			// 
			// "dir/"           --> ""              ... 上記で除かれている
			// "dir/subdir/"    --> "subdir/"       ... 以降はこちらが対象

			const auto key{ SafeSubStringW(keyFull, argKeyLen) };

			// CommonPrefixes(=ディレクトリ) なので、"/" 終端されている

			APP_ASSERT(!key.empty());
			APP_ASSERT(key != L"/");
			APP_ASSERT(key.back() == L'/');

			const auto keyWinPath{ argObjKey.append(key).toWinPath() };
			if (mRuntimeEnv->shouldIgnoreWinPath(keyWinPath))
			{
				// 無視するファイル名はスキップ

				traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());
				continue;
			}

			prefixes.insert(SafeSubStringW(key, 0, key.length() - 1));
		}
		else if (absl::holds_alternative<gcs::ObjectMetadata>(result))
		{
			// ファイル名の収集 ("dir/" のような空オブジェクトも含む)

			auto&& object = absl::get<gcs::ObjectMetadata>(result);

			// ディレクトリ・エントリのため最初に一番古いタイムスタンプを収集
			// * CommonPrefix にはタイムスタンプがないため

			const auto timeCreated = TimePointToWinFileTime100ns(object.time_created());
			if (timeCreated < commonPrefixTime)
			{
				commonPrefixTime = timeCreated;
			}

			const auto keyFull{ MB2WC(object.name()) };
			if (keyFull == argObjKey.key())
			{
				// 引数のディレクトリ名と同じ(= "." と同義)は無視

				continue;
			}

			// Prefix 部分を取り除く
			// 
			// "dir/"           --> ""              ... 上記で除かれている
			// "dir/file1.txt"  --> "file1.txt"     ... 以降はこちらが対象

			const auto key{ SafeSubStringW(keyFull, argKeyLen) };

			APP_ASSERT(!key.empty());
			APP_ASSERT(key.back() != L'/');

			const auto keyWinPath{ argObjKey.append(key).toWinPath() };
			if (mRuntimeEnv->shouldIgnoreWinPath(keyWinPath))
			{
				// 無視するファイル名はスキップ

				traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());
				continue;
			}

			objects.push_back(object);
		}
	}

	if (commonPrefixTime == UINT64_MAX)
	{
		// タイムスタンプが採取できなければデフォルト値を採用

		commonPrefixTime = mRuntimeEnv->DefaultCommonPrefixTime;
	}

	// 収集した情報からディレクトリエントリを作成

	DirEntryListType dirEntryList;

	for (const auto& prefix: prefixes)
	{
		// CommonPrefix なので、ディレクトリ・オブジェクトとして登録

		auto dirEntry{ DirectoryEntry::makeDirectoryEntry(prefix + L'/', commonPrefixTime) };
		APP_ASSERT(dirEntry);

		dirEntryList.push_back(std::move(dirEntry));

		if (mRuntimeEnv->MaxDisplayObjects > 0)
		{
			if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayObjects)
			{
				traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

				goto exit;
			}
		}
	}

	for (const auto& object: objects)
	{
		const auto keyFull{ MB2WC(object.name()) };

		// Prefix 部分を取り除く
		// 
		// "dir/"           --> ""              ... 上記で除かれている
		// "dir/file1.txt"  --> "file1.txt"     ... 以降はこちらが対象

		const auto key{ SafeSubStringW(keyFull, argKeyLen) };

		APP_ASSERT(!key.empty());
		APP_ASSERT(key.back() != L'/');

		if (prefixes.find(key) != prefixes.cend())
		{
			// ディレクトリと同じ名前のファイルは無視

			traceW(L"exists same name of dir key=%s", key.c_str());

			continue;
		}

		const auto timeCreated = TimePointToWinFileTime100ns(object.time_created());

		auto dirEntry = DirectoryEntry::makeFileEntry(key, object.size(), timeCreated);
		APP_ASSERT(dirEntry);

		const auto& metadata{ object.metadata() };
		setFileInfoFromMetadata(metadata, timeCreated, object.etag(), &dirEntry);

		traceW(L"dirEntry=%s", dirEntry->str().c_str());

		dirEntryList.push_back(std::move(dirEntry));

		if (mRuntimeEnv->MaxDisplayObjects > 0)
		{
			if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayObjects)
			{
				// 結果リストが ini ファイルで指定した最大値に到達

				traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

				goto exit;
			}
		}
	}

exit:
	traceW(L"dirEntryList.size=%zu", dirEntryList.size());

	*pDirEntryList = std::move(dirEntryList);

	return true;
}

bool GcpGsClient::DeleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argObjKey.isObject());

	traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

	auto status = mGsClient->DeleteObject(argObjKey.bucketA(), argObjKey.keyA());
	if (!IsSuccess(status))
	{
		errorW(L"fault: DeleteObject argObjKey=%s", argObjKey.c_str());
		return false;
	}

	return true;
}

bool GcpGsClient::PutObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argObjKey.isObject());

	traceW(L"argObjKey=%s argFileInfo=%s argInputPath=%s", argObjKey.c_str(), FileInfoToStringW(argFileInfo).c_str(), argInputPath);

	// メタデータを設定

	gcs::ObjectMetadata inMetadata;
	auto& mutable_metadata = inMetadata.mutable_metadata();
	setMetadataFromFileInfo(CONT_CALLER argFileInfo, &mutable_metadata);

	// Content-Type を取得

	const auto contentType{ getContentType(CONT_CALLER argInputPath, argObjKey.key()) };

	// ストリームを生成

	auto stream = mGsClient->WriteObject(argObjKey.bucketA(), argObjKey.keyA(),
		gcs::WithObjectMetadata(inMetadata), gcs::ContentType(WC2MB(contentType)));

	if (FA_IS_DIR(argFileInfo.FileAttributes))
	{
		// ディレクトリの場合は空のコンテンツ

		APP_ASSERT(!argInputPath);
	}
	else
	{
		APP_ASSERT(argInputPath);

		// ストリームに出力

		const auto nWrite = writeStreamFromFile(CONT_CALLER &stream, argInputPath, 0, argFileInfo.FileSize);

		if (nWrite != static_cast<FILEIO_LENGTH_T>(argFileInfo.FileSize))
		{
			errorW(L"fault: writeStreamFromFile argInputPath=%s", argInputPath);
			return false;
		}
	}

	stream.Close();

	// アップロード結果を取得

	const auto& outMetadata = stream.metadata();
	if (!outMetadata)
	{
		errorW(L"fault: !outMetadata");
		return false;
	}

	traceA("success: metadata.name=%s metadata.size=%llu", outMetadata->name().c_str(), outMetadata->size());

	return true;
}

bool GcpGsClient::CopyObject(CALLER_ARG const ObjectKey& argSrcObjKey, const ObjectKey& argDstObjKey)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argSrcObjKey.isObject());
	APP_ASSERT(argDstObjKey.isObject());
	APP_ASSERT(argSrcObjKey.toFileType() == argDstObjKey.toFileType());

	const auto new_copy_meta = mGsClient->CopyObject(
		argSrcObjKey.bucketA(), argSrcObjKey.keyA(),
		argDstObjKey.bucketA(), argDstObjKey.keyA());

	if (!IsSuccess(new_copy_meta))
	{
		errorW(L"fault: CopyObject argSrcObjKey=%s argDstObjKey=%s", argSrcObjKey.c_str(), argDstObjKey.c_str());
		return false;
	}

	traceW(L"success: CopyObject argSrcObjKey=%s argDstObjKey=%s", argSrcObjKey.c_str(), argDstObjKey.c_str());

	return true;
}

FILEIO_LENGTH_T GcpGsClient::GetObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, FILEIO_LENGTH_T argOffset, FILEIO_LENGTH_T argLength)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argOffset >= 0LL);
	APP_ASSERT(argLength > 0);

	auto stream = mGsClient->ReadObject(argObjKey.bucketA(), argObjKey.keyA(), gcs::ReadRange(argOffset, argOffset + argLength));
	if (!stream)
	{
		errorW(L"fault: ReadObject argObjKey=%s argOffset=%lld argLength=%lld", argObjKey.c_str(), argOffset, argLength);
		return -1LL;
	}

	// stream の内容をファイルに出力する

	return writeFileFromStream(CONT_CALLER argOutputPath, argOffset, &stream, argLength);
}

}	// namespace CSEGGS

// EOF