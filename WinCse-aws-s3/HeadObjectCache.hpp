#pragma once

#include "ObjectCacheTemplate.hpp"

class HeadObjectCache : public ObjectCacheTemplate<WCSE::DirInfoType>
{
public:
    void report(CALLER_ARG FILE* fp);
};

// EOF