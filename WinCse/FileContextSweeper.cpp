#include "FileContextSweeper.hpp"

using namespace CSELIB;
using namespace CSEDRV;

//
// �G�N�X�v���[���[���J�����܂ܐؒf����� WinFsp �� Close �����s����Ȃ�
// �̂ŁARelayOpen ���Ă΂�� RelayClose ���Ă΂�Ă��Ȃ����̂́A�A�v���P�[�V�����I������
// �����I�� RelayClose ���Ăяo��
// 
// ���u���Ă����͂Ȃ����A�f�o�b�O���Ƀ��������[�N�Ƃ��ĕ񍐂���Ă��܂�
// �{���̈Ӗ��ł̃��������[�N�ƍ��݂��Ă��܂�����
// 
// https://groups.google.com/g/winfsp/c/c4kYcA6p8OQ/m/OBBLVfXADgAJ?utm_medium=email&utm_source=footer
//

FileContextSweeper::~FileContextSweeper()
{
	NEW_LOG_BLOCK();

	// RelayClose �� mOpenAddrs.erase() ������̂ŃR�s�[���K�v

	auto copy{ mOpenAddrs };

	for (auto* addr: copy)
	{
		traceW(L"force close address=%p", addr);

		delete addr;
	}
}

#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ mGuard }

void FileContextSweeper::add(FileContext* ctx)
{
	THREAD_SAFE();
	APP_ASSERT(mOpenAddrs.find(ctx) == mOpenAddrs.cend());

	mOpenAddrs.insert(ctx);
}

void FileContextSweeper::remove(FileContext* ctx)
{
	THREAD_SAFE();
	APP_ASSERT(mOpenAddrs.find(ctx) != mOpenAddrs.cend());

	mOpenAddrs.erase(ctx);
}

// EOF