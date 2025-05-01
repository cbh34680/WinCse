#pragma once

namespace CSELIB
{

// 文字列をバケット名とキーに分割

class ObjectKey final
{
private:
	std::wstring	mBucket;
	std::wstring	mKey;
	std::wstring	mBucketKey;
	bool			mHasBucket = false;
	bool			mHasKey = false;
	bool			mMeansDir = false;
	bool			mMeansFile = false;
	bool			mDotEntries = false;

	ObjectKey() = delete;		// hidden

	WINCSELIB_API explicit ObjectKey(const std::wstring& argBucket, const std::wstring& argKey) noexcept;

public:
	// unorderd_map のキーになるために必要

	bool operator==(const ObjectKey& other) const noexcept
	{
		return mBucket == other.mBucket && mKey == other.mKey;
	}

	// map のキーになるために必要

	bool operator<(const ObjectKey& other) const noexcept
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

	bool operator>(const ObjectKey& other) const noexcept
	{
		if (this->operator<(other)) {
			return false;
		}
		else if (this->operator==(other)) {
			return false;
		}

		return true;
	}

	const std::wstring& bucket() const noexcept { return mBucket; }
	const std::wstring& key() const noexcept { return mKey; }
	const std::wstring& str() const noexcept { return mBucketKey; }
	PCWSTR c_str() const noexcept { return mBucketKey.c_str(); }
	bool isBucket() const noexcept { return mHasBucket && !mHasKey; }
	bool isObject() const noexcept { return mHasKey; }
	bool meansDir() const noexcept { return mMeansDir; }
	bool meansFile() const noexcept { return mMeansFile; }
	bool isDotEntries() const noexcept { return mDotEntries; }
	bool meansRegularDir() const noexcept { return mMeansDir && !mDotEntries; }

	WINCSELIB_API bool meansHidden() const noexcept;
	WINCSELIB_API FileTypeEnum toFileType() const noexcept;
	WINCSELIB_API std::string bucketA() const;
	WINCSELIB_API std::string keyA() const;
	WINCSELIB_API std::string strA() const;
	WINCSELIB_API ObjectKey append(const std::wstring& arg) const noexcept;
	WINCSELIB_API ObjectKey toFile() const noexcept;
	WINCSELIB_API ObjectKey toDir() const noexcept;
	WINCSELIB_API std::filesystem::path toWinPath() const noexcept;
	WINCSELIB_API std::optional<ObjectKey> toParentDir() const;

private:
	template <wchar_t setV>
	WINCSELIB_API static std::optional<ObjectKey> fromXPath(const std::wstring& argPath) noexcept;

public:
	WINCSELIB_API static std::optional<ObjectKey> fromPath(const std::wstring& argPath) noexcept;
	WINCSELIB_API static std::optional<ObjectKey> fromWinPath(const std::filesystem::path& argWinPath) noexcept;
};

}	// namespace CSELIB

namespace std
{
// カスタムハッシュ関数 ... unorderd_map のキーになるために必要

template <>
struct hash<CSELIB::ObjectKey>
{
	size_t operator()(const CSELIB::ObjectKey& that) const noexcept
	{
		return hash<wstring>()(that.bucket()) ^ (hash<wstring>()(that.key()) << 1);
	}
};

}	// namespace std

// EOF