#pragma once

#include "CSDriverCommon.h"
#include "FileContext.hpp"

namespace CSEDRV
{

class FileContextSweeper final
{
private:
	std::set<FileContext*>	mOpenAddrs;
	mutable std::mutex		mGuard;

public:
	void add(FileContext* ctx);
	void remove(FileContext* ctx);

	~FileContextSweeper();
};

}	// namespace CSELIB

// EOF