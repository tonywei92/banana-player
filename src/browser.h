#pragma once
#include <string>
#include <vector>

namespace bp
{
	// What the browser lists: folders first, then every file (videos, songs, photos, documents and the rest), each in natural order; hidden (.) names and
	// everything the player can't open left out.
	struct BrowseEntry
	{
		std::string name, path;
		bool dir = false;
	};

	// ok false: the folder couldn't be read (a card taken out, no permission). A folder with a ".hidden" file in it is
	// left out too, unless showHidden
	std::vector<BrowseEntry> listFolder(const std::string& dir, bool& ok, bool showHidden = false);

	// a folder hidden by a ".hidden" file in it
	bool isHiddenFolder(const std::string& dir);
	// the path is in a hidden folder, at any depth
	bool insideHiddenFolder(const std::string& path);

	// "/data/roms/videos/Show" -> "/data/roms/videos"; "/" stays "/"
	std::string parentFolder(const std::string& path);

	// A file or folder moved (renamed, or moved to another folder or card) from `from` to `to`: p itself, or anything
	// inside it, where it is now. false (out untouched) when p is neither. ("/a/b" doesn't move "/a/bc".)
	bool movedPath(const std::string& p, const std::string& from, const std::string& to, std::string& out);

	// the path is on the second card (/run/sd2) and nothing is mounted there: the card is out, and its folder is an
	// empty one of the system's, where nothing may be written (it would be lost, and hidden under the card when it's back)
	bool cardOut(const std::string& path);

	// where the browser starts: the handheld's folders, or a computer's home; BP_BROWSE (folders separated by ':')
	// settles it
	struct BrowseRoot
	{
		std::string label, path;
	};
	std::vector<BrowseRoot> browseRoots();
}
