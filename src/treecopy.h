// Copying files and folders between the handheld's cards (the web dashboard's Copy and Move): measured first, copied
// out of sight, put in place last, so a failure or a cancel never leaves half a copy behind (bpcore, no window).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace bp
{
	struct TreeInfo
	{
		uint64_t bytes = 0, files = 0, biggest = 0;
		std::string bad;	// what stopped the walk, relative to its top ("" when it was the top itself)
	};

	// a file's or folder's size, every file in it counted (hidden ones too). false: something in it is neither a
	// plain file nor a folder (a link, a device: a copy can't keep it, a move would lose it), a folder `refuse` turns
	// down (given its real path: a folder the dashboard never serves), or it can't be read; info.bad names it
	bool scanTree(const std::string& path, TreeInfo& info, const std::function<bool(const std::string&)>& refuse = {});

	// after each piece written: the bytes added and the file being copied (relative to the top). false cancels
	using CopyProgress = std::function<bool(uint64_t add, const std::string& rel)>;

	// `from` (a file or a folder) copied to `to`, which must not exist: into a hidden ".bp-copy-…" beside `to`, then
	// renamed onto `to` without replacing anything. Files keep their dates and are synced before the rename, so the
	// copy is on the card before a move removes the original. 0, or an errno (ECANCELED: cancelled; EEXIST: `to` is
	// taken); on any failure nothing is left behind
	int copyTree(const std::string& from, const std::string& to, const CopyProgress& progress = {});

	// a file, or a folder and everything in it (links removed, never followed)
	void removeTree(const std::string& path);

	// rename that fails with EEXIST instead of replacing `to`; both on one filesystem (EXDEV otherwise). 0 or an errno
	int renameNoReplace(const std::string& from, const std::string& to);

	// A folder of cached items (the manga reader's network chapters: one folder or archive each) held under a
	// budget: the least recently used (by mtime) go until what's left fits, never `keep` (the one open). Returns
	// the bytes left.
	uint64_t trimCacheFolder(const std::string& dir, uint64_t budget, const std::string& keep = "");
}
