#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>
std::string time_to_string(const std::chrono::system_clock::time_point& argTime);

void test_ReadObjectRange(google::cloud::storage::Client& client, const std::string& bucketName, const std::string& objectName, int64_t offsetStart, int64_t offsetEnd)
{
	auto stream = client.ReadObject(bucketName, objectName, google::cloud::storage::ReadRange(offsetStart, offsetEnd));

	char buf[1024];

	do
	{
		stream.read(buf, _countof(buf));

		std::cout << "gcount=" << stream.gcount() << std::endl;

		std::string s{ std::begin(buf), &buf[stream.gcount()] };
		std::cout << s << std::endl;
	}
	while (stream.good());
}

// EOF