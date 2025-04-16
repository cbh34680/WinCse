#pragma once

namespace WCSE
{

// ��������o�P�b�g���ƃL�[�ɕ���
class ObjectKey
{
private:
	std::wstring mBucket;
	std::wstring mKey;

	std::wstring mBucketKey;
	bool mHasBucket = false;
	bool mHasKey = false;
	bool mMeansDir = false;
	bool mMeansFile = false;

	void reset() noexcept
	{
		mHasBucket = !mBucket.empty();
		mHasKey = !mKey.empty();
		mBucketKey = mBucket + L'/' + mKey;

		//
		// �L�[���� (!mHasKey) --> bucket			== �f�B���N�g��
		// ��łȂ� (mHasKey)  --> bucket/key		== �t�@�C��
		//                         bucket/key/		== �f�B���N�g��
		//
		mMeansDir = mHasBucket ? (!mHasKey || (mHasKey && mKey.back() == L'/')) : false;
		mMeansFile = mHasBucket ? (mHasKey && mKey.back() != L'/') : false;
	}

public:
	ObjectKey() = default;

	explicit ObjectKey(const std::wstring& argBucket, const std::wstring& argKey) noexcept
		:
		mBucket(argBucket),
		mKey(argKey)
	{
		reset();
	}

	// unorderd_map �̃L�[�ɂȂ邽�߂ɕK�v
	bool operator==(const ObjectKey& other) const noexcept
	{
		return mBucket == other.mBucket && mKey == other.mKey;
	}

	// map �̃L�[�ɂȂ邽�߂ɕK�v
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

	// ObjectCacheKey �ŕK�v�ɂȂ���
	bool operator>(const ObjectKey& other) const noexcept
	{
		if (this->operator<(other))
		{
			return false;
		}
		else if (this->operator==(other))
		{
			return false;
		}

		return true;
	}

	ObjectKey toFile() const noexcept
	{
		//
		// �L�[�̌��� "/" ��t�^�������̂�ԋp����
		// �L�[���Ȃ��ꍇ�͎����̃R�s�[��Ԃ�			--> �o�P�b�g���݂̂̏ꍇ
		// ���������f�B���N�g���̏ꍇ���R�s�[��Ԃ�
		//

		if (mHasBucket && mHasKey)
		{
			if (mMeansDir)
			{
				return ObjectKey{ mBucket, mKey.substr(0, mKey.size() - 1) };
			}
		}

		return *this;
	}

	ObjectKey toDir() const noexcept
	{
		//
		// �L�[�̌��� "/" ��t�^�������̂�ԋp����
		// �L�[���Ȃ��ꍇ�͎����̃R�s�[��Ԃ�			--> �o�P�b�g���݂̂̏ꍇ
		// ���������f�B���N�g���̏ꍇ���R�s�[��Ԃ�
		//

		if (mHasBucket && mHasKey)
		{
			if (mMeansFile)
			{
				return ObjectKey{ mBucket, mKey + L'/' };
			}
		}

		return *this;
	}

	ObjectKey append(const std::wstring& arg) const noexcept { return ObjectKey{ mBucket, mKey + arg }; }

	const std::wstring& bucket() const noexcept { return mBucket; }
	const std::wstring& key() const noexcept { return mKey; }

	bool valid() const noexcept { return mHasBucket; }
	bool invalid() const noexcept { return !mHasBucket; }

	bool isBucket() const noexcept { return mHasBucket && !mHasKey; }
	bool isObject() const noexcept { return mHasBucket && mHasKey; }

	const std::wstring& str() const noexcept { return mBucketKey; }
	PCWSTR c_str() const noexcept { return mBucketKey.c_str(); }

	bool meansDir() const noexcept { return mMeansDir; }
	bool meansFile() const noexcept { return mMeansFile; }

	bool meansHidden() const noexcept
	{
		if (mHasKey)
		{
			// ".", ".." �ȊO�Ő擪�� "." �Ŏn�܂��Ă�����͉̂B���t�@�C���̈���

			if (mKey != L"." && mKey != L".." && mKey.at(0) == L'.')
			{
				return true;
			}
		}

		return false;
	}

	WINCSELIB_API static ObjectKey fromPath(const std::wstring& argPath);
	WINCSELIB_API static ObjectKey fromWinPath(const std::wstring& argWinPath);
	WINCSELIB_API std::optional<ObjectKey> toParentDir() const;
	WINCSELIB_API std::string bucketA() const;
	WINCSELIB_API std::string keyA() const;
	WINCSELIB_API std::string strA() const;
};

}

// EOF