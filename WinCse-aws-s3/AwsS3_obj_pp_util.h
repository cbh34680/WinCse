#pragma once

constexpr UINT64 PART_SIZE_BYTE = WCSE::FILESIZE_1MiBu * 4;

struct FilePart
{
    WINCSE_DEVICE_STATS* mStats;
    const UINT64 mOffset;
    const ULONG mLength;

    WCSE::EventHandle mDone;
    bool mResult = false;

    std::atomic<bool> mInterrupt = false;

    FilePart(WINCSE_DEVICE_STATS* argStats, UINT64 argOffset, ULONG argLength)
        : mStats(argStats), mOffset(argOffset), mLength(argLength)
    {
        mDone = ::CreateEventW(NULL,
            TRUE,				// 手動リセットイベント
            FALSE,				// 初期状態：非シグナル状態
            NULL);

        APP_ASSERT(mDone.valid());
    }

    void SetResult(bool argResult)
    {
        mResult = argResult;
        const auto b = ::SetEvent(mDone.handle());					// シグナル状態に設定
        APP_ASSERT(b);
    }

    ~FilePart()
    {
        mDone.close();
    }
};

// EOF