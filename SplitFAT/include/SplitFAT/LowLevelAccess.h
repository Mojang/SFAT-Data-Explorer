/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "Common.h"
#include <vector>
#include <functional>

namespace SFAT {

	using FATBlockTableType = std::vector<FATCellValueType>;
	using FATBlockCallbackType = std::function<ErrorCode(uint32_t blockIndex, FATBlockTableType& table, bool& wasChanged)>;

} // namespace SFAT
