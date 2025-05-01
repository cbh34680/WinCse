#pragma once

namespace CSELIB {

//
// FSP_FSCTL_DIR_INFO �ɕt�^���������ڂ����邪�AWinFsp �̃��\�[�X�ł���
// ���ڊg������킯�ɂ͂����Ȃ��̂ŁA�����Ɏ����� View �Ƃ��ċ@�\������
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
	PCWSTR									FileNameBuf;		// allocDirInfo ���̃����o���Q�Ƃ��Ă���̂� PCWSTR �ō\��Ȃ�

	std::map<std::wstring, std::wstring>	mUserProperties;

public:
	explicit DirInfo(int argAllocId, FSP_FSCTL_DIR_INFO* argRawValue, const std::wstring& argFileName, FileTypeEnum argFileType) noexcept
		:
		mAllocId(argAllocId),						// �f�o�b�O���̃I�u�W�F�N�g�m�F�p
		mRawValue(argRawValue),						// malloc ���ꂽ�������Ȃ̂ŁA�f�X�g���N�^�� free
		FileInfo(argRawValue->FileInfo),			// FSP_FSCTL_DIR_INFO �̂悤�Ɍ��������邽�߂̎Q��
		FileNameBuf(argRawValue->FileNameBuf),		// �V
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