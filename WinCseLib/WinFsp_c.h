#pragma once

//
// オリジナルの "passthrough.c" と同じ動きをさせるためのスイッチ
//
#define WINFSP_PASSTHROUGH				(0)

//
// "ntstatus.h" を include するためには以下の記述 (define/undef) が必要だが
// 同じことが "winfsp/winfsp.h" で行われているのでコメント化
// 
//#define WIN32_NO_STATUS
//#include <windows.h>
//#undef WIN32_NO_STATUS

#pragma warning(push, 0)
#include <winfsp/winfsp.h>
#pragma warning(pop)

//
// passthrough.c に定義されていたもののうち、アプリケーションに
// 必要となるものを外だし
//
#define ALLOCATION_UNIT                 (4096)

typedef struct
{
#if WINFSP_PASSTHROUGH
    // 間違って利用されないようにコメント化

    HANDLE Handle;
#endif
    PVOID DirBuffer;

    // 追加情報
    struct
    {
        PWSTR FileName;
        FSP_FSCTL_FILE_INFO FileInfo;
        PVOID CSData;
    }
    Open;
}
PTFS_FILE_CONTEXT;

NTSTATUS WINCSELIB_API GetFileInfoInternal(HANDLE Handle, FSP_FSCTL_FILE_INFO* FileInfo);

// EOF