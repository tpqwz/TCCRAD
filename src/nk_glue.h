#ifndef TCCRAD_NK_GLUE_H
#define TCCRAD_NK_GLUE_H

/* Every translation unit that includes nuklear must use these same flags. */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#include "nuklear.h"

#endif
