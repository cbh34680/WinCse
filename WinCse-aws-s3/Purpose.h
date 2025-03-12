#pragma once

enum class Purpose
{
	None = 0,
	CheckDirExists = 1,
	Display = 2,
	CheckFileExists = 3,
};

extern const wchar_t* PurposeString(const Purpose);

// EOF