/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/FileDescriptorRecord.h"

#ifndef _WIN32
#include <strings.h>
#endif

namespace SFAT {

	bool FileDescriptorRecord::isSameName(const std::string& name) const {
#ifdef _WIN32
		return (_strnicmp(name.c_str(), mEntityName, sizeof(mEntityName)) == 0);
#else // Berwick
		return (strncasecmp(name.c_str(), mEntityName, sizeof(mEntityName)) == 0);
#endif
	}

} // namespace SFAT