#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>
std::string time_to_string(const std::chrono::system_clock::time_point& argTime);

void test_ListBuckets(google::cloud::storage::Client& client)
{
	//auto buckets = client.ListBucketsForProject(project_id);
	auto buckets = client.ListBuckets();

	for (auto&& bucket_metadata: buckets)
	{
		if (!bucket_metadata) throw std::move(bucket_metadata).status();

		std::cout << bucket_metadata->name() << std::endl;
		std::cout << time_to_string(bucket_metadata->time_created()) << std::endl;
	}
}

// EOF