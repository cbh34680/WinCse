//#pragma once

#ifdef _DEBUG

#ifdef _CRTDBG_MAP_ALLOC
#undef _CRTDBG_MAP_ALLOC
#endif

#undef new
#undef malloc
#undef realloc
#undef calloc
#undef free
#undef _strdup
#undef _wcsdup

#endif

// EOF