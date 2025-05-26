// test-compat-s3.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include <iostream>
#include <cassert>
#include <string>

#pragma comment(lib, "aws-cpp-sdk-core.lib")
#pragma comment(lib, "aws-cpp-sdk-s3.lib")

int listBuckets_main(const char* endpoint, const char* region, const char* accessKey, const char* secretKey);
int listObjects_main(const char* endpoint, const char* region, const char* accessKey, const char* secretKey, const char* bucket, const char* prefix);
int putObject_main(const char* endpoint, const char* region, const char* accessKey, const char* secretKey, const char* bucket, const char* prefix, const char* inputFile);

int main(int, char **argv)
{
    char *endpoint;
    char *region;
    char *key_id;
    char *secret;
    char *bucket;
    char prefix[] = "";
    size_t len;

    _dupenv_s(&endpoint, &len, "WINCSE_TEST_S3COMPAT_ENDPOINT");
    _dupenv_s(&region, &len, "WINCSE_TEST_S3COMPAT_REGION");
    _dupenv_s(&key_id, &len, "WINCSE_TEST_S3COMPAT_ACCESS_KEY_ID");
    _dupenv_s(&secret, &len, "WINCSE_TEST_S3COMPAT_SECRET_ACCESS_KEY");
    _dupenv_s(&bucket, &len, "WINCSE_TEST_S3COMPAT_BUCKET");

    assert(endpoint && region && key_id && secret && bucket);

    std::cout << endpoint << std::endl;
    std::cout << region << std::endl;
    std::cout << key_id << std::endl;
    std::cout << secret << std::endl;
    std::cout << bucket << std::endl;
    std::cout << prefix << std::endl;

    //listBuckets_main(endpoint, region, key_id, secret);
    //listObjects_main(endpoint, region, key_id, secret, bucket, prefix);
    putObject_main(endpoint, region, key_id, secret, bucket, prefix, __FILE__);
    putObject_main(endpoint, region, key_id, secret, bucket, prefix, "C:\\WORK\\0byte.txt");

    free(endpoint);
    free(region);
    free(key_id);
    free(secret);
    free(bucket);
}

// EOF