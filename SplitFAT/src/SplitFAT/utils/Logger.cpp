/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/SFATAssert.h"
#include <string>
#include <stdarg.h>
#include <algorithm>

#if (SPLIT_FAT_ENABLE_BEDROCK_LOGGING_IMPLEMENTATION == 1)
#	include "Core/HeaderIncludes/Macros.h"
#	include "Core/Debug/DebugUtils.h"
#	include "Core/Debug/Log.h"
#endif

namespace SFAT {

	namespace {
		const int MAX_LOG_MESSAGE_SIZE = 256;

		const char *gAreaNames[static_cast<size_t>(LogArea::LA_AREAS_COUNT)] = {
			"Physical Disk Operations",
			"FAT read",
			"FAT write",
			"Volume Manager",
			"Virtual Disk Operations",
			"Large Writes",
			"Transaction",
			"Block Virtualization",
			"MC_File",
			"MC_Platform",
		};

		const char *gFormatStrings[static_cast<size_t>(EventType::ET_COUNT)] = {
	#if (SPLIT_FAT_ENABLE_BEDROCK_LOGGING_IMPLEMENTATION == 1)
			"SFAT(%s): %s\n",
			"SFAT(%s): %s\n",
			"SFAT(%s): %s\n",
	#else
			"Area: %s\t\tError: %s\n",
			"Area: %s\t\tWarning: %s\n",
			"Area: %s\t\tInfo: %s\n"
	#endif
		};
	}

	void LOGGER(EventType eventType, LogArea logArea, const char *szFormat, ...) {
		SFAT_ASSERT(szFormat != nullptr, "The format string can't be nullptr!");

		va_list vl;
		va_start(vl, szFormat);
		char buf[MAX_LOG_MESSAGE_SIZE + 1];
#if defined(_WIN32)
		vsnprintf_s(buf, MAX_LOG_MESSAGE_SIZE, _TRUNCATE, szFormat, vl);
#else
		vsnprintf(buf, MAX_LOG_MESSAGE_SIZE, szFormat, vl);
#endif
		va_end(vl);

#if (SPLIT_FAT_ENABLE_BEDROCK_LOGGING_IMPLEMENTATION == 1)
		switch (eventType) {
			case EventType::ET_ERROR: {
				ALOGE(LOG_AREA_FILE, gFormatStrings[static_cast<size_t>(eventType)], gAreaNames[static_cast<size_t>(logArea)], buf);
			} break;
			case EventType::ET_WARNING: {
				ALOGW(LOG_AREA_FILE, gFormatStrings[static_cast<size_t>(eventType)], gAreaNames[static_cast<size_t>(logArea)], buf);
			} break;
			case EventType::ET_INFO: {
				ALOGI(LOG_AREA_FILE, gFormatStrings[static_cast<size_t>(eventType)], gAreaNames[static_cast<size_t>(logArea)], buf);
			} break;
			default: {
			}
		}
#else
		printf(gFormatStrings[static_cast<size_t>(eventType)], gAreaNames[static_cast<size_t>(logArea)], buf);
#endif

	}

} // namespace SFAT
