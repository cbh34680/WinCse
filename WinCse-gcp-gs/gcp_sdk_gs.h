#pragma once
//
// GCP SDK �֘A
//

#include "internal_undef_alloc.h"

// "operator new()" ����`����Ă��邽��

#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include "internal_define_alloc.h"

// EOF