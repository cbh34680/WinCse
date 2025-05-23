#pragma once
//
// GCP SDK ŠÖ˜A
//

#include "internal_undef_alloc.h"

// "operator new()" ‚ª’è‹`‚³‚ê‚Ä‚¢‚é‚½‚ß

#pragma warning(push, 0)
#include "google/cloud/storage/client.h"
#pragma warning(pop)

#include "internal_define_alloc.h"

// EOF