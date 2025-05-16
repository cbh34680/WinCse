// test-oci-os.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include <iostream>
#include <cassert>

#pragma comment(lib, "aws-cpp-sdk-core.lib")
#pragma comment(lib, "aws-cpp-sdk-s3.lib")

int listBuckets_main(int argc, char **argv);
int listObjects_main(int argc, char **argv);
int putObject_main(int argc, char **argv);

int main(int argc, char **argv)
{
    //
    // https://github.com/tonymarkel/OCI_AWS_CPP_SDK_S3_Examples
    //
    /*
        [Example]
        export AWS_ACCESS_KEY_ID="da34baaa4ab029f51c34c1cee83d40f0dEXAMPLE"
        export AWS_SECRET_ACCESS_KEY="7w3uMS6kYiYkUpziSlLFcBimBsYDJfojwCWKEXAMPLE="
        export OCI_REGION="us-ashburn-1"
        export OCI_NAMESPACE="jfie8fhiwd"
        export OCI_BUCKET="Images"
        export OCI_PREFIX="2024/12/18/Camera"

        [Run the examples]
        ./listBuckets $OCI_NAMESPACE $OCI_REGION $AWS_ACCESS_KEY_ID $AWS_SECRET_ACCESS_KEY
        ./listObjects $OCI_NAMESPACE $OCI_REGION $AWS_ACCESS_KEY_ID $AWS_SECRET_ACCESS_KEY $OCI_BUCKET $OCI_PREFIX
    */
    char *ns;
    char *region;
    char *key_id;
    char *secret;
    char *bucket;
    char prefix[] = "";
    size_t len;

    _dupenv_s(&ns,     &len, "WINCSE_TEST_OCI_NAMESPACE");
    _dupenv_s(&region, &len, "WINCSE_TEST_OCI_REGION");
    _dupenv_s(&key_id, &len, "WINCSE_TEST_OCI_ACCESS_KEY_ID");
    _dupenv_s(&secret, &len, "WINCSE_TEST_OCI_SECRET_ACCESS_KEY");
    _dupenv_s(&bucket, &len, "WINCSE_TEST_OCI_BUCKET");

    assert(ns && region && key_id && secret && bucket);

    //std::cout << ns << std::endl;
    //std::cout << region << std::endl;
    //std::cout << key_id << std::endl;
    //std::cout << secret << std::endl;
    //std::cout << bucket << std::endl;
    //std::cout << prefix << std::endl;

    char* paramsBuckets[] = { argv[0], ns, region, key_id, secret };
    char* paramsObjects[] = { argv[0], ns, region, key_id, secret, bucket, prefix };

    //listBuckets_main(_countof(paramsBuckets), paramsBuckets);
    //listObjects_main(_countof(paramsObjects), paramsObjects);
    putObject_main(_countof(paramsObjects), paramsObjects);

    free(ns);
    free(region);
    free(key_id);
    free(secret);
    free(bucket);
}

// EOF