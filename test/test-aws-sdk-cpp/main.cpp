// test-aws-sdk-cpp.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include <iostream>
#include <Windows.h>

#pragma comment(lib, "aws-cpp-sdk-core.lib")
#pragma comment(lib, "aws-cpp-sdk-s3.lib")

// https://github.com/aws/aws-sdk-cpp/issues/3209
#define USE_IMPORT_EXPORT
//#define USE_WINDOWS_DLL_SEMANTICS

#pragma warning(push, 0)
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/s3/S3Client.h>
//#include <aws/s3/model/ListBucketsRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/GetObjectRequest.h>
#pragma warning(pop)

#undef USE_IMPORT_EXPORT

#include <functional>
#include <list>
#include <vector>
#include <map>

void FEP(const std::function<void(Aws::S3::S3Client*, const char*)>& callback)
{
    Aws::SDKOptions options;
    Aws::InitAPI(options);

    char *region;
    char *key_id;
    char *secret;
    char *bucket;
    size_t len;

    _dupenv_s(&region, &len, "WINCSE_TEST_AWS_REGION");
    _dupenv_s(&key_id, &len, "WINCSE_TEST_AWS_ACCESS_KEY_ID");
    _dupenv_s(&secret, &len, "WINCSE_TEST_AWS_SECRET_ACCESS_KEY");
    _dupenv_s(&bucket, &len, "WINCSE_TEST_AWS_BUCKET");

    assert(region && key_id && secret && bucket);

    //std::cout << region << std::endl;
    //std::cout << key_id << std::endl;
    //std::cout << secret << std::endl;
    //std::cout << bucket << std::endl;

    Aws::Client::ClientConfiguration config;
    config.region = region;

    Aws::String access_key{ key_id };
    Aws::String secret_key{ secret };

    Aws::Auth::AWSCredentials credentials{ access_key, secret_key };
    auto client = new Aws::S3::S3Client(credentials, nullptr, config);

    callback(client, bucket);

    delete client;

    free(region);
    free(key_id);
    free(secret);
    free(bucket);
}

void test_listBuckets(Aws::S3::S3Client* client, const char* envBucket)
{
    auto outcome = client->ListBuckets();
    if (!outcome.IsSuccess())
    {
        std::cerr << "Error: ListBuckets: " << outcome.GetError().GetMessage() << std::endl;
        return;
    }

    const auto& result = outcome.GetResult();
    for (const auto& bucket: result.GetBuckets())
    {
        std::cout << bucket.GetName() << std::endl;
        std::cout << bucket.GetCreationDate().ToGmtString("%Y-%m-%d %H:%M:%S") << std::endl;
    }
}

static void test_listObjects(Aws::S3::S3Client* client, const char* envBucket)
{
    Aws::S3::Model::ListObjectsV2Request request;

    request.SetBucket(envBucket);
    request.SetPrefix("test/");
    request.SetDelimiter("/");

    Aws::String continuationToken;

    do
    {
        if (!continuationToken.empty())
        {
            request.SetContinuationToken(continuationToken);
        }

        auto outcome = client->ListObjectsV2(request);
        if (!outcome.IsSuccess())
        {
            std::cerr << "Error: listObjects: " << outcome.GetError().GetMessage() << std::endl;
            break;
        }

        auto& result = outcome.GetResult();

        for (const auto& it : result.GetCommonPrefixes())
        {
            const std::string fullPath{ it.GetPrefix().c_str() };

            std::cout << "1 [" << fullPath << "]" << std::endl;
        }

        for (const auto& it: result.GetContents())
        {
            std::cout << "2 [" << it.GetKey() << "]" << std::endl;
        }

        continuationToken = outcome.GetResult().GetNextContinuationToken();
    }
    while(!continuationToken.empty());
}

static void test_getObject(Aws::S3::S3Client* client, const char* envBucket)
{
    Aws::S3::Model::GetObjectRequest request;

    request.SetBucket(envBucket);
    request.SetKey("55b.txt");
    request.SetRange("bytes=0-100");

    const auto outcome = client->GetObject(request);

    if (!outcome.IsSuccess())
    {
        std::cerr << "Error: GetObject: " << outcome.GetError().GetMessage() << std::endl;
        return;
    }

    const auto& result = outcome.GetResult();

    char buf[512] = {};

    const auto pbuf = result.GetBody().rdbuf();
    const auto contentSize = result.GetContentLength();  // ファイルサイズ

    const auto rn = pbuf->sgetn(buf, sizeof(buf));


    std::cout << buf << std::endl;
}

static void test_putObject(int argc, char** argv)
{
    char ns[] = "@";
    char *region;
    char *key_id;
    char *secret;
    char *bucket;
    char prefix[] = "";
    size_t len;

    _dupenv_s(&region, &len, "WINCSE_TEST_AWS_REGION");
    _dupenv_s(&key_id, &len, "WINCSE_TEST_AWS_ACCESS_KEY_ID");
    _dupenv_s(&secret, &len, "WINCSE_TEST_AWS_SECRET_ACCESS_KEY");
    _dupenv_s(&bucket, &len, "WINCSE_TEST_AWS_BUCKET");

    assert(region && key_id && secret && bucket);

    std::cout << region << std::endl;
    std::cout << key_id << std::endl;
    std::cout << secret << std::endl;
    std::cout << bucket << std::endl;
    std::cout << prefix << std::endl;

    char* paramsObjects[] = { argv[0], ns, region, key_id, secret, bucket, prefix };

    int putObject_main(int argc, char **argv);
    putObject_main(_countof(paramsObjects), paramsObjects);

}

int main(int argc, char** argv)
{
    //FEP(test_listBuckets);
    //FEP(test_listObjects);
    //FEP(test_getObject);

    test_putObject(argc, argv);

    return 0;
}

// EOF