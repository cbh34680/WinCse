#define USE_IMPORT_EXPORT

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <iostream>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>

int listObjects_main(const char* endpoint, const char* region, const char* accessKey, const char* secretKey, const char* bucket, const char* prefix) {

    Aws::SDKOptions m_options;
    Aws::InitAPI(m_options);
    Aws::Client::ClientConfiguration cfg;
    cfg.endpointOverride = endpoint;
    cfg.region = region;
    cfg.scheme = Aws::Http::Scheme::HTTP;
    cfg.verifySSL = true;

    Aws::Auth::AWSCredentials credentials{ accessKey, secretKey };
    auto m_client = Aws::S3::S3Client(credentials, cfg, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);

    int result = 0;
    {
        std::cout << "LISTING OBJECTS" << std::endl;
        Aws::Client::ClientConfiguration clientConfig;
        auto objects = m_client.ListObjectsV2(Aws::S3::Model::ListObjectsV2Request().WithBucket(bucket).WithPrefix(prefix));
        if (!objects.IsSuccess()) {
            std::cerr << "Failed with error: " << objects.GetError() << std::endl;
            result = 1;
        } else {
            std::cout << "Found " << objects.GetResult().GetContents().size()
                << " objects\n";
            for (auto &object: objects.GetResult().GetContents()) {
                std::cout << object.GetKey() << std::endl;
            }
        }
    }
    Aws::ShutdownAPI(m_options); // Should only be called once.
    return result;
}
