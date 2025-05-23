#define USE_IMPORT_EXPORT

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <iostream>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <fstream>
#include <memory>
using namespace Aws;
using namespace Aws::Auth;

// usage listObjects namespace region accesskey secretkey bucket prefix

int putObject_main(int argc, char **argv) {
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

    // https://github.com/aws/aws-sdk-cpp/issues/3411#issuecomment-2891706597
    // https://github.com/aws/aws-sdk-cpp/issues/3253#issue-2791519562
    cfg.checksumConfig.requestChecksumCalculation = Client::RequestChecksumCalculation::WHEN_REQUIRED;

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
// PUT -->
        //
        // https://github.com/awsdocs/aws-doc-sdk-examples/blob/main/cpp/example_code/s3/put_object.cpp
        //
        const Aws::String bucketName{ argv[5] };
        const Aws::String fileName{ __FILE__ };

        Aws::S3::Model::PutObjectRequest request;
        request.WithBucket(bucketName).WithKey("putObject.cpp");

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
            fileName.c_str(),
            std::ios_base::in | std::ios_base::binary);

        if (*inputData)
        {
#define SET_CONTENT_LENGTH (0)

#if SET_CONTENT_LENGTH
            inputData->seekg(0, std::ios::end);
            const auto contentLength = inputData->tellg();
            inputData->seekg(0, std::ios::beg);

            request.SetContentLength(contentLength);
            std::cout << "SetContentLength(" << contentLength << ")" << std::endl;
#endif
            //request.SetChecksumAlgorithm(Aws::S3::Model::ChecksumAlgorithm::NOT_SET);
            request.SetBody(inputData);

            std::cout << "Try PutObject" << std::endl <<
                " [Key] " << request.GetKey() << std::endl <<
                " [Length] " << request.GetContentLength() << std::endl <<
                std::endl;

            Aws::S3::Model::PutObjectOutcome outcome =
                m_client.PutObject(request);

            if (!outcome.IsSuccess()) {
                const auto& err{ outcome.GetError() };

                std::cerr << 
                    "Error: putObject" << std::endl <<
                    " [Code] " << err.GetResponseCode() << std::endl <<
                    " [Type] " << static_cast<int>(err.GetErrorType()) << std::endl <<
                    " [Name] " << err.GetExceptionName() << std::endl <<
                    " [Text] " << err.GetMessage() << std::endl <<
                    std::endl;
            } else {
                std::cout << "Added object '" << fileName << "' to bucket '" << bucketName << "'.";
            }
        }
        else
        {
            std::cerr << "Error unable to read file " << fileName << std::endl;
        }
// PUT <--
    }
    Aws::ShutdownAPI(m_options); // Should only be called once.
    return result;
}
