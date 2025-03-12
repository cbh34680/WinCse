#include "WinCseLib.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <dbghelp.h>

#pragma comment(lib, "Crypt32.lib")             // CryptBinaryToStringA
#pragma comment(lib, "Dbghelp.lib")             // SymInitialize


namespace WinCseLib {

void AbnormalEnd(const char* file, const int line, const char* func, const int signum)
{
	wchar_t szTempPath[MAX_PATH];
	::GetTempPathW(MAX_PATH, szTempPath);
	std::wstring tempPath{ szTempPath };

	const DWORD pid = ::GetCurrentProcessId();
	const DWORD tid = ::GetCurrentThreadId();

	std::wstringstream ssPath;
	ssPath << tempPath;

	if (std::filesystem::is_directory(tempPath + L"WinCse"))
	{
		ssPath << L"WinCse\\";
	}
	else
	{
		ssPath << L"WinCse-";
	}

	ssPath << L"abend-";
	ssPath << pid;
	ssPath << L'-';
	ssPath << tid;
	ssPath << L".log";

	std::ofstream ofs{ ssPath.str(), std::ios_base::app };

	//
	std::stringstream ssCause;
	ssCause << std::endl;
	ssCause << "cause; ";
	ssCause << file;
	ssCause << "(";
	ssCause << line;
	ssCause << "); signum(";
	ssCause << signum;
	ssCause << "); ";
	ssCause << func;
	ssCause << std::endl;
	ssCause << "GetLastError()=";
	ssCause << ::GetLastError();
	ssCause << std::endl;
	ssCause << std::endl;

	const std::string causeStr{ ssCause.str() };

	::OutputDebugStringA(causeStr.c_str());

	if (ofs)
	{
		ofs << causeStr;
	}

	//
	const int maxFrames = 62;
	void* stack[maxFrames];
	HANDLE process = ::GetCurrentProcess();
	::SymInitialize(process, NULL, TRUE);

	USHORT frames = ::CaptureStackBackTrace(0, maxFrames, stack, NULL);
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (USHORT i = 0; i < frames; i++)
	{
		::SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);

		std::stringstream ss;
		ss << frames - i - 1;
		ss << ": ";
		ss << symbol->Name;
		ss << " - 0x";
		ss << symbol->Address;
		ss << std::endl;

		const std::string ss_str{ ss.str() };

		::OutputDebugStringA(ss_str.c_str());

		if (ofs)
		{
			ofs << ss_str;
		}
	}

	free(symbol);

	ofs.close();

	abort();
}

// malloc, calloc �Ŋm�ۂ����������� shared_ptr �ŉ�����邽�߂̊֐�
template <typename T>
void free_deleter(T* ptr)
{
	free(ptr);
}

// �t�@�C�������� FSP_FSCTL_DIR_INFO �̃q�[�v�̈�𐶐����A�������̃����o��ݒ肵�ĕԋp
DirInfoType makeDirInfo(const ObjectKey& argObjKey)
{
	const auto& key{ argObjKey.key() };

	APP_ASSERT(!key.empty());		// �o�P�b�g�����L�[�ɓ���ė��p���邱�Ƃ�����̂ŁAhasKey �ł͔���ł��Ȃ�

	const auto keyLen = key.length();
	const auto keyLenBytes = keyLen * sizeof(WCHAR);
	const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
	const auto dirInfoSize = offFileNameBuf + keyLenBytes;
	const auto allocSize = dirInfoSize + sizeof(WCHAR);

	FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)calloc(1, allocSize);
	APP_ASSERT(dirInfo);

	dirInfo->Size = (UINT16)dirInfoSize;

	//dirInfo->FileInfo.IndexNumber = HashString(bucket.empty() ? key : bucket + L'/' + key);
	dirInfo->FileInfo.IndexNumber = HashString(argObjKey.str());

	//
	// ���s���ɃG���[�ƂȂ� (Buffer is too small)
	// 
	// �����炭�AFSP_FSCTL_DIR_INFO.FileNameBuf �� [] �Ƃ��Ē�`����Ă��邽��
	// wcscpy_s �ł� 0 byte �̈�ւ̃o�b�t�@�E�I�[�o�[�t���[�Ƃ��ĔF�������
	// ���܂��̂ł͂Ȃ����Ǝv��
	// 
	//wcscpy_s(dirInfo->FileNameBuf, wkeyLen, wkey.c_str());

	memcpy(dirInfo->FileNameBuf, key.c_str(), keyLenBytes);

	return DirInfoType(dirInfo, free_deleter<FSP_FSCTL_DIR_INFO>);
}

// argKey                       parentDir       filename
// ------------------------------------------------------
// ""                      NG
// "dir"                   OK   ""              "dir"       
// "dir/"                  OK   ""              "dir"
// "dir/key.txt"           OK   "dir/"          "key.txt"
// "dir/key.txt/"          OK   "dir/"          "key.txt/"
// "dir/subdir/key.txt"    OK   "dir/subdir/"   "key.txt"
// "dir/subdir/key.txt/"   OK   "dir/subdir/"   "key.txt/"

bool SplitPath(const std::wstring& argKey, std::wstring* pParentDir /* nullable */, std::wstring* pFilename /* nullable */)
{
	// �L�[����e�f�B���N�g�����擾

	auto tokens{ SplitString(argKey, L'/', false) };
	if (tokens.empty())
	{
		return false;
	}

	auto filename{ tokens.back() };
	if (filename.empty())
	{
		return false;
	}

	tokens.pop_back();

	// �����Ώۂ̐e�f�B���N�g��

	auto parentDir{ JoinStrings(tokens, L'/', false) };
	if (parentDir.empty())
	{
		// �o�P�b�g�̃��[�g�E�f�B���N�g�����猟��

		// "" --> ""
	}
	else
	{
		// �T�u�f�B���N�g�����猟��

		// "dir"        --> "dir/"
		// "dir/subdir" --> "dir/subdir/"

		parentDir += L'/';
	}

	// �����Ώۂ̃t�@�C���� (�f�B���N�g����)

	if (argKey.back() == L'/')
	{
		// SplitString() �� "/" ��������Ă��܂��̂ŁAargKey �� "dir/" �� "dir/file.txt/"
		// ���w�肳��Ă���Ƃ��� filename �� "/" ��t�^

		filename += L'/';
	}

	if (pParentDir)
	{
		*pParentDir = std::move(parentDir);
	}

	if (pFilename)
	{
		*pFilename = std::move(filename);
	}

	return true;
}

#define INI_LINE_BUFSIZ		(1024)

bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue)
{
	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	std::vector<wchar_t> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringW(argSection, keyName, L"", buf.data(), (DWORD)buf.size(), confPath.c_str());

	if (::GetLastError() != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::wstring(buf.data());

	return true;
}

bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue)
{
	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	std::vector<char> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringA(argSection, keyName, "", buf.data(), (DWORD)buf.size(), confPath.c_str());

	if (::GetLastError() != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::string(buf.data());

	return true;
}

//
// ObjectKey
//
void ObjectKey::reset()
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

ObjectKey::ObjectKey(const std::wstring& argWinPath)
{
	// Windows �p�X��������o�P�b�g���ƃL�[�ɕ���

	std::wstringstream input{ argWinPath };

	std::vector<std::wstring> tokens;
	std::wstring token;

	while (std::getline(input, token, L'\\'))
	{
		tokens.emplace_back(std::move(token));
	}

	switch (tokens.size())
	{
		case 0:
		case 1:
		{
			// mBucket is empty
			// mKey is empty

			break;
		}
		case 2:
		{
			mBucket = std::move(tokens[1]);
			// mKey is empty

			break;
		}
		default:
		{
			mBucket = std::move(tokens[1]);

			std::wstringstream output;
			for (int i = 2; i < tokens.size(); ++i)
			{
				if (i != 2)
				{
					output << L'/';
				}
				output << std::move(tokens[i]);
			}
			mKey = output.str();

			break;
		}
	}

	if (!mKey.empty())
	{
		// "\\" �ŕ������邽�߁A�����̍Ōオ "\\" �ł��Ȃ����Ă��܂�
		// �̂ŁA�����ŕ�U����

		if (argWinPath.back() == L'\\')
		{
			mKey += L'/';
		}
	}

	reset();
}

//
// std::map �̃L�[�ɂ���ꍇ�ɕK�v
//
bool ObjectKey::operator<(const ObjectKey& other) const
{
	if (mBucket < other.mBucket) {			// bucket
		return true;
	}
	else if (mBucket > other.mBucket) {
		return false;
	}
	else if (mKey < other.mKey) {				// key
		return true;
	}
	else if (mKey > other.mKey) {
		return false;
	}

	return false;
}

bool ObjectKey::operator>(const ObjectKey& other) const
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

ObjectKey ObjectKey::toDir() const
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

std::unique_ptr<ObjectKey> ObjectKey::toParentDir() const
{
	//
	// �L�[���������ꍇ�� "/" �ŕ��������e�f�B���N�g����Ԃ�
	//

	if (mHasBucket && mHasKey)
	{
		std::wstring parentDir;
		if (SplitPath(mKey, &parentDir, nullptr))
		{
			return std::make_unique<ObjectKey>(mBucket, parentDir);
		}
	}

	return nullptr;
}

std::string ObjectKey::bucketA() const
{
	return WC2MB(mBucket);
}

std::string ObjectKey::keyA() const
{
	return WC2MB(mKey);
}

std::string ObjectKey::strA() const
{
	return WC2MB(mBucketKey);
}

//
// LogBlock
//
static std::atomic<int> mCounter(0);
static thread_local int mDepth = 0;

LogBlock::LogBlock(const wchar_t* argFile, const int argLine, const wchar_t* argFunc)
	: mFile(argFile), mLine(argLine), mFunc(argFunc)
{
	mCounter++;

	GetLogger()->traceW_impl(mDepth, mFile, mLine, mFunc, L"{enter}");
	mDepth++;
}

LogBlock::~LogBlock()
{
	mDepth--;
	GetLogger()->traceW_impl(mDepth, mFile, -1, mFunc, L"{leave}");
}

int LogBlock::depth()
{
	return mDepth;
}

int LogBlock::getCount()
{
	return mCounter.load();
}

} // namespace WinCseLib

// EOF
