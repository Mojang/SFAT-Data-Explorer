/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <utility>

#if (defined(TEST_INFRASTRUCTURE_DISABLED) || defined(MCPE_PROFILE)) && !defined(FORCE_LOGS_IN_PUBLISH)
#	define ENABLE_SPLITFAT_LOGGER	0
#else
#	define ENABLE_SPLITFAT_LOGGER	1
#endif

namespace SFAT {

	enum class LogArea
	{
		LA_PHYSICAL_DISK,
		LA_FAT_READ,
		LA_FAT_WRITE,
		LA_VOLUME_MANAGER,
		LA_VIRTUAL_DISK,
		LA_LARGE_WRITES,
		LA_TRANSACTION,
		LA_BLOCK_VIRTUALIZATION,

		LA_EXTERNAL_AREA_FILE,
		LA_EXTERNAL_AREA_PLATFORM,

		LA_AREAS_COUNT
	};

	enum class EventType {
		ET_ERROR,
		ET_WARNING,
		ET_INFO,
		ET_COUNT
	};

	void LOGGER(EventType eventType, LogArea logArea, const char *szFormat, ...);

#if	(ENABLE_SPLITFAT_LOGGER == 1)
	template <typename... Arg>
	void SFAT_LOGE(LogArea logArea, const char *szFormat, Arg... arg) {
		LOGGER(EventType::ET_ERROR, logArea, szFormat, std::forward<Arg>(arg)...);
	}

	template <typename... Arg>
	void SFAT_LOGW(LogArea logArea, const char *szFormat, Arg... arg) {
		LOGGER(EventType::ET_WARNING, logArea, szFormat, std::forward<Arg>(arg)...);
	}

	template <typename... Arg>
	void SFAT_LOGI(LogArea logArea, const char *szFormat, Arg... arg) {
		LOGGER(EventType::ET_INFO, logArea, szFormat, std::forward<Arg>(arg)...);
	}
#else 
	template <typename... Arg>
	void SFAT_LOGE(LogArea logArea, const char *szFormat, Arg... arg) {
		(void)logArea;
		(void)szFormat;
	}

	template <typename... Arg>
	void SFAT_LOGW(LogArea logArea, const char *szFormat, Arg... arg) {
		(void)logArea;
		(void)szFormat;
	}

	template <typename... Arg>
	void SFAT_LOGI(LogArea logArea, const char *szFormat, Arg... arg) {
		(void)logArea;
		(void)szFormat;
	}
#endif
} // namespace SFAT
