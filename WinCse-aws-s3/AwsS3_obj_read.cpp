#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"


using namespace WinCseLib;


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
bool AwsS3::readFile(CALLER_ARG PVOID UParam,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    StatsIncr(readFile);

    APP_ASSERT(UParam);
    NEW_LOG_BLOCK();

    ReadFileContext* ctx = (ReadFileContext*)UParam;

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu", ctx->mFile, Offset, Length);

    //const auto ret = readFile_Simple(CONT_CALLER UParam, Buffer, Offset, Length, PBytesTransferred);
    const auto ret = readFile_Multipart(CONT_CALLER UParam, Buffer, Offset, Length, PBytesTransferred);

    StatsIncrBool(ret, _ReadSuccess, _ReadError);

    return ret;
}

// EOF