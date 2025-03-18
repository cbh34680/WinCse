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

#include <aws/s3/model/BucketLocationConstraint.h>
#include <aws/s3/model/Bucket.h>
#include <aws/s3/model/GetBucketLocationRequest.h>
#include <aws/s3/model/HeadBucketRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>

#undef USE_IMPORT_EXPORT

int test1()
{
    Aws::SDKOptions options;
    Aws::InitAPI(options);

    char *region;
    char *key_id;
    char *secret;
    char *bucket;
    size_t len;

    _dupenv_s( &region, &len, "WINCSE_AWS_DEFAULT_REGION");
    _dupenv_s( &key_id, &len, "WINCSE_AWS_ACCESS_KEY_ID");
    _dupenv_s( &secret, &len, "WINCSE_AWS_SECRET_ACCESS_KEY");
    _dupenv_s( &bucket, &len, "WINCSE_BUCKET_NAME");

    assert(region && key_id && secret && bucket);

    std::cout << region << std::endl;
    std::cout << key_id << std::endl;
    std::cout << secret << std::endl;
    std::cout << bucket << std::endl;

    Aws::Client::ClientConfiguration config;
    config.region = region;

    Aws::String access_key{ key_id };
    Aws::String secret_key{ secret };

    Aws::Auth::AWSCredentials credentials{ access_key, secret_key };
    auto client = new Aws::S3::S3Client(credentials, nullptr, config);

    auto test = client->ListBuckets();
    if (test.IsSuccess())
    {
        Aws::S3::Model::ListObjectsV2Request request;

        request.SetBucket(bucket);
        //request.SetPrefix("新しいフォルダー/");
        request.SetPrefix("テスト/");
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
    else
    {
        std::cerr << "Error: listObjects: " << test.GetError().GetMessage() << std::endl;
    }

    free(region);
    free(key_id);
    free(secret);

    return 0;
}

int main()
{
    test1();

    return 0;
}

// EOF