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

FilePart::FilePart(WINCSE_DEVICE_STATS& argStats, UINT64 argOffset, ULONG argLength)
    : mStats(argStats), mOffset(argOffset), mLength(argLength)
{
    StatsIncr(_CreateEvent);

    mDone = ::CreateEventW(NULL,
        TRUE,				// 手動リセットイベント
        FALSE,				// 初期状態：非シグナル状態
        NULL);

    APP_ASSERT(mDone);
}

FilePart::~FilePart()
{
    StatsIncr(_CloseHandle_Event);
    ::CloseHandle(mDone);
}

//
// 外部からリクエストを行う複数のスレッドが、一つのファイルに同時に
// アクセスしないための仕組み
// 
// gNamedGuards はファイル名と Win32 Mutex のペアとなっており
// ファイルへの出力可能アクセスを行うときは
// WaitForSingleObject, ReleaseMutex により排他制御を行う。
//

//
// 以降は gGuard, gNamedGuards を使う関数
//
static std::mutex gGuard;
static std::unordered_map<std::wstring, std::unique_ptr<SharedBase>> gNameLocals;

template<typename T, typename... Args>
T* getUnprotectedNamedDataByName(const std::wstring& name, Args... args)
{
    std::lock_guard<std::mutex> lock_(gGuard);

    auto it{ gNameLocals.find(name) };
    if (it == gNameLocals.end())
    {
        it = gNameLocals.emplace(name, std::make_unique<T>(args...)).first;
    }

    it->second->mCount++;

    static_assert(std::is_base_of<SharedBase, T>::value, "T must be derived from SharedBase");

    T* t = dynamic_cast<T*>(it->second.get());
    APP_ASSERT(t);

    return t;
}

void releaseUnprotectedNamedDataByName(const std::wstring& name)
{
    std::lock_guard<std::mutex> lock_(gGuard);

    auto it{ gNameLocals.find(name) };

    it->second->mCount--;

    if (it->second->mCount == 0)
    {
        gNameLocals.erase(it);
    }
}

// EOF