#pragma once

namespace CSELIB {

//
// FSP_FSCTL_DIR_INFO に付与したい項目があるが、WinFsp のリソースであり
// 直接拡張するわけにはいかないので、内部に持たせ View として機能させる
//
class DirInfo final
{
private:
	FSP_FSCTL_DIR_INFO* const				mRawValue;

public:
	const int								mAllocId;
	const std::wstring						FileName;
	const FileTypeEnum						FileType;

	FSP_FSCTL_FILE_INFO&					FileInfo;
	PCWSTR									FileNameBuf;		// allocDirInfo 内のメンバを参照しているので PCWSTR で構わない

	std::map<std::wstring, std::wstring>	mUserProperties;

public:
	explicit DirInfo(int argAllocId, FSP_FSCTL_DIR_INFO* argRawValue, const std::wstring& argFileName, FileTypeEnum argFileType) noexcept
		:
		mAllocId(argAllocId),						// デバッグ時のオブジェクト確認用
		mRawValue(argRawValue),						// malloc されたメモリなので、デストラクタで free
		FileInfo(argRawValue->FileInfo),			// FSP_FSCTL_DIR_INFO のように見せかけるための参照
		FileNameBuf(argRawValue->FileNameBuf),		// 〃
		FileName(argFileName),
		FileType(argFileType)
	{
	}

	FSP_FSCTL_DIR_INFO* data() const noexcept 
	{
		return mRawValue;
	}

	~DirInfo()
	{
		free(mRawValue);
	}

	WINCSELIB_API std::wstring str() const noexcept;
};

using DirInfoPtr = std::shared_ptr<DirInfo>;
using DirInfoPtrList = std::list<DirInfoPtr>;

}	// namespace CSELIB

// EOF