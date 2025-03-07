#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"


using namespace WinCseLib;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
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