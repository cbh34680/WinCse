#define USE_IMPORT_EXPORT

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <iostream>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/model/PutObjectRequest.h>

#include <fstream>
#include <memory>
#include <filesystem>

int putObject_main(const char* endpoint, const char* region, const char* accessKey, const char* secretKey, const char* bucket, const char* prefix, const char* inputFile) {

    std::cout << "START FUNCTION: " << __FUNCTION__ << std::endl;

    Aws::SDKOptions options;
    Aws::InitAPI(options);
    Aws::Client::ClientConfiguration cfg;
    cfg.endpointOverride = endpoint;
    cfg.region = region;
    cfg.scheme = Aws::Http::Scheme::HTTP;
    cfg.verifySSL = true;

    Aws::Auth::AWSCredentials credentials{ accessKey, secretKey };
    auto m_client = Aws::S3::S3Client(credentials, cfg, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);

    int result = 0;

    const Aws::String fileName{ inputFile };

    if (!std::filesystem::exists(fileName))
    {
        std::cerr << fileName << ": file not found" << std::endl;
        return 1;
    }

    std::cout << "input file: " << fileName << std::endl << std::endl;

    Aws::S3::Model::PutObjectRequest request;
    request.WithBucket(bucket).WithKey(std::filesystem::path(fileName).filename().string());

    std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
        fileName.c_str(),
        std::ios_base::in | std::ios_base::binary);

    if (*inputData)
    {
        request.SetContentLength(std::filesystem::file_size(fileName));
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
            std::cout << "Added object '" << fileName << "' to bucket '" << bucket << "'." << std::endl;
        }
    }
    else
    {
        std::cerr << "Error unable to read file " << fileName << std::endl;
    }

    Aws::ShutdownAPI(options); // Should only be called once.

    return 0;
}
