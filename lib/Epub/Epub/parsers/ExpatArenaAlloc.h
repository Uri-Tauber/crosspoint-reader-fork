#pragma once

#include <expat.h>

#include "epub_arena.h"

static const XML_Memory_Handling_Suite expat_arena_mem_suite = {epub_arena_malloc, epub_arena_realloc, epub_arena_free};
