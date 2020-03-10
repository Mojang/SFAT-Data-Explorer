/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include <SplitFAT/utils/PathString.h>

using namespace SFAT;

TEST(PathStringTest, DefaultContructor) {
	PathString path;
	EXPECT_EQ(path.getLength(), 0);
	EXPECT_TRUE(path.isEmpty());
}

TEST(PathStringTest, Contructors) {
	// Constructor with zero ending char* buffer.
	{
		PathString path("directory");
		EXPECT_STRCASEEQ(path.getString().c_str(), "directory");
		EXPECT_FALSE(path.isEmpty());
	}

	// Constructor with std::string
	{
		std::string str("another_directory");
		PathString path(str);
		EXPECT_STRCASEEQ(path.getString().c_str(), str.c_str());
	}

	// Constructor with moved std::string
	{
		std::string str("another_directory");
		PathString path(std::move(str));
		EXPECT_TRUE(str.empty()); //The string should be moved
		EXPECT_STRCASEEQ(path.getString().c_str(), "another_directory");
	}

	// Constructor with PathString
	{
		PathString path0("different_one");
		PathString path1(path0);
		EXPECT_NE(path0.getString().c_str(), path1.getString().c_str()); //Different pointers
		EXPECT_STRCASEEQ(path0.getString().c_str(), path1.getString().c_str()); //Same content
	}

	// Constructor with moved PathString
	{
		PathString path0("yet_another_one");
		PathString path1(std::move(path0));
		EXPECT_TRUE(path0.getString().empty()); //The string should be moved
		EXPECT_STRCASEEQ(path1.getString().c_str(), "yet_another_one"); //Same content
	}

	{
		PathString path("root/file.txt");
		PathString parentPath(path.getParentPath());
		EXPECT_STRCASEEQ(path.c_str(), "root/file.txt"); //Still the same
		EXPECT_STRCASEEQ(parentPath.c_str(), "root"); //Still the same
	}

}

TEST(PathStringTest, Assignment) {
	// Assignment with zero ending char* buffer.
	{
		PathString path;
		path = "directory";
		EXPECT_STRCASEEQ(path.getString().c_str(), "directory");
	}

	// Assignment with std::string
	{
		std::string str("another_directory");
		PathString path;
		path = str;
		EXPECT_STRCASEEQ(path.getString().c_str(), str.c_str());
	}

	// Assignment with moved std::string
	{
		std::string str("another_directory");
		PathString path;
		path = std::move(str);
		EXPECT_TRUE(str.empty()); //The string should be moved
		EXPECT_STRCASEEQ(path.getString().c_str(), "another_directory");
	}

	// Assignment with PathString
	{
		PathString path0("different_one");
		PathString path1;
		path1 = path0;
		EXPECT_NE(path0.getString().c_str(), path1.getString().c_str()); //Different pointers
		EXPECT_STRCASEEQ(path0.getString().c_str(), path1.getString().c_str()); //Same content
	}

	// Assignment with moved PathString
	{
		PathString path0("yet_another_one");
		PathString path1;
		path1 = (std::move(path0));
		EXPECT_TRUE(path0.getString().empty()); //The string should be moved
		EXPECT_STRCASEEQ(path1.getString().c_str(), "yet_another_one"); //Same content
	}
}

TEST(PathStringTest, PathNormalization) {
	// to-lower-case test
	{
		PathString path("AbCd");
		EXPECT_STRCASEEQ(path.getString().c_str(), "abcd");
	}

	// Slash normalization
	{
		PathString path("\\AbCd");
		EXPECT_STRCASEEQ(path.getString().c_str(), "/abcd");
	}

	// Last slash removal
	{
		PathString path("\\AbCd/");
		EXPECT_STRCASEEQ(path.getString().c_str(), "/abcd");
	}

	// Last slash removal, but not if the path is only "/"
	{
		PathString path("/");
		EXPECT_STRCASEEQ(path.getString().c_str(), "/");
	}

	// Last slash removal, but not if the path is only "\\"
	{
		PathString path("\\");
		EXPECT_STRCASEEQ(path.getString().c_str(), "/");
	}

	// Remove duplicated slashes
	{
		PathString path("\\\\dir0\\\\dir1//dir2/\\dir3\\/dir4////////file");
		EXPECT_STRCASEEQ(path.getString().c_str(), "/dir0/dir1/dir2/dir3/dir4/file");
	}

	// Remove duplicated slashes
	{
		PathString path("\\\\");
		EXPECT_STRCASEEQ(path.getString().c_str(), "/");
	}

	// Remove duplicated slashes
	{
		PathString path("//");
		EXPECT_STRCASEEQ(path.getString().c_str(), "/");
	}
}

TEST(PathStringTest, GetEntiryName) {

	// Get the filename
	{
		PathString path("FileName.txt");
		EXPECT_STRCASEEQ(path.getName().c_str(), "filename.txt");
	}

	// Get the filename
	{
		PathString path("/FileName.txt");
		EXPECT_STRCASEEQ(path.getName().c_str(), "filename.txt");
	}

	// Get the filename
	{
		PathString path("someDirectory/FileName.txt");
		EXPECT_STRCASEEQ(path.getName().c_str(), "filename.txt");
	}

	// Get the filename
	{
		PathString path("/someDirectory/FileName.txt");
		EXPECT_STRCASEEQ(path.getName().c_str(), "filename.txt");
	}
}

TEST(PathStringTest, GetParentPath) {

	// Get the parent path
	{
		PathString path("directory");
		EXPECT_STRCASEEQ(path.getParentPath().c_str(), "");
	}

	// Get the parent path
	{
		PathString path("/FileName.txt");
		EXPECT_STRCASEEQ(path.getParentPath().c_str(), "");
	}

	// Get the parent path
	{
		PathString path("some_directory/FileName.txt");
		EXPECT_STRCASEEQ(path.getParentPath().c_str(), "some_directory");
	}

	// Get the parent path
	{
		PathString path("/some_directory/FileName.txt");
		EXPECT_STRCASEEQ(path.getParentPath().c_str(), "/some_directory");
	}

	// Get the parent path
	{
		PathString path("/dir0/dir1/dir2/FileName.txt");
		EXPECT_STRCASEEQ(path.getParentPath().c_str(), "/dir0/dir1/dir2");
	}
}

TEST(PathStringTest, PathStringIteration) {

	// Get the first entity
	{
		PathString path("/dir0/dir1/dir2/FileName.txt");
		std::string part = path.getFirstPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir0");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0");
	}

	// Get the first entity
	{
		PathString path("dir0/dir1/dir2/FileName.txt");
		std::string part = path.getFirstPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir0");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "dir0");
	}

	// Get the first and all next entities
	{
		PathString path("/dir0/dir1/dir2/FileName.txt");
		std::string part = path.getFirstPathEntity();
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir1");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0/dir1");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir2");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0/dir1/dir2");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "filename.txt");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0/dir1/dir2/filename.txt");
		part = path.getNextPathEntity();
		//Reached the end
		EXPECT_STRCASEEQ(part.c_str(), "");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0/dir1/dir2/filename.txt");
		//Should stay at the end
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0/dir1/dir2/filename.txt");
		//Return to the start
		part = path.getFirstPathEntity(); /// Start from the begining again
		EXPECT_STRCASEEQ(part.c_str(), "dir0");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "/dir0");
	}

	// Get the start with getting next entity
	{
		PathString path("dir0/dir1/FileName.txt");
		std::string part;
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir0");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "dir0");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir1");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "dir0/dir1");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "filename.txt");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "dir0/dir1/filename.txt");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "");
		EXPECT_STRCASEEQ(path.getCurrentPath().c_str(), "dir0/dir1/filename.txt");
	}

	// Get the start with getting next entity. The path starts with slash
	{
		PathString path("/dir0/dir1/FileName.txt");
		std::string part;
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir0");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "dir1");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "filename.txt");
		part = path.getNextPathEntity();
		EXPECT_STRCASEEQ(part.c_str(), "");
	}
}
