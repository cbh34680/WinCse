//#pragma once

#ifdef _DEBUG

#ifndef _CRTDBG_MAP_ALLOC
#error "_CRTDBG_MAP_ALLOC not defined"
#endif

#undef _CRTDBG_MAP_ALLOC

#undef new
#undef malloc
#undef realloc
#undef calloc
#undef free
#undef _strdup
#undef _wcsdup

#endif

// EOF