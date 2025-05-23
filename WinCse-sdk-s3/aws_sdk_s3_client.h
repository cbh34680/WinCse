#pragma once
//
// AWS SDK ŠÖ˜A
//

#ifdef USE_IMPORT_EXPORT
#error "USE_IMPORT_EXPORT already defined"
#endif

#include "internal_undef_alloc.h"

// https://github.com/aws/aws-sdk-cpp/issues/3209
#define USE_IMPORT_EXPORT
//#define USE_WINDOWS_DLL_SEMANTICS

#pragma warning(push, 0)
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/s3/S3Client.h>
#pragma warning(pop)

#undef USE_IMPORT_EXPORT

#include "internal_define_alloc.h"

// EOF