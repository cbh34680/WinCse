#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>
std::string time_to_string(const std::chrono::system_clock::time_point& argTime);

void test_GetObjectMetadata(google::cloud::storage::Client& client, const std::string& bucketName, const std::string& objectName)
{
	auto object_metadata = client.GetObjectMetadata(bucketName, objectName);
	if (!object_metadata) throw std::move(object_metadata).status();

	std::cout << "name=" << object_metadata->name() << std::endl;
	std::cout << "bucket=" << object_metadata->bucket() << std::endl;
	std::cout << "time_created=" << time_to_string(object_metadata->time_created()) << std::endl;
	std::cout << *object_metadata << std::endl;

	const auto& kvs = object_metadata->metadata();

	for (const auto& kv: kvs)
	{
		std::cout << "key=[" << kv.first << "] value=[" << kv.second << "]" << std::endl;
	}
}

// EOF