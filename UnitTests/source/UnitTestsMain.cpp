/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>

#ifdef _DEBUG
//#	pragma comment(lib, "gtest_maind.lib")
#	pragma comment(lib, "gtestd.lib")
#else
//#	pragma comment(lib, "gtest_main.lib")
#	pragma comment(lib, "gtest.lib")
#endif

int unitTestsMain(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);

	int res = RUN_ALL_TESTS();

	return res;
}