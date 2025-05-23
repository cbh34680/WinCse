#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>
std::string time_to_string(const std::chrono::system_clock::time_point& argTime);

void test_ListObjects(google::cloud::storage::Client& client, const std::string& bucketName)
{
	auto objects = client.ListObjects(bucketName);

	for (auto&& object_metadata: objects)
	{
		if (!object_metadata) throw std::move(object_metadata).status();

		std::cout << object_metadata->name() << std::endl;
		std::cout << time_to_string(object_metadata->time_created()) << std::endl;

		const auto& kvs = object_metadata->metadata();

		for (const auto& kv: kvs)
		{
			std::cout << "key=[" << kv.first << "] value=[" << kv.second << "]" << std::endl;
		}
	}
}

// EOF