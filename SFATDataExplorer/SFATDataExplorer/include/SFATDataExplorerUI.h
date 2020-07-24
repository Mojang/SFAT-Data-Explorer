#ifndef _SFAT_DATA_EXPLORER_H_2020_01_29_
#define _SFAT_DATA_EXPLORER_H_2020_01_29_

#include "WindowsDialogs.h"

#include "imgui.h"
#include <ctype.h>          // toupper, isprint
#include <limits.h>         // INT_MIN, INT_MAX
#include <math.h>           // sqrtf, powf, cosf, sinf, floorf, ceilf
#include <stdio.h>          // vsnprintf, sscanf, printf
#include <stdlib.h>         // NULL, malloc, free, atoi
#if defined(_MSC_VER) && _MSC_VER <= 1500 // MSVC 2008 or earlier
#include <stddef.h>         // intptr_t
#else
#include <stdint.h>         // intptr_t
#endif

#include <memory>
#include <vector>
#include <string>

// SFAT forward declarations
namespace SFAT {
	class SplitFATFileStorage;
}

class FileSelectDialog
{
public:

	FileSelectDialog()
	{
		m_dialogName = "File Select";
	}

	void Open()
	{
		//ImGuiContext& g = *GImGui;
		//ImGuiWindow* window = g.CurrentWindow;
		ImGui::OpenPopup(m_dialogName.c_str());
	}

	void DialogRendering()
	{
		if (ImGui::BeginPopupModal(m_dialogName.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("The current dialog should allow selection of SplitFAT storage.\n\n\n\n");
			ImGui::Separator();

			if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
			ImGui::EndPopup();
		}
	}
private:
	std::vector<std::string> m_fileItems;
	std::string m_dialogName;
};

struct DirectoryTreeNode {
	DirectoryTreeNode() = default;
	DirectoryTreeNode(size_t id, size_t parentID, int depth, std::string name, std::string fullPath)
		: mID(id)
		, mParentID(parentID)
		, mDepth(depth)
		, mChildrenCount(0)
		, mDirectoryName(std::move(name))
		, mFullPath(std::move(fullPath))
		, mIsNodeOpen(false) {
	}

	size_t mID;
	size_t mParentID;
	int mDepth; // 0 is Root
	int mChildrenCount;
	std::string mDirectoryName;
	std::string mFullPath;
	bool mIsNodeOpen;
};

struct ItemData {
	ItemData(std::string name, size_t fileSize, bool isDirectory, uint64_t startCluster)
		: mName(std::move(name))
		, mFileSize(fileSize)
		, mIsDirectory(isDirectory)
		, mStartCluster(startCluster) {

	}

	std::string mName;
	size_t mFileSize;
	bool mIsDirectory;
	uint64_t mStartCluster;
};

class SFATDataExplorerUI
{
public:
	SFATDataExplorerUI();
	~SFATDataExplorerUI();

	bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f);
	void ShowDirectoryTree();
	bool OpenSFATStorage(const std::string& downloadStoragePath);
	bool IterateThroughSFATDirectories();
	bool IterateThroughSFATDirectyItems();

	void showWindow();

	void showDirectoryItems();

	void showMenu();

	void showFileMenu();

	size_t showDirectoryRecursive(size_t startIndex, size_t& nodeClicked, size_t selectedID = static_cast<size_t>(-1));

private:
	FileSelectDialog	mFileSelectDialog;
	bool				mIsOpen;
	bool				mRequestFileDialog;
	size_t				mSelectedDirectoryID;
	size_t				mDisplayedDirectoryID;
	size_t				mSelectedItemID;

	std::shared_ptr<SFAT::SplitFATFileStorage> mFileStorage;
	std::vector<DirectoryTreeNode> mAllDirectories;
	std::vector<ItemData> mDirectoryItems;
	std::string mCurrentDisplayedDirectory;

	WindowsDialogs mWindowsDialogs;
	std::string mDownloadStoragePath;

};

#endif //_SFAT_DATA_EXPLORER_H_2020_01_29_
