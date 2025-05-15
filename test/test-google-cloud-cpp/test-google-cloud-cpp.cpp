#pragma comment(lib, "google_cloud_cpp_storage.lib")
#pragma comment(lib, "google_cloud_cpp_rest_internal.lib")
#pragma comment(lib, "google_cloud_cpp_common.lib")
#pragma comment(lib, "abseil_dll.lib")
#pragma comment(lib, "libcurl-d.lib")

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "crc32c.lib")
#pragma comment(lib, "bcrypt.lib")

#include <Windows.h>
#include <memory>
#include <string_view>
#include <iostream>
#include <filesystem>

int quickstart_main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    char *credentials = nullptr;
    char *bucket_name = nullptr;

    size_t len;

    _dupenv_s(&credentials, &len, "GOOGLE_APPLICATION_CREDENTIALS");
    _dupenv_s(&bucket_name, &len, "WINCSE_TEST_GCS_BUCKET");

    std::unique_ptr<char, decltype(&free)> credentials_{ credentials, free };
    std::unique_ptr<char, decltype(&free)> bucket_name_{ bucket_name, free };

    std::cout << "credentials: " << credentials << std::endl;
    std::cout << "bucket_name: " << bucket_name << std::endl;

    if (strcmp(credentials, "") == 0 || strcmp(bucket_name, "") == 0)
    {
        std::cerr << "The configuration settings are incorrect." << std::endl;
        return 1;
    }

    if (!std::filesystem::exists(credentials))
    {
        std::cerr << credentials << ": file not exists" << std::endl;
        return 1;
    }

    // https://github.com/googleapis/google-cloud-cpp/tree/main/google/cloud/storage/quickstart
    // https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/quickstart/quickstart.cc
    // https://raw.githubusercontent.com/googleapis/google-cloud-cpp/refs/heads/main/google/cloud/storage/quickstart/quickstart.cc

    char* paramsQuickstart[] = { argv[0], bucket_name };

    const int retQuickstart = quickstart_main(_countof(paramsQuickstart), paramsQuickstart);
    std::cout << "retQuickstart=" << retQuickstart << std::endl;

    return 0;
}

// EOF