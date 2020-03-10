/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#ifdef _SFAT_USE_MC_CORE_UTILS
#	define SPLIT_FAT_ENABLE_BEDROCK_LOGGING_IMPLEMENTATION 1
#endif

#if (SPLIT_FAT_ENABLE_BEDROCK_LOGGING_IMPLEMENTATION == 1)
#	include "Core/Debug/DebugUtils.h"
#	define SFAT_ASSERT(condition, msg) DEBUG_ASSERT((condition), (msg))
#else
#	include <assert.h>
#	define SFAT_ASSERT(condition, msg) { assert((condition) && (msg)); }
#endif



