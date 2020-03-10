/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <string>

namespace SFAT {

	class PathString {
	public:
		PathString() = default;
		PathString(const char *szPath);
		PathString(const std::string& path);
		PathString(std::string&& path);
		PathString(const PathString& path) = default;
		PathString(PathString&& path);

		PathString& operator=(const PathString& path) = default;
		PathString& operator=(PathString&& path) = default;
		PathString& operator=(const char *szPath);
		PathString& operator=(const std::string& path);
		PathString& operator=(std::string&& path);

		std::string getName() const;
		PathString getParentPath() const;
		size_t getLength() const;
		bool isEmpty() const;
		const std::string& getString() const;
		std::string&& getString();
		bool isRoot() const;

		/// Returns the first entity of the path
		std::string getFirstPathEntity();
		/// Returns the next entity and updates the position in the path.
		std::string getNextPathEntity();
		/// Returns entire path up to the last entity (including). Doesn't change the position in the path.
		std::string getCurrentPath() const;

		const char* c_str() const;

		//TODO: Make unit-tests for these
		static PathString combinePath(const PathString& path0, const PathString& path1);

	private:
		void _normalize();
		void _initialize();

		std::string mPath;
		static char mPreferedSeparator;
		size_t mPosition = 0; ///Used in the getFirstPathEntity(), getNextPathEntity()
	};

} // namespace SFAT
