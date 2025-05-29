#define USE_IMPORT_EXPORT

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/model/GetBucketLocationRequest.h>

#include <iostream>

int getBucketRegion_main(const char* endpoint, const char* region, const char* accessKey, const char* secretKey, const char* bucket) {

    Aws::SDKOptions options;
    Aws::InitAPI(options);
    Aws::Client::ClientConfiguration cfg;
    cfg.endpointOverride = endpoint;
    cfg.region = region;
    cfg.scheme = Aws::Http::Scheme::HTTP;
    cfg.verifySSL = true;

    Aws::Auth::AWSCredentials credentials{ accessKey, secretKey };
    auto client = Aws::S3::S3Client(credentials, cfg, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);

    int result = 0;

    Aws::S3::Model::GetBucketLocationRequest request;
    request.SetBucket(bucket);
    auto outcome = client.GetBucketLocation(request);

    if (!outcome.IsSuccess()) {
        std::cerr << "Failed with error: " << outcome.GetError() << std::endl;
        result = 1;

    } else {
        const auto location = outcome.GetResult().GetLocationConstraint();
        const int iLocation = static_cast<int>(location);
        std::cout << "iLocation=" << iLocation << std::endl;

        if (location == Aws::S3::Model::BucketLocationConstraint::NOT_SET)
        {
            std::cerr << "location: NOT_SET" << std::endl;
        }
        else
        {
            const auto region = Aws::S3::Model::BucketLocationConstraintMapper::GetNameForBucketLocationConstraint(location);

            std::cout << "region:" << region << std::endl;
        }
    }

    Aws::ShutdownAPI(options); // Should only be called once.
    return result;
}

// EOF