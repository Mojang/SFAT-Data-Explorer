#pragma once

#include <windows.h>
#include <string>

class WindowsDialogs {
public:
	std::string BrowseFolder(const std::string& title, const std::string& saved_path);
	void openFileDialog();
private:
	static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);

};