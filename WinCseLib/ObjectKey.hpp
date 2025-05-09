#pragma once

namespace CSELIB
{

// 文字列をバケット名とキーに分割

class ObjectKey final
{
private:
	std::wstring	mBucket;
	std::wstring	mKey;
	std::wstring	mObjectPath;

	bool			mHasKey = false;
	bool			mMeansDir = false;

	ObjectKey() = delete;		// hidden

	WINCSELIB_API explicit ObjectKey(const std::wstring& argBucket, const std::wstring& argKey);

public:
	// unorderd_map のキーになるために必要

	bool operator==(const ObjectKey& other) const
	{
		return mBucket == other.mBucket && mKey == other.mKey;
	}

	// map のキーになるために必要

	bool operator<(const ObjectKey& other) const
	{
		if (mBucket < other.mBucket) {			// bucket
			return true;
		}
		else if (mBucket > other.mBucket) {
			return false;
		}
		else if (mKey < other.mKey) {			// key
			return true;
		}
		else if (mKey > other.mKey) {
			return false;
		}

		return false;
	}

	bool operator>(const ObjectKey& other) const
	{
		if (this->operator<(other)) {
			return false;
		}
		else if (this->operator==(other)) {
			return false;
		}

		return true;
	}

	const std::wstring& bucket() const { return mBucket; }
	const std::wstring& key() const { return mKey; }
	const std::wstring& str() const { return mObjectPath; }
	PCWSTR c_str() const { return mObjectPath.c_str(); }
	bool isBucket() const { return !mHasKey; }
	bool isObject() const { return mHasKey; }
	bool meansDir() const { return mMeansDir; }
	bool meansFile() const { return !mMeansDir; }

	ObjectKey append(const std::wstring& arg) const
	{
		return ObjectKey{ mBucket, mKey + arg };
	}

	WINCSELIB_API bool meansHidden() const;
	WINCSELIB_API FileTypeEnum toFileType() const;
	WINCSELIB_API std::string bucketA() const;
	WINCSELIB_API std::string keyA() const;
	WINCSELIB_API std::string strA() const;
	WINCSELIB_API ObjectKey toFile() const;
	WINCSELIB_API ObjectKey toDir() const;
	WINCSELIB_API std::filesystem::path toWinPath() const;
	WINCSELIB_API std::optional<ObjectKey> toParentDir() const;

private:
	template <wchar_t setV>
	WINCSELIB_API static std::optional<ObjectKey> fromXPath(const std::wstring& argPath);

public:
	static std::optional<ObjectKey> fromObjectPath(const std::wstring& argBucket, const std::wstring& argKey)
	{
		return fromObjectPath(argBucket + L'/' + argKey);
	}

	WINCSELIB_API static std::optional<ObjectKey> fromObjectPath(const std::wstring& argPath);
	WINCSELIB_API static std::optional<ObjectKey> fromWinPath(const std::filesystem::path& argWinPath);
};

}	// namespace CSELIB

namespace std
{
// カスタムハッシュ関数 ... unorderd_map のキーになるために必要

template <>
struct hash<CSELIB::ObjectKey>
{
	size_t operator()(const CSELIB::ObjectKey& that) const
	{
		return hash<wstring>()(that.bucket()) ^ (hash<wstring>()(that.key()) << 1);
	}
};

}	// namespace std

// EOF