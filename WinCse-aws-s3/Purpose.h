#pragma once

enum class Purpose
{
	None = 0,
	CheckDirExists = 1,
	Display = 2,
	CheckFileExists = 3,
};

extern PCWSTR PurposeString(const Purpose);

// EOF