#pragma once
//
// AWS SDK 関連
//

#include "internal_undef_alloc.h"

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
#include <aws/s3/model/ListBucketsRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#pragma warning(pop)

#undef USE_IMPORT_EXPORT

#include "internal_define_alloc.h"

//
// "Windows.h" で定義されている GetObject と aws-sdk-cpp のメソッド名が
// バッティングしてコンパイルできないのことを回避
//
#ifdef GetObject
#undef GetObject
#endif

#ifdef GetMessage
#undef GetMessage
#endif

// EOF