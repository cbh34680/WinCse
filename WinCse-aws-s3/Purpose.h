#pragma once

enum class Purpose
{
	None = 0,
	CheckDir = 1,
	Display = 2,
	CheckFile = 3,
};

extern const wchar_t* PurposeString(const Purpose);

// EOF