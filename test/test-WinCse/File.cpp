#include <Windows.h>
#include <iostream>
#include <filesystem>

void t_CPP_File()
{
	char path[] = "file.tmp";

	HANDLE h1 = ::CreateFileA(
		path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
		NULL);

	HANDLE h2 = ::CreateFileA(
		path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	std::cout << "(1) " << ::GetFileAttributesA(path) << std::endl;

	std::error_code ec;
	std::filesystem::exists(path, ec);

	std::cout << ec.value() << " " << ec.message() << std::endl;

	CloseHandle(h1);
	std::cout << "(2) " << ::GetFileAttributesA(path) << std::endl;
	std::filesystem::exists(path, ec);

	std::cout << ec.value() << " " << ec.message() << std::endl;

	CloseHandle(h2);
	std::cout << "(3) " << ::GetFileAttributesA(path) << std::endl;
	std::filesystem::exists(path, ec);

	std::cout << ec.value() << " " << ec.message() << std::endl;
}

// EOF