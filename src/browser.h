#pragma once
#include <cstdint>
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

	// A storage place's name by where BananaOS mounts it and the kind of device under it, as the kernel names it
	// ("SD": a card, "MMC": built in, "": not known). /data is the system's own storage: the first card when the
	// system runs from one, else the handheld's own; /run/sd2 is the second slot.
	std::string storageLabel(const std::string& mount, const std::string& deviceType);

	// What a storage place's row says of its room: "21 GB free", "43 GB used of 64 GB", the share used (the bar),
	// and `full` when little is left (under a tenth, or under 1 GB): the bar and the free line turn DANGER.
	// known false (the rest empty): nothing could be read (total 0).
	struct Capacity
	{
		bool known = false, full = false;
		double used = 0;	// 0..1
		std::string free, detail;
	};
	Capacity capacityOf(uint64_t freeBytes, uint64_t totalBytes);
	// a card's size as its box says it: "64 GB", "7.4 GB", "512 MB" (1000s; one decimal under 10)
	std::string capacityWords(uint64_t bytes);

	// where the browser starts: the handheld's folders, or a computer's home; BP_BROWSE (folders separated by ':')
	// settles it (BP_BROWSE_CARDS=N: the first N of them stand in for storages). `card` says a place is a storage of its
	// own (the browser's summaries count them; Files and the dashboard show its room), not just a folder
	struct BrowseRoot
	{
		std::string label, path;
		bool card = false;
	};
	std::vector<BrowseRoot> browseRoots();
	// a place that is a folder, by the storage it is on: "SD card 1 / roms / videos"; on none of them (a
	// computer's folder): its path
	std::string placeWhere(const std::vector<BrowseRoot>& roots, const BrowseRoot& place);
}
