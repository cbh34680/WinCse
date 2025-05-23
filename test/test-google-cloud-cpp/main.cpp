#pragma comment(lib, "google_cloud_cpp_storage.lib")
#pragma comment(lib, "google_cloud_cpp_rest_internal.lib")
#pragma comment(lib, "google_cloud_cpp_common.lib")
#pragma comment(lib, "abseil_dll.lib")

#ifdef _DEBUG
#pragma comment(lib, "libcurl-d.lib")
#else
#pragma comment(lib, "libcurl.lib")
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "crc32c.lib")
#pragma comment(lib, "bcrypt.lib")

#include <optional>

#include "google/cloud/storage/client.h"
#include "google/cloud/internal/getenv.h"
#include <nlohmann/json.hpp>

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>

int quickstart_main(int argc, char* argv[]);

std::string time_to_string(const std::chrono::system_clock::time_point& argTime)
{
	time_t time = std::chrono::system_clock::to_time_t(argTime);
	struct tm tmLocal;

	localtime_s(&tmLocal, &time);

	std::ostringstream oss;
	oss << std::put_time(&tmLocal, "%Y-%m-%d %H:%M:%S");

    return oss.str();
}

void test_ListBuckets(google::cloud::storage::Client& client);
void test_ListObjects(google::cloud::storage::Client& client, const std::string& bucketName);
void test_ListObjectsAndPrefixes(google::cloud::storage::Client& client, const std::string& bucketName, const std::string& prefix);
void test_GetObjectMetadata(google::cloud::storage::Client& client, const std::string& bucketName, const std::string& objectName);
void test_ReadObject(google::cloud::storage::Client& client, const std::string& bucketName, const std::string& objectName);
void test_ReadObjectRange(google::cloud::storage::Client& client, const std::string& bucketName, const std::string& objectName, int64_t offsetStart, int64_t offsetEnd);

void test_all(google::cloud::storage::Client& client, const std::string& bucketName)
{
    try
    {
#if 0
        // https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/examples/storage_bucket_samples.cc

        test_ListBuckets(client);
#endif
#if 1
        // https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/examples/storage_object_samples.cc

        test_ListObjects(client, bucketName);

        std::cout << "bucket=[" << bucketName << "] prefix=[]" << std::endl;
        test_ListObjectsAndPrefixes(client, bucketName, "");

        std::cout << "bucket=[" << bucketName << "] prefix=[folder1/]" << std::endl;
        test_ListObjectsAndPrefixes(client, bucketName, "folder1/");

        std::cout << "bucket=[" << bucketName << "] prefix=[NOT-FOUND/]" << std::endl;
        test_ListObjectsAndPrefixes(client, bucketName, "NOT-FOUND/");

        std::cout << "bucket=[" << bucketName << "] prefix=[folder1/folder1-2/]" << std::endl;
        test_ListObjectsAndPrefixes(client, bucketName, "folder1/folder1-2/");
#endif
#if 0
        std::cout << "bucket=[" << bucketName << "] objectName=[folder1/1k.txt]" << std::endl;
        test_GetObjectMetadata(client, bucketName, "folder1/1k.txt");

        std::cout << "bucket=[" << bucketName << "] objectName=[folder1/NOT-FOUND.txt]" << std::endl;
        test_GetObjectMetadata(client, bucketName, "folder1/NOT-FOUND.txt");
#endif
#if 0
        std::cout << "bucket=[" << bucketName << "] objectName=[folder1/1k.txt]" << std::endl;
        test_ReadObject(client, bucketName, "folder1/1k.txt");
#endif
#if 0
        std::cout << "bucket=[" << bucketName << "] objectName=[folder1/1k.txt]" << std::endl;
        test_ReadObjectRange(client, bucketName, "folder1/1k.txt", 0, 5);
        test_ReadObjectRange(client, bucketName, "folder1/1k.txt", 5, 10);
        test_ReadObjectRange(client, bucketName, "folder1/1k.txt", 10, 15);

        // out of range
        test_ReadObjectRange(client, bucketName, "folder1/1k.txt", 1000000, 1000001);
#endif

    }
    catch (google::cloud::Status const& status)
    {
        std::cerr << "google::cloud::Status thrown: " << status << "\n";
    }
    catch (const std::exception& ex)
    {
        std::cerr << "exception: " << ex.what() << std::endl;
    }
}

int main()
{
    // credentials

    const auto credentials = google::cloud::internal::GetEnv("WINCSE_TEST_GCS_CREDENTIALS");

    if (!credentials || credentials->empty())
    {
        std::cerr << "credentials empty" << std::endl;
        return 1;
    }

    if (!std::filesystem::exists(*credentials))
    {
        std::cerr << *credentials << ": file not exists" << std::endl;
        return 1;
    }

    std::cout << "credentials: " << *credentials << std::endl;

    // project_id

    auto project_id = google::cloud::internal::GetEnv("WINCSE_TEST_GCS_PROJECT_ID");

    if (!project_id || project_id->empty())
    {
        std::ifstream file(*credentials);
        if (!file.is_open())
        {
            std::cerr << *credentials << ": open error" << std::endl;
            return 1;
        }

        nlohmann::json json;
        file >> json;

        if (!json.contains("project_id")) {
            std::cerr << "project_id not found in credentials file." << std::endl;
            return 1;
        }

        file.close();

        project_id = json["project_id"].get<std::string>();

        if (!project_id || *project_id == "")
        {
            std::cerr << "project_id empty" << std::endl;
            return 1;
        }

        std::cout << "project_id is " << *project_id << std::endl;
    }

    std::cout << "project_id: " << *project_id << std::endl;

    // bucket

    const auto bucket = google::cloud::internal::GetEnv("WINCSE_TEST_GCS_BUCKET");

    if (!bucket || bucket->empty())
    {
        std::cerr << "bucket empty" << std::endl;
        return 1;
    }

    std::cout << "bucket: " << *bucket << std::endl;

    // putenv()

    _putenv_s("GOOGLE_APPLICATION_CREDENTIALS", credentials->c_str());
    _putenv_s("GOOGLE_CLOUD_PROJECT", project_id->c_str());

    // https://github.com/googleapis/google-cloud-cpp/tree/main/google/cloud/storage/quickstart
    // https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/quickstart/quickstart.cc
    // https://raw.githubusercontent.com/googleapis/google-cloud-cpp/refs/heads/main/google/cloud/storage/quickstart/quickstart.cc

    //char* paramsQuickstart[] = { argv[0], bucket->c_str() };
    //const int retQuickstart = quickstart_main(_countof(paramsQuickstart), paramsQuickstart);
    //std::cout << "retQuickstart=" << retQuickstart << std::endl;

    auto client = google::cloud::storage::Client::CreateDefaultClient();
    if (!client)
    {
        std::cerr << "fault: CreateDefaultClient" << std::endl;
        return 1;
    }

    test_all(*client, *bucket);

    std::cout << "[exit]" << std::endl;

    return 0;
}

// EOF