#pragma once

#include "ObjectCacheTemplate.hpp"

class ListObjectsCache : public ObjectCacheTemplate<WCSE::DirInfoListType>
{
public:
    void report(CALLER_ARG FILE* fp);
};

// EOF