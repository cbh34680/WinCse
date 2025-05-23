//#pragma once

#ifdef _DEBUG

#ifdef _CRTDBG_MAP_ALLOC
#error "_CRTDBG_MAP_ALLOC already defined"
#endif

#define _CRTDBG_MAP_ALLOC

#include <cstdlib>
#include <crtdbg.h>

#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define malloc(size) _malloc_dbg(size, _NORMAL_BLOCK, __FILE__, __LINE__)
#define realloc(ptr, size) _realloc_dbg(ptr, size, _NORMAL_BLOCK, __FILE__, __LINE__)
#define calloc(num, size) _calloc_dbg(num, size, _NORMAL_BLOCK, __FILE__, __LINE__)
#define free(ptr) _free_dbg(ptr, _NORMAL_BLOCK)
#define _strdup(str) _strdup_dbg(str, _NORMAL_BLOCK, __FILE__, __LINE__)
#define _wcsdup(str) _wcsdup_dbg(str, _NORMAL_BLOCK, __FILE__, __LINE__)

#endif

// EOF