#include "mangastore.h"
#include "playlists.h"	// writeFileAtomic: the tmp-then-rename the other stores write with
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>

namespace bp
{
	namespace
	{
		const char* LIB_HEADER = "# banana-player manga library v1";
		const char* PROGRESS_HEADER = "# banana-player manga progress v1";

		// A field's tabs, newlines and backslashes, so a line stays a line and a field stays a field. The carriage
		// return goes too: a line ending in one would look like a field that ends in one.
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

		bool toBool(const std::string& s, bool& out)
		{
			if (s == "1") { out = true; return true; }
			if (s == "0") { out = false; return true; }
			return false;
		}

		std::string lowerAscii(std::string s)
		{
			for (auto& c : s)
				if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
			return s;
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

		// A chapter was opened: its length is known or a page was turned
		bool started(const ChapterProgress& p)
		{
			return p.pages > 0 || p.page > 0;
		}

		// The Library page's order: the ones read most recently first, then by title
		bool libraryLess(const MangaSeries& a, const MangaSeries& b)
		{
			if (a.lastRead != b.lastRead) return a.lastRead > b.lastRead;
			std::string ta = lowerAscii(a.title), tb = lowerAscii(b.title);
			if (ta != tb) return ta < tb;
			if (a.title != b.title) return a.title < b.title;
			return a.id < b.id;
		}

		// The newest first, the same chapter twice always in the same place
		bool progressLess(const ChapterProgress& a, const ChapterProgress& b)
		{
			if (a.at != b.at) return a.at > b.at;
			return a.chapterId < b.chapterId;
		}
	}

	bool MangaStore::load(const std::string& dir)
	{
		series_.clear();
		progress_.clear();
		progIdxDirty_ = true;
		dir_ = dir;
		if (dir.empty()) return false;
		std::string text;
		if (readWhole(dir_ + "/manga/library.tsv", text)) parseLibrary(text);
		if (readWhole(dir_ + "/manga/progress.tsv", text)) parseProgress(text);
		serial_++;	// what the reader shows is different now
		return true;
	}

	void MangaStore::parseLibrary(const std::string& text)
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> f = splitTabs(line);
			// a later version may add columns: the extra fields are ignored, but ours must all be there
			if (f.size() < 8) continue;
			MangaSeries s;
			s.id = unescapeField(f[0]);
			s.title = unescapeField(f[1]);
			s.source = unescapeField(f[2]);
			s.cover = unescapeField(f[3]);
			int64_t added = 0, lastRead = 0;
			bool inLibrary = true;
			if (!toInt(f[4], added) || !toInt(f[5], lastRead) || !toBool(f[6], inLibrary)) continue;
			s.added = added;
			s.lastRead = lastRead;
			s.inLibrary = inLibrary;
			s.readingMode = unescapeField(f[7]);
			if (s.id.empty()) continue;
			bool known = false;
			for (auto& o : series_)
				if (o.id == s.id)
				{
					o = s;
					known = true;
					break;
				}
			if (!known) series_.push_back(s);
		}
		trimSeries();
	}

	void MangaStore::parseProgress(const std::string& text)
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> f = splitTabs(line);
			if (f.size() < 7) continue;
			ChapterProgress p;
			p.seriesId = unescapeField(f[0]);
			p.chapterId = unescapeField(f[1]);
			p.chapterTitle = unescapeField(f[2]);
			int64_t page = 0, pages = 0, at = 0;
			bool read = false;
			if (!toInt(f[3], page) || !toInt(f[4], pages) || !toBool(f[5], read) || !toInt(f[6], at)) continue;
			p.page = int(page);
			p.pages = int(pages);
			p.read = read;
			p.at = at;
			// the hidden column came last, so a file without it is an older one and its chapters are in the
			// History; a value we can't read says the same
			p.hidden = false;
			if (f.size() > 7) toBool(f[7], p.hidden);
			if (p.chapterId.empty()) continue;
			bool known = false;
			for (auto& o : progress_)
				if (o.chapterId == p.chapterId)
				{
					o = p;
					known = true;
					break;
				}
			if (!known)
			{
				progress_.push_back(p);
				progIdxDirty_ = true;
			}
		}
		trimProgress();
	}

	bool MangaStore::save() const
	{
		if (dir_.empty()) return false;
		std::string dir = dir_ + "/manga";
		mkdir(dir_.c_str(), 0755);
		mkdir(dir.c_str(), 0755);
		bool ok = writeFileAtomic(dir + "/library.tsv", serializeLibrary());
		ok = writeFileAtomic(dir + "/progress.tsv", serializeProgress()) && ok;
		return ok;
	}

	std::string MangaStore::serializeLibrary() const
	{
		std::string out = LIB_HEADER;
		out += "\n";
		char buf[64];
		for (const auto& s : series_)
		{
			out += escapeField(s.id) + "\t" + escapeField(s.title) + "\t" + escapeField(s.source) + "\t" +
				escapeField(s.cover) + "\t";
			snprintf(buf, sizeof buf, "%lld\t%lld\t", (long long)s.added, (long long)s.lastRead);
			out += buf;
			out += s.inLibrary ? "1\t" : "0\t";
			out += escapeField(s.readingMode) + "\n";
		}
		return out;
	}

	std::string MangaStore::serializeProgress() const
	{
		std::string out = PROGRESS_HEADER;
		out += "\n";
		char buf[64];
		for (const auto& p : progress_)
		{
			out += escapeField(p.seriesId) + "\t" + escapeField(p.chapterId) + "\t" + escapeField(p.chapterTitle) + "\t";
			snprintf(buf, sizeof buf, "%d\t%d\t", p.page, p.pages);
			out += buf;
			out += p.read ? "1\t" : "0\t";
			snprintf(buf, sizeof buf, "%lld\t", (long long)p.at);
			out += buf;
			// the hidden flag is the last column, so a file without it is an older one
			out += p.hidden ? "1\n" : "0\n";
		}
		return out;
	}

	std::vector<MangaSeries> MangaStore::library() const
	{
		std::vector<MangaSeries> out;
		for (const auto& s : series_)
			if (s.inLibrary) out.push_back(s);
		std::sort(out.begin(), out.end(), libraryLess);
		return out;
	}

	const MangaSeries* MangaStore::series(const std::string& id) const
	{
		for (const auto& s : series_)
			if (s.id == id) return &s;
		return nullptr;
	}

	void MangaStore::putSeries(const MangaSeries& s)
	{
		if (s.id.empty()) return;
		for (auto& o : series_)
			if (o.id == s.id)
			{
				o = s;
				serial_++;
				return;
			}
		series_.push_back(s);
		trimSeries();
		serial_++;
	}

	void MangaStore::setInLibrary(const std::string& id, bool on)
	{
		for (auto& s : series_)
			if (s.id == id)
			{
				s.inLibrary = on;
				serial_++;
				return;
			}
	}

	void MangaStore::removeSeries(const std::string& id)
	{
		series_.erase(std::remove_if(series_.begin(), series_.end(),
						  [&](const MangaSeries& s) { return s.id == id; }),
			series_.end());
		progress_.erase(std::remove_if(progress_.begin(), progress_.end(),
						   [&](const ChapterProgress& p) { return p.seriesId == id; }),
			progress_.end());
		progIdxDirty_ = true;
		serial_++;
	}

	void MangaStore::setProgress(const ChapterProgress& p)
	{
		ChapterProgress* e = nullptr;
		for (auto& o : progress_)
			if (o.chapterId == p.chapterId)
			{
				e = &o;
				break;
			}
		if (!e)
		{
			progress_.push_back(p);
			progIdxDirty_ = true;
			e = &progress_.back();
		}
		else
		{
			e->seriesId = p.seriesId;
			// a chapter read once stays read: opening it again doesn't unread it (markRead says otherwise)
			e->read = e->read || p.read;
			e->page = p.page;
			// the length and the name are kept when this update doesn't carry them: a caller that doesn't know
			// them yet shouldn't throw away what is known
			if (p.pages > 0) e->pages = p.pages;
			if (!p.chapterTitle.empty()) e->chapterTitle = p.chapterTitle;
			e->at = p.at;
		}
		// the last page means the chapter is read
		if (e->pages > 0 && e->page >= e->pages - 1) e->read = true;
		// the chapter is being read, so it belongs in the History again
		e->hidden = false;
		// the series was read now
		for (auto& s : series_)
			if (s.id == p.seriesId) s.lastRead = p.at;
		trimProgress();
		serial_++;
	}

	const ChapterProgress* MangaStore::progress(const std::string& chapterId) const
	{
		// by an index: a series page asks once per chapter on every redraw, and walking every chapter ever opened for
		// each of 1000 made the list lag on the handheld
		if (progIdxDirty_)
		{
			progIdx_.clear();
			progIdx_.reserve(progress_.size());
			for (size_t i = 0; i < progress_.size(); i++) progIdx_.emplace(progress_[i].chapterId, i);
			progIdxDirty_ = false;
		}
		auto it = progIdx_.find(chapterId);
		return it == progIdx_.end() ? nullptr : &progress_[it->second];
	}

	std::vector<ChapterProgress> MangaStore::continueReading(int max) const
	{
		std::vector<ChapterProgress> out;
		if (max <= 0) return out;
		// one pass over the chapters: each series' latest unfinished one. Looking for it per series instead would
		// walk every chapter of every series, and the Library page asks for this on every redraw
		std::unordered_map<std::string, const ChapterProgress*> best;
		for (const auto& p : progress_)
		{
			// (a row cleared from the History leaves Continue reading too: its place still opens the chapter there)
			if (p.read || p.hidden || !started(p)) continue;
			auto it = best.find(p.seriesId);
			if (it == best.end())
			{
				best.emplace(p.seriesId, &p);
				continue;
			}
			if (p.at > it->second->at || (p.at == it->second->at && p.chapterId < it->second->chapterId)) it->second = &p;
		}
		for (const auto& s : series_)
		{
			if (!s.inLibrary) continue;
			auto it = best.find(s.id);
			if (it != best.end()) out.push_back(*it->second);
		}
		std::sort(out.begin(), out.end(), progressLess);
		if (out.size() > size_t(max)) out.resize(size_t(max));
		return out;
	}

	std::vector<ChapterProgress> MangaStore::history(int max) const
	{
		if (max <= 0) return {};
		std::vector<ChapterProgress> out;
		// a chapter cleared from the History isn't in it, its progress stays
		for (const auto& p : progress_)
			if (!p.hidden) out.push_back(p);
		size_t n = out.size() > size_t(max) ? size_t(max) : out.size();
		// only the newest `max` are sorted into place: the History page shows a screenful, not every chapter
		std::partial_sort(out.begin(), out.begin() + long(n), out.end(), progressLess);
		out.resize(n);
		return out;
	}

	void MangaStore::markRead(const std::string& seriesId, const std::vector<std::string>& chapterIds, bool read)
	{
		for (const auto& id : chapterIds)
		{
			if (id.empty()) continue;
			ChapterProgress* e = nullptr;
			for (auto& o : progress_)
				if (o.chapterId == id)
				{
					e = &o;
					break;
				}
			if (!e)
			{
				// a chapter never opened becomes a record of, so it counts as unread until it is marked read
				ChapterProgress p;
				p.seriesId = seriesId;
				p.chapterId = id;
				p.read = read;
				progress_.push_back(p);
				progIdxDirty_ = true;
				continue;
			}
			e->read = read;
		}
		trimProgress();
		serial_++;
	}

	void MangaStore::clearHistory()
	{
		// only the History list goes: the read marks and the page to resume are the reader's own progress and
		// stay, so the unread counts and Continue reading keep working
		for (auto& p : progress_) p.hidden = true;
		serial_++;
	}

	void MangaStore::clearHistorySince(int64_t since)
	{
		for (auto& p : progress_)
			if (p.at >= since) p.hidden = true;
		serial_++;
	}

	int MangaStore::unread(const std::string& seriesId, const std::vector<std::string>& chapterIds) const
	{
		if (chapterIds.empty()) return 0;
		// one pass over the chapters, so a long chapter list doesn't walk the whole store for every id
		std::unordered_set<std::string> want(chapterIds.begin(), chapterIds.end());
		std::unordered_set<std::string> read;
		for (const auto& p : progress_)
		{
			// a chapter of another series isn't this one's, so it isn't read for this series either
			if (p.read && want.count(p.chapterId) && (p.seriesId.empty() || p.seriesId == seriesId)) read.insert(p.chapterId);
		}
		int n = 0;
		for (const auto& id : chapterIds)
			if (!read.count(id)) n++;
		return n;
	}

	void MangaStore::trimSeries()
	{
		while (series_.size() > MAX_SERIES)
		{
			size_t old = 0;
			for (size_t i = 1; i < series_.size(); i++)
				if (series_[i].added < series_[old].added) old = i;
			series_.erase(series_.begin() + long(old));
		}
	}

	void MangaStore::trimProgress()
	{
		while (progress_.size() > MAX_PROGRESS)
		{
			size_t old = 0;
			for (size_t i = 1; i < progress_.size(); i++)
				if (progress_[i].at < progress_[old].at) old = i;
			progress_.erase(progress_.begin() + long(old));
			progIdxDirty_ = true;
		}
	}
}
