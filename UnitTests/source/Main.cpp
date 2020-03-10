/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "pch.h"
#include <iostream>

#include <SplitFAT/VolumeManager.h>
#include <SplitFAT/utils/SFATAssert.h>
#include "UnitTestsMain.h"

int main(int argc, char **argv) {
	int res = unitTestsMain(argc, argv);

	printf("Press Enter to quit\n");
	getchar();
}
