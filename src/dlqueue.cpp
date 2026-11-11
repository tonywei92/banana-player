#include "dlqueue.h"
#include "playlists.h"	// writeFileAtomic: the tmp-then-rename the other stores write with
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		const char* QUEUE_HEADER = "# banana-player manga downloads-queue v1";

		// mangastore's field escaping, again: its copies are private to its file and the queue must not depend on the
		// library's TSV to write its own. A field's tabs, newlines and backslashes, so a line stays a line and a field
		// stays a field. The carriage return goes too: a line ending in one would look like a field that ends in one.
		std::string escapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (char c : s)
			{
				if (c == '\t') o += "\\t";
				else if (c == '\n') o += "\\n";
				else if (c == '\r') o += "\\r";
				else if (c == '\\') o += "\\\\";
				else o += c;
			}
			return o;
		}

		std::string unescapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 >= s.size())
				{
					o += s[i];
					continue;
				}
				char n = s[++i];
				if (n == 't') o += '\t';
				else if (n == 'n') o += '\n';
				else if (n == 'r') o += '\r';
				else if (n == '\\') o += '\\';
				else
				{
					o += '\\';	// an escape we don't know stays as it was written
					o += n;
				}
			}
			return o;
		}

		std::vector<std::string> splitTabs(const std::string& line)
		{
			std::vector<std::string> f;
			size_t pos = 0;
			for (;;)
			{
				size_t t = line.find('\t', pos);
				if (t == std::string::npos)
				{
					f.push_back(line.substr(pos));
					break;
				}
				f.push_back(line.substr(pos, t - pos));
				pos = t + 1;
			}
			return f;
		}

		bool toInt(const std::string& s, int64_t& out)
		{
			if (s.empty()) return false;
			char* end = nullptr;
			long long v = strtoll(s.c_str(), &end, 10);
			if (!end || *end) return false;
			out = v;
			return true;
		}

		bool toUInt(const std::string& s, uint64_t& out)
		{
			if (s.empty()) return false;
			for (char c : s)
				if (c < '0' || c > '9') return false;
			char* end = nullptr;
			unsigned long long v = strtoull(s.c_str(), &end, 10);
			if (!end || *end) return false;
			out = v;
			return true;
		}

		bool readWhole(const std::string& path, std::string& out)
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return false;
			out.clear();
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
			fclose(f);
			return true;
		}

		// The state as the file writes it: a person can read the file, and one no version knows is a broken line.
		const char* stateName(DlItem::State s)
		{
			switch (s)
			{
			case DlItem::Waiting: return "waiting";
			case DlItem::Running: return "running";
			case DlItem::Paused: return "paused";
			case DlItem::Failed: return "failed";
			case DlItem::Done: return "done";
			}
			return "waiting";
		}

		bool parseState(const std::string& s, DlItem::State& out)
		{
			// running and waiting are one state on load: a quit mid-chapter resumes at the next page not written
			if (s == "waiting" || s == "running") out = DlItem::Waiting;
			else if (s == "paused") out = DlItem::Paused;
			else if (s == "failed") out = DlItem::Failed;
			else if (s == "done") out = DlItem::Done;
			else return false;
			return true;
		}

		// Each URL is escaped before the list's own newlines go between them, then the whole list is escaped again for
		// the line: a URL may itself hold a newline, and the two levels keep it apart from the separator.
		std::string encodePages(const std::vector<std::string>& urls)
		{
			std::string joined;
			for (size_t i = 0; i < urls.size(); i++)
			{
				if (i) joined += '\n';
				joined += escapeField(urls[i]);
			}
			return escapeField(joined);
		}

		void decodePages(const std::string& field, std::vector<std::string>& out)
		{
			std::string joined = unescapeField(field);
			out.clear();
			size_t pos = 0;
			for (;;)
			{
				size_t t = joined.find('\n', pos);
				if (t == std::string::npos)
				{
					out.push_back(unescapeField(joined.substr(pos)));
					break;
				}
				out.push_back(unescapeField(joined.substr(pos, t - pos)));
				pos = t + 1;
			}
			// an empty field is no pages, not one URL that is empty
			if (out.size() == 1 && out[0].empty()) out.clear();
		}
	}

	bool DownloadQueue::load(const std::string& stateDir)
	{
		items_.clear();
		// a low card and an all-pause are the worker's and this session's; the next start resumes (section 7)
		allPaused_ = false;
		lowSpace_ = false;
		dir_ = stateDir;
		if (dir_.empty()) return false;
		std::string text;
		if (readWhole(dir_ + "/manga/downloads-queue.tsv", text)) parse(text);
		serial_++;	// what the Downloads page shows is different now
		return true;
	}

	bool DownloadQueue::save() const
	{
		if (dir_.empty()) return false;
		std::string dir = dir_ + "/manga";
		mkdir(dir_.c_str(), 0755);
		mkdir(dir.c_str(), 0755);
		return writeFileAtomic(dir + "/downloads-queue.tsv", serialize());
	}

	void DownloadQueue::parse(const std::string& text)
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> f = splitTabs(line);
			// a later version may add columns after these; a line missing one of ours is broken
			if (f.size() < 13) continue;
			DlItem it;
			it.id = unescapeField(f[0]);
			it.seriesId = unescapeField(f[1]);
			it.seriesTitle = unescapeField(f[2]);
			it.chapterId = unescapeField(f[3]);
			it.chapterTitle = unescapeField(f[4]);
			it.source = unescapeField(f[5]);
			if (it.id.empty() || !parseState(f[6], it.state)) continue;
			int64_t done = 0, added = 0;
			uint64_t bytes = 0;
			if (!toInt(f[7], done) || !toUInt(f[8], bytes) || !toInt(f[9], added)) continue;
			it.error = unescapeField(f[10]);
			it.target = unescapeField(f[11]);
			decodePages(f[12], it.pageUrls);
			// pages past the end and chapters past either cap are what damage looks like, not a queue
			if (it.pageUrls.size() > MAX_PAGES) continue;
			if (done < 0 || done > int64_t(it.pageUrls.size())) continue;
			it.done = int(done);
			it.bytes = bytes;
			it.added = added;
			if (find(it.id)) continue;	// one chapter is one item, as add() keeps it
			if (items_.size() >= MAX_ITEMS) break;
			items_.push_back(it);
		}
	}

	std::string DownloadQueue::serialize() const
	{
		std::string out = QUEUE_HEADER;
		out += "\n";
		char buf[64];
		for (const auto& it : items_)
		{
			// a Done chapter is on the card now, not the queue's
			if (it.state == DlItem::Done) continue;
			out += escapeField(it.id) + "\t" + escapeField(it.seriesId) + "\t" + escapeField(it.seriesTitle) + "\t" +
				escapeField(it.chapterId) + "\t" + escapeField(it.chapterTitle) + "\t" + escapeField(it.source) + "\t";
			out += stateName(it.state);
			snprintf(buf, sizeof buf, "\t%d\t%llu\t%lld", it.done, (unsigned long long)it.bytes, (long long)it.added);
			out += buf;
			out += "\t" + escapeField(it.error) + "\t" + escapeField(it.target) + "\t" + encodePages(it.pageUrls) + "\n";
		}
		return out;
	}

	void DownloadQueue::add(DlItem it)
	{
		// the caller may set the key itself; one that only knows the two ids gets the joined one
		if (it.id.empty()) it.id = it.seriesId + "\n" + it.chapterId;
		if (it.id.empty() || it.id == "\n") return;
		// a chapter this long is broken, and its first 5000 pages saved as whole would lie about it
		if (it.pageUrls.size() > MAX_PAGES) return;
		if (items_.size() >= MAX_ITEMS) return;
		if (find(it.id)) return;
		if (it.done < 0 || it.done > int(it.pageUrls.size())) it.done = 0;
		it.error.clear();	// a chapter added back starts clean; retry() is what carries a failure
		items_.push_back(it);
		serial_++;
	}

	void DownloadQueue::remove(const std::string& id)
	{
		size_t before = items_.size();
		items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const DlItem& it) { return it.id == id; }),
			items_.end());
		if (items_.size() != before) serial_++;
	}

	void DownloadQueue::pause(const std::string& id)
	{
		DlItem* it = find(id);
		// a failed one is retried, not paused: its state holds the error the Downloads page shows
		if (!it || (it->state != DlItem::Waiting && it->state != DlItem::Running)) return;
		it->state = DlItem::Paused;
		serial_++;
	}

	void DownloadQueue::resume(const std::string& id)
	{
		DlItem* it = find(id);
		if (!it || it->state != DlItem::Paused) return;
		it->state = DlItem::Waiting;
		allPaused_ = false;
		serial_++;
	}

	void DownloadQueue::pauseAll()
	{
		if (allPaused_) return;
		allPaused_ = true;
		serial_++;
	}

	void DownloadQueue::resumeAll()
	{
		if (!allPaused_) return;
		allPaused_ = false;
		serial_++;
	}

	const DlItem* DownloadQueue::next() const
	{
		if (allPaused_ || lowSpace_) return nullptr;
		for (const auto& it : items_)
			if (it.state == DlItem::Waiting) return &it;
		return nullptr;
	}

	void DownloadQueue::setLowSpace(bool low)
	{
		if (lowSpace_ == low) return;
		lowSpace_ = low;
		serial_++;
	}

	void DownloadQueue::started(const std::string& id)
	{
		DlItem* it = find(id);
		if (!it || it->state != DlItem::Waiting) return;
		it->state = DlItem::Running;
		it->error.clear();
		serial_++;
	}

	void DownloadQueue::pageDone(const std::string& id, uint64_t bytes)
	{
		DlItem* it = find(id);
		if (!it || it->state == DlItem::Done) return;
		// the list's end is the end: a page the worker counted past it has nowhere to be written
		if (it->done >= int(it->pageUrls.size())) return;
		it->done++;
		it->bytes += bytes;
		serial_++;
	}

	void DownloadQueue::failed(const std::string& id, const std::string& error)
	{
		DlItem* it = find(id);
		if (!it || it->state == DlItem::Done) return;
		it->state = DlItem::Failed;
		it->error = error;
		// done stays: the pages on the card are not fetched again by the retry
		serial_++;
	}

	void DownloadQueue::retry(const std::string& id)
	{
		DlItem* it = find(id);
		if (!it || it->state != DlItem::Failed) return;
		it->state = DlItem::Waiting;
		it->error.clear();
		serial_++;
	}

	void DownloadQueue::finished(const std::string& id)
	{
		DlItem* it = find(id);
		if (!it || it->state == DlItem::Done) return;
		it->state = DlItem::Done;
		it->error.clear();
		serial_++;
	}

	void DownloadQueue::clearDone()
	{
		size_t before = items_.size();
		items_.erase(std::remove_if(items_.begin(), items_.end(),
						 [](const DlItem& it) { return it.state == DlItem::Done; }),
			items_.end());
		if (items_.size() != before) serial_++;
	}

	std::vector<DlItem> DownloadQueue::items() const
	{
		return items_;
	}

	int DownloadQueue::pendingPages() const
	{
		int n = 0;
		for (const auto& it : items_)
		{
			if (it.state == DlItem::Done) continue;
			int left = int(it.pageUrls.size()) - it.done;
			if (left > 0) n += left;
		}
		return n;
	}

	DlItem* DownloadQueue::find(const std::string& id)
	{
		for (auto& it : items_)
			if (it.id == id) return &it;
		return nullptr;
	}

	const DlItem* DownloadQueue::find(const std::string& id) const
	{
		for (const auto& it : items_)
			if (it.id == id) return &it;
		return nullptr;
	}
}
