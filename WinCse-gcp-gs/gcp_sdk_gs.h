#pragma once
//
// GCP SDK 関連
//

#include "internal_undef_alloc.h"

// "operator new()" が定義されているため

#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include "internal_define_alloc.h"

// EOF