#pragma once
//
// AWS SDK ŠÖ˜A
//

#include "internal_undef_alloc.h"

#ifndef USE_IMPORT_EXPORT
// https://github.com/aws/aws-sdk-cpp/issues/3209
#define USE_IMPORT_EXPORT
//#define USE_WINDOWS_DLL_SEMANTICS
#endif

#pragma warning(push, 0)
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>

#include <aws/s3/model/CompletedPart.h>
#pragma warning(pop)

#undef USE_IMPORT_EXPORT

#include "internal_define_alloc.h"

// EOF