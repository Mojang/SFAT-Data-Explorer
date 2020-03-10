#include "SFATDataExplorerUI.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#	define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui_internal.h"

#include "SplitFAT/SplitFATFileSystem.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "BerwickSplitFATConfiguration.h"
#include "SplitFAT/utils/PathString.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include <stack>

using namespace ImGui;
using namespace SFAT;
using namespace Core::SFAT;

#define TEST_STORAGE_PATH	"E:/BerwickTest/CUSA00744"

namespace {
	const size_t kMAX_DIRECTORY_DEPTH = 32;
	const size_t kINVALID_ITEM_ID = static_cast<size_t>(-1);
}

SFATDataExplorerUI::SFATDataExplorerUI()
	: mIsOpen(true)
	, mRequestFileDialog(false)
	, mSelectedDirectoryID(kINVALID_ITEM_ID)
	, mDisplayedDirectoryID(kINVALID_ITEM_ID)
	, mSelectedItemID(kINVALID_ITEM_ID)
	, mDownloadStoragePath(TEST_STORAGE_PATH) {
}

SFATDataExplorerUI::~SFATDataExplorerUI() {
}

void SFATDataExplorerUI::showWindow()
{
	// Demonstrate the various window flags. Typically you would just use the default!
	static bool no_titlebar = false;
	static bool no_scrollbar = false;
	static bool no_menu = false;
	static bool no_move = false;
	static bool no_resize = false;
	static bool no_collapse = false;
	static bool no_close = false;
	static bool no_nav = false;
	static bool no_background = false;
	static bool no_bring_to_front = false;

	ImGuiWindowFlags window_flags = 0;
	if (no_titlebar)        window_flags |= ImGuiWindowFlags_NoTitleBar;
	if (no_scrollbar)       window_flags |= ImGuiWindowFlags_NoScrollbar;
	if (!no_menu)           window_flags |= ImGuiWindowFlags_MenuBar;
	if (no_move)            window_flags |= ImGuiWindowFlags_NoMove;
	if (no_resize)          window_flags |= ImGuiWindowFlags_NoResize;
	if (no_collapse)        window_flags |= ImGuiWindowFlags_NoCollapse;
	if (no_nav)             window_flags |= ImGuiWindowFlags_NoNav;
	if (no_background)      window_flags |= ImGuiWindowFlags_NoBackground;
	if (no_bring_to_front)  window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
	//if (no_close)           p_open = NULL; // Don't pass our bool* to Begin

	// We specify a default position/size in case there's no data in the .ini file. Typically this isn't required! We only do it to make the Demo applications a little more welcoming.
	ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiCond_FirstUseEver);

	// Main body of the Demo window starts here.
	if (!ImGui::Begin("Directory Tree", &mIsOpen, window_flags)) {
		// Early out if the window is collapsed, as an optimization.
		ImGui::End();
		return;
	}

	//Show the main menu
	{
		showMenu();
	}

	if (mRequestFileDialog) {
		auto newPath = mWindowsDialogs.BrowseFolder("Select PS4 Storage Directory", mDownloadStoragePath);
		if (!newPath.empty()) {
			if (OpenSFATStorage()) {
				mDownloadStoragePath = newPath;
				IterateThroughSFATDirectories();
			}
		}
		//openFileDialog();
		mRequestFileDialog = false;
	}

	//Modal dialogs here
	mFileSelectDialog.DialogRendering();

	//Show the both panels with splitter in between.
	{
		//if (ImGui::Button("Refresh", ImVec2(200, 0))) {
		//	IterateThroughSFATDirectories();
		//}
		std::string selectedDirectoryPath;
		if (mSelectedDirectoryID != kINVALID_ITEM_ID) {
			selectedDirectoryPath = mAllDirectories[mSelectedDirectoryID].mFullPath;
		}
		ImGui::Text("\nSelected Path: %s\n", selectedDirectoryPath.c_str());

		float h = -1;
		static float sz1 = 400;
		static float sz2 = 800;
		Splitter(true, 8.0f, &sz1, &sz2, 8, 8, h);
		ImGui::BeginChild("1", ImVec2(sz1, h), true);
		ShowDirectoryTree();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("2", ImVec2(sz2, h), true);
		showDirectoryItems();
		ImGui::EndChild();
	}

	ImGui::End();
}

void SFATDataExplorerUI::showMenu()
{
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			showFileMenu();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
			if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled item
			ImGui::Separator();
			if (ImGui::MenuItem("Cut", "CTRL+X")) {}
			if (ImGui::MenuItem("Copy", "CTRL+C")) {}
			if (ImGui::MenuItem("Paste", "CTRL+V")) {}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

void SFATDataExplorerUI::showFileMenu() {
	if (ImGui::MenuItem("New")) {}

	if (ImGui::MenuItem("Open", "Ctrl+O")) {
		mRequestFileDialog = true;
	}

	if (ImGui::BeginMenu("Open Recent")) {
		// List 
		//ImGui::MenuItem("fish_hat.c");
		//ImGui::MenuItem("fish_hat.inl");
		//ImGui::MenuItem("fish_hat.h");
		//if (ImGui::BeginMenu("More.."))
		//{
		//	ImGui::MenuItem("Hello");
		//	ImGui::MenuItem("Sailor");
		//	if (ImGui::BeginMenu("Recurse.."))
		//	{
		//		showFileMenu();
		//		ImGui::EndMenu();
		//	}
		//	ImGui::EndMenu();
		//}
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Save", "Ctrl+S")) {}
	if (ImGui::MenuItem("Save As..")) {}
	ImGui::Separator();
	if (ImGui::BeginMenu("Options")) {
		static bool enabled = true;
		ImGui::Checkbox("Read-only mode", &enabled);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Quit", "Alt+F4")) {}
}


bool SFATDataExplorerUI::Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size /*= -1.0f*/) {
	ImGuiContext& g = *GImGui;
	ImGuiWindow* window = g.CurrentWindow;
	ImGuiID id = window->GetID("##Splitter");
	ImRect bb;
	bb.Min = window->DC.CursorPos + (split_vertically ? ::ImVec2(*size1, 0.0f) : ::ImVec2(0.0f, *size1));
	bb.Max = bb.Min + CalcItemSize(split_vertically ? ::ImVec2(thickness, splitter_long_axis_size) : ::ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
	return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
	return false;
}

size_t SFATDataExplorerUI::showDirectoryRecursive(size_t startIndex, size_t& nodeClicked, size_t selectedID) {

	static const ImGuiTreeNodeFlags kBaseFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

	size_t countDirectories = mAllDirectories.size();
	if (startIndex >= countDirectories) {
		return kINVALID_ITEM_ID;
	}

	DirectoryTreeNode& node = mAllDirectories[startIndex];
	// Disable the default open on single-click behavior and pass in Selected flag according to our selection state.
	ImGuiTreeNodeFlags nodeFlags = kBaseFlags;
	const bool isSelected = (selectedID == startIndex);
	if (isSelected) {
		nodeFlags |= ImGuiTreeNodeFlags_Selected;
	}

	size_t nextNodeIndex = startIndex + 1;
	if (node.mChildrenCount >= 0) {
		bool isNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)startIndex, nodeFlags, node.mDirectoryName.c_str()/*, i*/);
		if (ImGui::IsItemClicked()) {
			nodeClicked = startIndex;
		}
		node.mIsNodeOpen = isNodeOpen;
		if (isNodeOpen) {
			while (nextNodeIndex < countDirectories) {
				DirectoryTreeNode& childNode = mAllDirectories[nextNodeIndex];
				if (childNode.mParentID != startIndex) {
					break;
				}
				nextNodeIndex = showDirectoryRecursive(nextNodeIndex, nodeClicked, selectedID);
			}
			ImGui::TreePop();
		}
		else {
			// Skip all children of the current node
			while (nextNodeIndex < countDirectories) {
				DirectoryTreeNode& nextNode = mAllDirectories[nextNodeIndex];
				if (nextNode.mDepth <= node.mDepth) {
					break;
				}
				++nextNodeIndex;
			}
		}
	}
	else
	{
		nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
		ImGui::TreeNodeEx((void*)(intptr_t)startIndex, nodeFlags, node.mDirectoryName.c_str(), startIndex);
		if (ImGui::IsItemClicked()) {
			nodeClicked = startIndex;
		}
	}

	return nextNodeIndex;
}

void SFATDataExplorerUI::ShowDirectoryTree() {
	size_t nodeClicked = kINVALID_ITEM_ID;
	showDirectoryRecursive(0, nodeClicked, mSelectedDirectoryID);

	if (nodeClicked != kINVALID_ITEM_ID)
	{
		// Update selection state. Process outside of tree loop to avoid visual inconsistencies during the clicking-frame.
		if (ImGui::GetIO().KeyCtrl) {
			// CTRL+click to toggle
			if (mSelectedDirectoryID == kINVALID_ITEM_ID) {
				mSelectedDirectoryID = nodeClicked;
			}
			else {
				mSelectedDirectoryID = kINVALID_ITEM_ID;
			}
		}
		else {
			mSelectedDirectoryID = nodeClicked;           // Click to single-select
		}
	}
}

bool SFATDataExplorerUI::OpenSFATStorage() {
	mFileStorage = std::make_shared<SplitFATFileStorage>();
	std::shared_ptr<BerwickSplitFATConfiguration> lowLevelFileAccess = std::make_shared<BerwickSplitFATConfiguration>();
	ErrorCode err = lowLevelFileAccess->setup(mDownloadStoragePath);
	SFAT_ASSERT(err == ErrorCode::RESULT_OK, "The SFAT low level setup failed!");
	err = mFileStorage->setup(lowLevelFileAccess);
	if (err != ErrorCode::RESULT_OK) {
		mFileStorage = nullptr;
		return false;
	}
	SFAT_ASSERT(err == ErrorCode::RESULT_OK, "The SFAT file storage setup failed!");
	
	return (err == ErrorCode::RESULT_OK);
}


bool SFATDataExplorerUI::IterateThroughSFATDirectories() {
	if (!mFileStorage) {
		return false;
	}

	mAllDirectories.clear();

	//printf("\n\n\n\n");
	size_t recordIndex = 0;
	int depth = 0;
	std::vector<DirectoryTreeNode> stack(kMAX_DIRECTORY_DEPTH);
	//stack[0] = std::move(DirectoryTreeNode(0, kINVALID_ITEM_ID, 0, "/", "/"));
	mAllDirectories.emplace_back(0, kINVALID_ITEM_ID, 0, "/", "/");
	stack[0] = mAllDirectories.back();
	mFileStorage->iterateThroughDirectory("/", DI_DIRECTORY | DI_RECURSIVE, [this, &recordIndex, &stack](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
		if (record.isDirectory()) {
			int currentDepth = std::count(fullPath.begin(), fullPath.end(), '/');
			++recordIndex;

			SFAT_ASSERT(currentDepth > 0, "The depth should be always positive!");
			int parentIndex = stack[currentDepth - 1].mID;
			stack[currentDepth - 1].mChildrenCount++;
			mAllDirectories.emplace_back(recordIndex, parentIndex, currentDepth, record.mEntityName, fullPath);
			stack[currentDepth] = mAllDirectories.back();

			//printf("ID: %d\tParent ID: %d, Depth: %d\tName:%s\n", recordIndex, parentIndex, currentDepth, fullPath.c_str());
		}
		return ErrorCode::RESULT_OK;
	});

	return true;
}

bool SFATDataExplorerUI::IterateThroughSFATDirectyItems() {
	if (!mFileStorage) {
		return false;
	}

	if (mSelectedDirectoryID != mDisplayedDirectoryID) {
		mDirectoryItems.clear();
		mDisplayedDirectoryID = mSelectedDirectoryID;
		mSelectedItemID = kINVALID_ITEM_ID;

		if (mDisplayedDirectoryID != kINVALID_ITEM_ID) {
			mCurrentDisplayedDirectory = mAllDirectories[mSelectedDirectoryID].mFullPath;

			mFileStorage = std::make_shared<SplitFATFileStorage>();
			std::shared_ptr<BerwickSplitFATConfiguration> lowLevelFileAccess = std::make_shared<BerwickSplitFATConfiguration>();
			ErrorCode err = lowLevelFileAccess->setup(TEST_STORAGE_PATH);
			SFAT_ASSERT(err == ErrorCode::RESULT_OK, "The SFAT low level setup failed!");
			err = mFileStorage->setup(lowLevelFileAccess);
			if (err != ErrorCode::RESULT_OK) {
				return false;
			}
			SFAT_ASSERT(err == ErrorCode::RESULT_OK, "The SFAT file storage setup failed!");
			if (err != ErrorCode::RESULT_OK) {
				return false;
			}

			size_t recordIndex = 0;
			int depth = 0;
			mFileStorage->iterateThroughDirectory(mCurrentDisplayedDirectory.c_str(), DI_DIRECTORY | DI_FILE, [this](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {

				mDirectoryItems.emplace_back(record.mEntityName, record.mFileSize, record.isDirectory(), record.mStartCluster);
				return ErrorCode::RESULT_OK;
			});
		}
		else {
			mCurrentDisplayedDirectory.clear();
		}
	}

	return true;
}


void SFATDataExplorerUI::showDirectoryItems() {
	IterateThroughSFATDirectyItems();

	static const bool h_borders = true;
	static const bool v_borders = true;
	ImGui::Columns(4, NULL, v_borders);
	size_t directoryItemsCount = mDirectoryItems.size();
	//ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor(0.3f, 3.0f, 8.0f, 1.0f));
	//ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(0.0f, 0.0f, 0.0f, 1.0f));
	ImGui::TextUnformatted("Type");
	ImGui::NextColumn();
	ImGui::TextUnformatted("Name");
	ImGui::NextColumn();
	ImGui::TextUnformatted("Size");
	ImGui::NextColumn();
	ImGui::TextUnformatted("First Cluster");
	ImGui::NextColumn();
	ImGui::Separator();
	//ImGui::PopStyleColor(1);

	for (size_t i = 0; i < directoryItemsCount; i++) {
		auto& item = mDirectoryItems[i];
		if (h_borders && ImGui::GetColumnIndex() == 0)
			ImGui::Separator();
		if (item.mIsDirectory) {
			ImGui::TextUnformatted("<Dir>");
		}
		else {
			ImGui::TextUnformatted("<File>");
		}
		ImGui::NextColumn();
		if (ImGui::Selectable(item.mName.c_str(), mSelectedItemID == i)) {
			mSelectedItemID = i;
		}
		//ImGui::TextUnformatted(item.mName.c_str());
		ImGui::NextColumn();
		ImGui::Text("%d", item.mFileSize);
		ImGui::NextColumn();
		ImGui::Text("%08X", item.mStartCluster);
		ImGui::NextColumn();
	}
}


