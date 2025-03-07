#include "AwsS3.hpp"


using namespace WinCseLib;


OpenFileContext::~OpenFileContext()
{
    if (mFile != INVALID_HANDLE_VALUE)
    {
        StatsIncr(_CloseHandle_File);
        ::CloseHandle(mFile);
    }
}

bool AwsS3::openFile(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    const UINT32 CreateOptions, const UINT32 GrantedAccess,
    const FSP_FSCTL_FILE_INFO& argFileInfo, 
    PVOID* pUParam)
{
    StatsIncr(openFile);

    NEW_LOG_BLOCK();

    // DoOpen() ����Ăяo����邪�A�t�@�C�����J��=�_�E�����[�h�ɂȂ��Ă��܂�����
    // �����ł� UParam �ɏ��݂̂�ۑ����ADoRead() ����Ăяo����� readFile() ��
    // �t�@�C���̃_�E�����[�h���� (�L���b�V���E�t�@�C��) ���s���B

    OpenFileContext* ctx = new OpenFileContext(mStats, argBucket, argKey, CreateOptions, GrantedAccess, argFileInfo);
    APP_ASSERT(ctx);

    *pUParam = (PVOID*)ctx;

    return true;
}

void AwsS3::closeFile(CALLER_ARG PVOID UParam)
{
    StatsIncr(closeFile);

    APP_ASSERT(UParam);
    NEW_LOG_BLOCK();

    OpenFileContext* ctx = (OpenFileContext*)UParam;
    delete ctx;
}

// EOF