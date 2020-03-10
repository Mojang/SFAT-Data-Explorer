/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/utils/PathString.h"
#include "SplitFAT/utils/SFATAssert.h"
#include <algorithm>

namespace SFAT {

	char PathString::mPreferedSeparator = '/';

	PathString::PathString(const char *szPath)
		: mPath(szPath) {
		_initialize();
	}

	PathString::PathString(const std::string& path)
		: mPath(path) {
		_initialize();
	}

	PathString::PathString(std::string&& path)
		: mPath(std::move(path)) {
		_initialize();
	}

	PathString::PathString(PathString&& path)
		: mPath(std::move(path.mPath))
		, mPosition(path.mPosition) {
	}

	PathString& PathString::operator=(const char *szPath) {
		mPath = szPath;
		_initialize();
		return *this;
	}

	PathString& PathString::operator=(const std::string& path) {
		mPath = path;
		_initialize();
		return *this;
	}

	//PathString& PathString::operator=(PathString&& path) {
	//	mPath = std::move(path);
	//	_normalize();
	//	return *this;
	//}

	PathString& PathString::operator=(std::string&& path) {
		mPath = std::move(path);
		_initialize();
		return *this;
	}

	void PathString::_normalize() {
		//TODO: Implement removal of the invalid characters - "?*\n\r\t,:'\"|"

		size_t size = mPath.size();
		bool onSlashSymbol = false;
		size_t newStringEnd = 0;
		for (size_t i = 0; i < size; ++i) {
			char ch = mPath[i];
			if ((ch == '\\') || (ch == '/')) {
				if (onSlashSymbol) {
					//We were already on either '/' or  '\', so skipping any repeating slash symbols
					continue;
				}
				onSlashSymbol = true;
				mPath[newStringEnd] = mPreferedSeparator;
			}
			else {
				onSlashSymbol = false;
				//mPath[newStringEnd] = ::tolower(ch);
				mPath[newStringEnd] = ch;
			}

			++newStringEnd;
		}
		if (onSlashSymbol && (newStringEnd > 1)) {
			/// Remove the last slash if the path is longer than one symbol
			/// /<Name0> --> /<Name>
			/// /<Name>/ --> /<Name>
			/// <Name>/ --> <Name>
			/// / --> /
			--newStringEnd;
		}
		mPath.resize(newStringEnd);
	}

	void PathString::_initialize() {
		_normalize();
		if (!mPath.empty() && (mPath[0] == mPreferedSeparator)) {
			mPosition = 1;
		}
	}

	std::string PathString::getName() const {
		SFAT_ASSERT(!mPath.empty(), "The path should not be empty!");
		
		size_t pos = mPath.rfind(mPreferedSeparator, mPath.size());
		if (pos != std::string::npos) {
			return mPath.substr(pos + 1, mPath.length() - pos);
		}

		return mPath;
	}

	PathString PathString::getParentPath() const {
		SFAT_ASSERT(!mPath.empty(), "The path should not be empty!");

		size_t pos = mPath.rfind(mPreferedSeparator, mPath.size());
		if (pos != std::string::npos) {
			return PathString(mPath.substr(0, pos));
		}

		return PathString();
	}

	size_t PathString::getLength() const {
		return mPath.size();
	}

	bool PathString::isEmpty() const {
		return mPath.empty();
	}

	bool PathString::isRoot() const {
		return (mPath.size() == 1) && (mPath[0] == mPreferedSeparator);
	}


	const std::string& PathString::getString() const {
		return mPath;
	}

	std::string&& PathString::getString() {
		mPosition = 0;
		return std::move(mPath);
	}

	std::string PathString::getFirstPathEntity() {
		SFAT_ASSERT(!mPath.empty(), "The path should not be empty!");

		mPosition = 0;
		if (!mPath.empty() && (mPath[0] == mPreferedSeparator)) {
			++mPosition;
		}

		return getNextPathEntity();
	}

	std::string PathString::getNextPathEntity() {
		SFAT_ASSERT(!mPath.empty(), "The path should not be empty!");

		size_t nextPos = mPath.find(mPreferedSeparator, mPosition);
		if (nextPos != std::string::npos) {
			size_t oldPos = mPosition;
			mPosition = nextPos + 1;
			return mPath.substr(oldPos, nextPos - oldPos);
		}

		size_t oldPos = mPosition;
		mPosition = mPath.size();
		return mPath.substr(oldPos);
	}

	std::string PathString::getCurrentPath() const {
		if (mPosition == mPath.size()) {
			return mPath.substr(0, mPosition);
		}
		return mPath.substr(0, mPosition - 1);
	}

	const char* PathString::c_str() const {
		return mPath.c_str();
	}

	PathString PathString::combinePath(const PathString& path0, const PathString& path1) {
		std::string path;
		if ((path0.mPath.length() > 0) && (path0.mPath.back() == mPreferedSeparator)) {
			path = path0.mPath + path1.mPath;
		}
		else if (path0.mPath.empty()) {
			path = path1.mPath;
		}
		else {
			path = path0.mPath + mPreferedSeparator + path1.mPath;
		}
		return PathString(std::move(path));
	}

} // namespace SFAT
