#include <Windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>

void t_CPP_File()
{
	std::filesystem::create_directory("dir");

	std::ofstream ofs;
	ofs.open("dir\\file.txt", std::ios_base::out);
	if (ofs)
	{
		ofs.write("abc", 3);
		ofs.close();

		std::error_code ec;
		std::filesystem::remove("dir", ec);

		if (ec)
		{
			std::cout << ec.message() << std::endl;

			std::filesystem::remove("dir\\file.txt", ec);

			if (ec)
			{
				std::cout << ec.message() << std::endl;
			}
			else
			{
				std::filesystem::remove("dir", ec);

				if (ec)
				{
					std::cout << ec.message() << std::endl;
				}
				else
				{
					std::cout << "done." << std::endl;
				}
			}
		}
		else
		{
			std::cout << "done." << std::endl;
		}
	}
	else
	{
		std::cout << "open error" << std::endl;
	}

}

void t_CPP_File_Win32()
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
	std::cout << std::filesystem::exists(path, ec) << std::endl;

	std::cout << ec.value() << " " << ec.message() << std::endl;

	CloseHandle(h1);
	std::cout << "(2) " << ::GetFileAttributesA(path) << std::endl;
	std::cout << std::filesystem::exists(path, ec) << std::endl;

	std::cout << ec.value() << " " << ec.message() << std::endl;

	CloseHandle(h2);
	std::cout << "(3) " << ::GetFileAttributesA(path) << std::endl;
	std::cout << std::filesystem::exists(path, ec) << std::endl;

	std::cout << ec.value() << " " << ec.message() << std::endl;
}

// EOF