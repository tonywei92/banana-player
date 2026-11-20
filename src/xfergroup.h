// Copy / Move of several items to or from a server (Files' select bar): the one-at-a-time transfer (xfer.h's
// xferCopy) run once per item, with the progress carried across them. No network and no window in here, so the job's
// logic is tested in core-test with a stand-in for the one transfer.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	// The job's progress, written by a transfer and read by the Ui each pass. A group sets the two bases before each
	// item, so the numbers a transfer writes (its own total, its bytes so far) carry on from the items before it.
	struct XferProgress
	{
		std::atomic<uint64_t>* done = nullptr;
		std::atomic<uint64_t>* total = nullptr;
		const std::atomic<bool>* cancel = nullptr;
		std::function<void(const std::string&)> current;	// the file being written, from the top's folder
		uint64_t doneBase = 0, totalBase = 0;	// what the items before this one came to
	};

	struct XferGroupResult
	{
		std::vector<std::pair<size_t, std::string>> landed;	// each finished item (its index) and the name it got
		std::string error;	// "" all went; else why it stopped (the items before it stay)
		bool cancelled = false;
	};

	// the transfer of item i: given the name it asks for, "" when done (the name it got put back), else a sentence
	using XferOne = std::function<std::string(size_t i, std::string& name, XferProgress& pr)>;

	// Items 0..n-1 in turn. Each is whole or not at all (the transfer's own rule), so a stop at item k, by a failure
	// or a cancel, leaves 0..k-1 complete; the result says which. Progress is cumulative: the total grows as each
	// item is measured.
	XferGroupResult xferGroup(const std::vector<std::string>& names, XferProgress& pr, const XferOne& one);
}
