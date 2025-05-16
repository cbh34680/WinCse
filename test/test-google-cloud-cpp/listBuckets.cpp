#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>

void listBuckets_main(google::cloud::storage::Client& client)
{
	//auto buckets = client.ListBucketsForProject(project_id);
	auto buckets = client.ListBuckets();

	for (auto&& bucket_metadata: buckets)
	{
		if (!bucket_metadata) throw std::move(bucket_metadata).status();

		std::cout << bucket_metadata->name() << std::endl;

		time_t time = std::chrono::system_clock::to_time_t(bucket_metadata->time_created());
		struct tm tmLocal;

		localtime_s(&tmLocal, &time);

		std::ostringstream oss;
		oss << std::put_time(&tmLocal, "%Y-%m-%d %H:%M:%S");
		std::cout << oss.str() << std::endl;
	}
}

// EOF