#pragma once

#include "WinCseLib.h"

constexpr UINT64 PART_SIZE_BYTE = WCSE::FILESIZE_1MiBu * 4;

struct FilePart
{
    const UINT64 mOffset;
    const ULONG mLength;

    WCSE::EventHandle mDone;
    bool mResult = false;

    std::atomic<bool> mInterrupt = false;

    explicit FilePart(UINT64 argOffset, ULONG argLength) noexcept
        :
        mOffset(argOffset),
        mLength(argLength)
    {
        mDone = ::CreateEventW(NULL,
            TRUE,				// 手動リセットイベント
            FALSE,				// 初期状態：非シグナル状態
            NULL);

        APP_ASSERT(mDone.valid());
    }

    void SetResult(bool argResult) noexcept
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