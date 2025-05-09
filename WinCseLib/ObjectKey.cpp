#include "WinCseLib.h"

using namespace CSELIB;


ObjectKey::ObjectKey(const std::wstring& argBucket, const std::wstring& argKey)
	:
	mBucket(argBucket),
	mKey(argKey)
{
	APP_ASSERT(!mBucket.empty());

	// �t���O�̃p�^�[��
	// 
	// !mHasKey &&  mMeansDir	... "bucket"				--> �o�P�b�g
	// !mHasKey && !mMeansDir	... ---
	//  mHasKey &&  mMeansDir	... "bucket/dir/"			--> �I�u�W�F�N�g
	//  mHasKey && !mMeansDir	... "bucket/dir/file.txt"	--> �V

	mHasKey = !mKey.empty();

	if (mHasKey)
	{
		// �L�[������ꍇ�͍Ō�̕����� "/" ���ǂ����ɂ��t�@�C�����f�B���N�g�����𔻒f����

		APP_ASSERT(mKey != L"." && mKey != L".." && mKey != L"/");

		mMeansDir = mKey.back() == L'/';
		
	}
	else
	{
		// �L�[���Ȃ�(=�o�P�b�g) �̓f�B���N�g��

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
	// �L�[���������ꍇ�� "/" �ŕ��������e�f�B���N�g����Ԃ�

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
std::optional<ObjectKey> ObjectKey::fromXPath(const std::wstring& argPath)
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

// EOF