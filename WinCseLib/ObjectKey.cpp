#include "WinCseLib.h"

namespace CSELIB {

ObjectKey::ObjectKey(const std::wstring& argBucket, const std::wstring& argKey)
	:
	mBucket(argBucket),
	mKey(argKey)
{
	APP_ASSERT(!mBucket.empty());

	// フラグのパターン
	// 
	// !mHasKey &&  mMeansDir	... "bucket"				--> バケット
	// !mHasKey && !mMeansDir	... ---
	//  mHasKey &&  mMeansDir	... "bucket/dir/"			--> オブジェクト
	//  mHasKey && !mMeansDir	... "bucket/dir/file.txt"	--> 〃

	mHasKey = !mKey.empty();

	if (mHasKey)
	{
		// キーがある場合は最後の文字が "/" かどうかによりファイルかディレクトリかを判断する

		APP_ASSERT(mKey != L"." && mKey != L".." && mKey != L"/");

		mMeansDir = mKey.back() == L'/';
		
	}
	else
	{
		// キーがない(=バケット) はディレクトリ

		mMeansDir = true;
	}

	mObjectPath = mBucket + L'/' + mKey;
}

bool ObjectKey::meansHidden() const
{
	if (mHasKey)
	{
		std::wstring filename;
		if (SplitObjectKey(mKey, nullptr, &filename))
		{
			return filename.at(0) == L'.';
		}
	}

	return false;
}

std::string ObjectKey::bucketA() const { return WC2MB(mBucket); }
std::string ObjectKey::keyA() const { return WC2MB(mKey); }
std::string ObjectKey::strA() const { return WC2MB(mObjectPath); }

std::optional<ObjectKey> ObjectKey::toParentDir() const
{
	// キーがあった場合は "/" で分割した親ディレクトリを返す

	if (mHasKey)
	{
		std::wstring parentDir;
		if (SplitObjectKey(mKey, &parentDir, nullptr))
		{
			return ObjectKey{ mBucket, parentDir };
		}
	}

	return std::nullopt;
}

FileTypeEnum ObjectKey::toFileType() const
{
	if (isBucket())
	{
		return FileTypeEnum::Bucket;
	}
	else if (meansDir())
	{
		return FileTypeEnum::Directory;
	}
	else if (meansFile())
	{
		return FileTypeEnum::File;
	}

	throw FatalError(__FUNCTION__);
}

ObjectKey ObjectKey::toFile() const
{
	if (mHasKey && mMeansDir)
	{
		APP_ASSERT(mKey.back() == L'/');

		return ObjectKey{ mBucket, mKey.substr(0, mKey.size() - 1) };
	}

	return *this;
}

ObjectKey ObjectKey::toDir() const
{
	if (!mMeansDir)
	{
		return ObjectKey{ mBucket, mKey + L'/' };
	}

	return *this;
}

std::filesystem::path ObjectKey::toWinPath() const
{
	auto ret{ std::wstring{ L'\\' } + mBucket };

	if (mHasKey)
	{
		auto key{ mKey };

		if (key.back() == L'/')
		{
			// WinFsp からコールバックされる関数 (ex. Open()...) の引数の
			// ファイル名は、ディレクトリであった場合でも "\" 終端していない
			// その動作に合わせ、"/" 終端の場合は予め取り除く

			key.pop_back();
		}

		// "/" を "\" に置き換え

		std::replace(key.begin(), key.end(), L'/', L'\\');

		// バケットとキーを結合

		ret += L'\\' + key;
	}

	return ret;
}

template <wchar_t setV>
std::optional<ObjectKey> ObjectKey::fromXPath(const std::wstring& argPath)
{
	// パス文字列をバケット名とキーに分割

	std::wstring bucket;
	std::wstring key;

	std::vector<std::wstring> tokens;

	std::wistringstream input{ argPath };
	std::wstring token;

	while (std::getline(input, token, setV))
	{
		token = TrimW(token);
		if (token.empty())
		{
			continue;
		}

		tokens.emplace_back(std::move(token));
	}

	switch (tokens.size())
	{
		case 0:
		{
			return std::nullopt;
		}

		case 1:
		{
			bucket = std::move(tokens[0]);

			break;
		}

		default:
		{
			bucket = std::move(tokens[0]);

			std::wostringstream ss;
			for (int i = 1; i < tokens.size(); ++i)
			{
				if (i != 1)
				{
					ss << L'/';
				}
				ss << tokens[i];
			}
			key = ss.str();

			APP_ASSERT(!key.empty());

			if (argPath.back() == setV)
			{
				// "/" で分割しているので、入力の一番最後にある "/" も消えてしまう
				// この場合を考慮して、キーの最後に "/" を追加

				key += L'/';
			}

			break;
		}
	}

	APP_ASSERT(!bucket.empty());

	return ObjectKey{ bucket, key };
}

std::optional<ObjectKey> ObjectKey::fromObjectPath(const std::wstring& argPath)
{
	if (argPath.empty())
	{
		return std::nullopt;
	}

	if (argPath.at(0) == L'/')
	{
		return std::nullopt;
	}

	return fromXPath<L'/'>(argPath);
}

std::optional<ObjectKey> ObjectKey::fromWinPath(const std::filesystem::path& argWinPath)
{
	if (argWinPath.empty())
	{
		return std::nullopt;
	}

	if (argWinPath.wstring().at(0) != L'\\')
	{
		return std::nullopt;
	}

	return fromXPath<L'\\'>(argWinPath);
}

}	// namespace CSELIB

// EOF