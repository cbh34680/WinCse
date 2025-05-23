#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>
std::string time_to_string(const std::chrono::system_clock::time_point& argTime);

void test_ListObjectsAndPrefixes(google::cloud::storage::Client& client, const std::string& bucketName, const std::string& argPrefix)
{
	auto items = client.ListObjectsAndPrefixes(bucketName, google::cloud::storage::Delimiter("/"), google::cloud::storage::Prefix(argPrefix));

	for (auto&& item: items)
	{
		if (!item) throw std::move(item).status();

		auto&& result = *std::move(item);

		if (absl::holds_alternative<std::string>(result))
		{
			auto&& prefix = absl::get<std::string>(result);

			std::cout << "prefix=" << prefix << std::endl;
		}
		else if (absl::holds_alternative<google::cloud::storage::ObjectMetadata>(result))
		{
			auto&& object = absl::get<google::cloud::storage::ObjectMetadata>(result);

			std::cout << "object.name=" << object.name() << std::endl;
			std::cout << time_to_string(object.time_created()) << std::endl;

			const auto& kvs = object.metadata();

			for (const auto& kv: kvs)
			{
				std::cout << "key=[" << kv.first << "] value=[" << kv.second << "]" << std::endl;
			}
		}
		else
		{
			std::cout << "unknown" << std::endl;
		}
	}
}

// EOF