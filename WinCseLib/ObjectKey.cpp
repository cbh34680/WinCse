#include "WinCseLib.h"

using namespace CSELIB;


ObjectKey::ObjectKey(const std::wstring& argBucket, const std::wstring& argKey) noexcept
	:
	mBucket(argBucket),
	mKey(argKey)
{
	APP_ASSERT(!argBucket.empty());

	mHasKey = mMeansDir = mMeansFile = mDotEntries = false;

	mHasBucket = !mBucket.empty();
	if (mHasBucket)
	{
		//
		// キーがある  (mHasKey)
		//		キーが ".", ".." のどちらか					== ディレクトリ (ドット・エントリ)
		//		最後が "/"			(ex. bucket/key/)		== ディレクトリ
		//      最後が "/" ではない	(ex. bucket/key.txt)
		//			最後が "/.", "/.." のどちらか			== ディレクトリ (ドット・エントリ)
		// 			それ以外								== ファイル
		// 
		// キーがない (!mHasKey)
		//							(ex. bucket)			== ディレクトリ
		//

		mHasKey = !mKey.empty();
		if (mHasKey)
		{
			if (mKey == L"." || mKey == L"..")
			{
				// ".", ".." はドット・エントリのディレクトリ

				mMeansDir = mDotEntries = true;
			}
			else
			{
				if (mKey.back() == L'/')
				{
					// 最後が "/" で終わっていたら通常のディレクトリ

					mMeansDir = true;
				}
				else
				{
					const auto keyLen = mKey.length();

					if (keyLen >= 2 && mKey.substr(keyLen - 2) == L"/.")
					{
						// 最後が "/." で終わっていたらドット・エントリのディレクトリ

						mMeansDir = mDotEntries = true;
					}
					else if (keyLen >= 3 && mKey.substr(keyLen - 3) == L"/..")
					{
						// 最後が "/.." で終わっていたらドット・エントリのディレクトリ

						mMeansDir = mDotEntries = true;
					}
				}
			}

			mMeansFile = !mMeansDir;
		}
		else
		{
			// キーがない(=バケット) はディレクトリ

			mMeansDir = true;
		}

		APP_ASSERT(!(mMeansDir && mMeansFile));
	}

	mBucketKey = mBucket + L'/' + mKey;
}

bool ObjectKey::meansHidden() const noexcept
{
	if (mHasKey)
	{
		std::wstring filename;
		if (SplitObjectKey(mKey, nullptr, &filename))
		{
			return MeansHiddenFile(filename);
		}
	}

	return false;
}

std::string ObjectKey::bucketA() const { return WC2MB(mBucket); }
std::string ObjectKey::keyA() const { return WC2MB(mKey); }
std::string ObjectKey::strA() const { return WC2MB(mBucketKey); }

std::optional<ObjectKey> ObjectKey::toParentDir() const
{
	//
	// キーがあった場合は "/" で分割した親ディレクトリを返す
	//

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

FileTypeEnum ObjectKey::toFileType() const noexcept
{
	if (isBucket())
	{
		return FileTypeEnum::Bucket;
	}
	else if (meansDir())
	{
		return FileTypeEnum::DirectoryObject;
	}
	else if (meansFile())
	{
		return FileTypeEnum::FileObject;
	}

	APP_ASSERT(0);

	return FileTypeEnum::None;
}

ObjectKey ObjectKey::append(const std::wstring& arg) const noexcept
{
	if (this->meansDir())
	{
		// ".", ".." には結合させない

		APP_ASSERT(this->meansRegularDir());
	}

	return ObjectKey{ mBucket, mKey + arg };
}

ObjectKey ObjectKey::toFile() const noexcept
{
	//
	// キーの後ろから "/" を削除したものを返却する
	// キーがない場合は自分のコピーを返す			--> バケット名のみの場合
	// そもそもファイルの場合もコピーを返す
	//

	if (mHasKey)
	{
		if (this->meansRegularDir())
		{
			APP_ASSERT(mKey.back() == L'/');

			return ObjectKey{ mBucket, mKey.substr(0, mKey.size() - 1) };
		}
	}

	return *this;
}

ObjectKey ObjectKey::toDir() const noexcept
{
	//
	// キーの後ろに "/" を付与したものを返却する
	// キーがない場合は自分のコピーを返す			--> バケット名のみの場合
	// そもそもディレクトリの場合もコピーを返す
	//

	if (mHasKey)
	{
		if (this->meansFile())
		{
			return ObjectKey{ mBucket, mKey + L'/' };
		}
	}

	return *this;
}

std::filesystem::path ObjectKey::toWinPath() const noexcept
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
std::optional<ObjectKey> ObjectKey::fromXPath(const std::wstring& argPath) noexcept
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

	return ObjectKey{ bucket, key };
}

std::optional<ObjectKey> ObjectKey::fromPath(const std::wstring& argPath) noexcept
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

std::optional<ObjectKey> ObjectKey::fromWinPath(const std::filesystem::path& argWinPath) noexcept
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

// EOF