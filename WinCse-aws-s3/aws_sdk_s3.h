#pragma once
//
// AWS SDK 関連
//

#include "internal_undef_alloc.h"

#ifndef USE_IMPORT_EXPORT
// https://github.com/aws/aws-sdk-cpp/issues/3209
#define USE_IMPORT_EXPORT
//#define USE_WINDOWS_DLL_SEMANTICS
#endif

#pragma warning(push, 0)
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/auth/AWSCredentialsProvider.h>

#include <aws/s3/model/GetBucketLocationRequest.h>
#include <aws/s3/model/ListBucketsRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/DeleteObjectsRequest.h>

// Multipart upload
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/CompletedPart.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <aws/s3/model/AbortMultipartUploadRequest.h>
#include <aws/s3/model/CompletedMultipartUpload.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
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