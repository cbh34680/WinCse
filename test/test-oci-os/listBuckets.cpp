#define USE_IMPORT_EXPORT

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <iostream>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
using namespace Aws;
using namespace Aws::Auth;

// usage listBuckets namespace region accesskey secretkey

int listBuckets_main(int argc, char **argv) {
    if (argc != 5) {
        std::cout << "Usage: " << argv[0] << " namespace region accesskey secretkey" << std::endl;
        return -1;
    }
    Aws::SDKOptions m_options;
    Aws::InitAPI(m_options); // Should only be called once.
    Aws::Client::ClientConfiguration cfg;
    // first param is namespace, 2nd param is region 
    // S3 compatible URL is https://NAMESPACE.compat.objectstorage.REGION.oraclecloud.com/ 
    cfg.endpointOverride = std::string("https://") + argv[1] + ".compat.objectstorage." + argv[2] + ".oraclecloud.com/" ;
    std::cout << "S3 Endpooint is: " << cfg.endpointOverride << std::endl;
    cfg.scheme = Aws::Http::Scheme::HTTP;
    cfg.verifySSL = true;
    cfg.region = argv[2];
    // 3rd param is access key, 4th param is secret key
    Aws::S3::S3Client m_client(Aws::Auth::AWSCredentials(argv[3], argv[4]), cfg,
          Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);
    int result = 0;
    Aws::Client::ClientConfiguration clientConfig;
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