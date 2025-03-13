#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"


using namespace WinCseLib;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
NTSTATUS AwsS3::readObject(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    StatsIncr(read);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);
    APP_ASSERT(ctx->isFile());

    //return readObject_Simple(CONT_CALLER ctx, Buffer, Offset, Length, PBytesTransferred);
    return readObject_Multipart(CONT_CALLER ctx, Buffer, Offset, Length, PBytesTransferred);
}

std::wstring FileOutputMeta::str() const
{
    std::wstring sCreationDisposition;

    switch (mCreationDisposition)
    {
        case CREATE_ALWAYS:     sCreationDisposition = L"CREATE_ALWAYS";     break;
        case CREATE_NEW:        sCreationDisposition = L"CREATE_NEW";        break;
        case OPEN_ALWAYS:       sCreationDisposition = L"OPEN_ALWAYS";       break;
        case OPEN_EXISTING:     sCreationDisposition = L"OPEN_EXISTING";     break;
        case TRUNCATE_EXISTING: sCreationDisposition = L"TRUNCATE_EXISTING"; break;
        default: APP_ASSERT(0);
    }

    std::wstringstream ss;

    ss << L"mPath=";
    ss << mPath;
    ss << L" mCreationDisposition=";
    ss << sCreationDisposition;
    ss << L" mOffset=";
    ss << mOffset;
    ss << L" mLength=";
    ss << mLength;
    ss << L" mSpecifyRange=";
    ss << (mSpecifyRange ? L"true" : L"false");
    ss << L" mSetFileTime=";
    ss << (mSetFileTime ? L"true" : L"false");

    return ss.str();
}

// EOF