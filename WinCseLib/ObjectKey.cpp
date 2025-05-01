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
		// �L�[������  (mHasKey)
		//		�L�[�� ".", ".." �̂ǂ��炩					== �f�B���N�g�� (�h�b�g�E�G���g��)
		//		�Ōオ "/"			(ex. bucket/key/)		== �f�B���N�g��
		//      �Ōオ "/" �ł͂Ȃ�	(ex. bucket/key.txt)
		//			�Ōオ "/.", "/.." �̂ǂ��炩			== �f�B���N�g�� (�h�b�g�E�G���g��)
		// 			����ȊO								== �t�@�C��
		// 
		// �L�[���Ȃ� (!mHasKey)
		//							(ex. bucket)			== �f�B���N�g��
		//

		mHasKey = !mKey.empty();
		if (mHasKey)
		{
			if (mKey == L"." || mKey == L"..")
			{
				// ".", ".." �̓h�b�g�E�G���g���̃f�B���N�g��

				mMeansDir = mDotEntries = true;
			}
			else
			{
				if (mKey.back() == L'/')
				{
					// �Ōオ "/" �ŏI����Ă�����ʏ�̃f�B���N�g��

					mMeansDir = true;
				}
				else
				{
					const auto keyLen = mKey.length();

					if (keyLen >= 2 && mKey.substr(keyLen - 2) == L"/.")
					{
						// �Ōオ "/." �ŏI����Ă�����h�b�g�E�G���g���̃f�B���N�g��

						mMeansDir = mDotEntries = true;
					}
					else if (keyLen >= 3 && mKey.substr(keyLen - 3) == L"/..")
					{
						// �Ōオ "/.." �ŏI����Ă�����h�b�g�E�G���g���̃f�B���N�g��

						mMeansDir = mDotEntries = true;
					}
				}
			}

			mMeansFile = !mMeansDir;
		}
		else
		{
			// �L�[���Ȃ�(=�o�P�b�g) �̓f�B���N�g��

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
	// �L�[���������ꍇ�� "/" �ŕ��������e�f�B���N�g����Ԃ�
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
		// ".", ".." �ɂ͌��������Ȃ�

		APP_ASSERT(this->meansRegularDir());
	}

	return ObjectKey{ mBucket, mKey + arg };
}

ObjectKey ObjectKey::toFile() const noexcept
{
	//
	// �L�[�̌�납�� "/" ���폜�������̂�ԋp����
	// �L�[���Ȃ��ꍇ�͎����̃R�s�[��Ԃ�			--> �o�P�b�g���݂̂̏ꍇ
	// ���������t�@�C���̏ꍇ���R�s�[��Ԃ�
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
	// �L�[�̌��� "/" ��t�^�������̂�ԋp����
	// �L�[���Ȃ��ꍇ�͎����̃R�s�[��Ԃ�			--> �o�P�b�g���݂̂̏ꍇ
	// ���������f�B���N�g���̏ꍇ���R�s�[��Ԃ�
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
			// WinFsp ����R�[���o�b�N�����֐� (ex. Open()...) �̈�����
			// �t�@�C�����́A�f�B���N�g���ł������ꍇ�ł� "\" �I�[���Ă��Ȃ�
			// ���̓���ɍ��킹�A"/" �I�[�̏ꍇ�͗\�ߎ�菜��

			key.pop_back();
		}

		// "/" �� "\" �ɒu������

		std::replace(key.begin(), key.end(), L'/', L'\\');

		// �o�P�b�g�ƃL�[������

		ret += L'\\' + key;
	}

	return ret;
}

template <wchar_t setV>
std::optional<ObjectKey> ObjectKey::fromXPath(const std::wstring& argPath) noexcept
{
	// �p�X��������o�P�b�g���ƃL�[�ɕ���

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
				// "/" �ŕ������Ă���̂ŁA���͂̈�ԍŌ�ɂ��� "/" �������Ă��܂�
				// ���̏ꍇ���l�����āA�L�[�̍Ō�� "/" ��ǉ�

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