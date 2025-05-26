#define USE_IMPORT_EXPORT

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <iostream>
#include <aws/core/auth/AWSCredentialsProviderChain.h>

int listBuckets_main(const char* endpoint, const char* region, const char* accessKey, const char* secretKey) {

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

    auto outcome = m_client.ListBuckets();

    if (!outcome.IsSuccess()) {
        std::cerr << "Failed with error: " << outcome.GetError() << std::endl;
        result = 1;
    } else {
        std::cout << "Found " << outcome.GetResult().GetBuckets().size()
            << " buckets\n";
        for (auto &bucket: outcome.GetResult().GetBuckets()) {
            std::cout << bucket.GetName() << std::endl;
        }
    }

    Aws::ShutdownAPI(m_options); // Should only be called once.
    return result;
}

// EOF