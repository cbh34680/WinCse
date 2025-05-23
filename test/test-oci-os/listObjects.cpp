#define USE_IMPORT_EXPORT

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <iostream>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
using namespace Aws;
using namespace Aws::Auth;

// usage listObjects namespace region accesskey secretkey bucket prefix

int listObjects_main(int argc, char **argv) {
    if (argc != 7) {
        std::cout << "Usage: " << argv[0] << " namespace region accesskey secretkey bucket prefix" << std::endl;
        return -1;
    }
    Aws::SDKOptions m_options;
    Aws::InitAPI(m_options); // Should only be called once.
    Aws::Client::ClientConfiguration cfg;
    // first param is namespace, 2nd param is region 
    // S3 compatible URL is https://NAMESPACE.compat.objectstorage.REGION.oraclecloud.com/ 
    cfg.endpointOverride = std::string("https://") + argv[1] + ".compat.objectstorage." + argv[2] + ".oraclecloud.com/" ;
    std::cout << "S3 Endpoint is: " << cfg.endpointOverride << std::endl;
    std::cout << "Bucket is:" << argv[5] << std::endl;
    std::cout << "Prefix is:" << argv[6] << std::endl;
    cfg.scheme = Aws::Http::Scheme::HTTP;
    cfg.verifySSL = true;
    cfg.region = argv[2];
    // 3rd param is access key, 4th param is secret key
    Aws::S3::S3Client m_client(Aws::Auth::AWSCredentials(argv[3], argv[4]), cfg,
          Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);
    int result = 0;
    {
        std::cout << "LISTING OBJECTS" << std::endl;
        Aws::Client::ClientConfiguration clientConfig;
        auto objects = m_client.ListObjectsV2(Aws::S3::Model::ListObjectsV2Request().WithBucket(argv[5]).WithPrefix(argv[6]));
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
