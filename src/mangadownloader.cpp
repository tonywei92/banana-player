#include "mangadownloader.h"
#include "browser.h"
#include "cbzwrite.h"
#include "comicinfo.h"
#include "manga_names.h"	// chapterNumber: a chapter's number comes from its name, as the reader counts it
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		// Short on purpose: a chapter's page that fails twice is more likely a server saying no than a hiccup, and a
		// reader waiting on the Downloads page should see the outcome in seconds.
		const int64_t kRetryWaitMs[] = {500, 1500};

		// A page is never near this; the fetcher's own default, so a broken server can't fill the card in one answer.
		const int64_t kPageMaxBytes = 64 * 1024 * 1024;

		bool isImageExt(const std::string& e)
		{
			return e == "jpg" || e == "jpeg" || e == "png" || e == "webp" || e == "gif" || e == "avif" ||
				e == "bmp";
		}

		std::string lower(std::string s)
		{
			for (char& c : s)
				if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
			return s;
		}

		// The extension as the URL spells it. It's only a first guess: the address can carry a query string, no
		// extension at all, or one the server contradicts.
		std::string extFromUrl(const std::string& url)
		{
			std::string path = url.substr(0, url.find_first_of("?#"));
			size_t slash = path.rfind('/');
			std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
			size_t dot = file.rfind('.');
			if (dot == std::string::npos) return "";
			std::string e = lower(file.substr(dot + 1));
			return isImageExt(e) ? e : "";
		}

		// What the answer says it is: the authority once the bytes are here, so a page named .php lands as .jpg
		std::string extFromType(const std::string& contentType)
		{
			std::string t = lower(contentType.substr(0, contentType.find(';')));
			while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
			size_t at = t.find_first_not_of(" \t");
			if (at != std::string::npos) t = t.substr(at);
			if (t == "image/jpeg" || t == "image/jpg" || t == "image/pjpeg") return "jpg";
			if (t == "image/png") return "png";
			if (t == "image/webp") return "webp";
			if (t == "image/gif") return "gif";
			if (t == "image/avif") return "avif";
			if (t == "image/bmp") return "bmp";
			return "";
		}

		bool statFile(const std::string& path, uint64_t& size)
		{
			struct stat sb;
			if (stat(path.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode)) return false;
			size = (uint64_t)sb.st_size;
			return true;
		}

		bool exists(const std::string& path)
		{
			struct stat sb;
			return stat(path.c_str(), &sb) == 0;
		}

		bool mkdirs(const std::string& path)
		{
			if (path.empty()) return false;
			for (size_t i = 1; i <= path.size(); i++)
			{
				if (i != path.size() && path[i] != '/') continue;
				std::string part = path.substr(0, i);
				if (part.empty() || part == "/") continue;
				mkdir(part.c_str(), 0755);
			}
			struct stat sb;
			return stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
		}

		void removeTree(const std::string& path)
		{
			DIR* d = opendir(path.c_str());
			if (!d)
			{
				remove(path.c_str());
				return;
			}
			while (struct dirent* e = readdir(d))
			{
				if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
				removeTree(path + "/" + e->d_name);
			}
			closedir(d);
			rmdir(path.c_str());
		}

		std::string baseName(const std::string& path)
		{
			size_t slash = path.rfind('/');
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}

		// FNV-1a: stable across runs and filesystems, and no character of an id (one holds a newline) reaches a path
		std::string hashId(const std::string& id)
		{
			uint64_t h = 1469598103934665603ull;
			for (unsigned char c : id)
			{
				h ^= c;
				h *= 1099511628211ull;
			}
			char buf[17];
			snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
			return buf;
		}

		int utf8Len(unsigned char c)
		{
			if (c >= 0xF0) return 4;
			if (c >= 0xE0) return 3;
			if (c >= 0xC0) return 2;
			return 1;
		}

		// At most maxChars characters (code points): a byte cut would leave half a sequence and a name no tool reads
		std::string trimToChars(const std::string& s, size_t maxChars)
		{
			size_t at = 0, chars = 0;
			while (at < s.size() && chars < maxChars)
			{
				int n = utf8Len((unsigned char)s[at]);
				if (at + (size_t)n > s.size()) n = 1;
				at += (size_t)n;
				chars++;
			}
			return s.substr(0, at);
		}

		std::string numberText(double v)
		{
			if (v < 0) return "";
			char buf[32];
			if (v == (double)(long long)v) snprintf(buf, sizeof buf, "%lld", (long long)v);
			else snprintf(buf, sizeof buf, "%.10g", v);
			return buf;
		}

		uint64_t defaultFreeBytes(const std::string& dir)
		{
			// A card whose size can't be read is not evidence of a full one: refusing every download on a filesystem
			// without statvfs would be worse than trying, which fails honestly when the write does. The downloads
			// folder may not have been made yet, so its nearest existing parent answers for the card.
			if (dir.empty()) return ~uint64_t(0);
			std::string at = dir;
			for (;;)
			{
				struct statvfs sv;
				if (statvfs(at.c_str(), &sv) == 0) return (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
				if (at == "/") return ~uint64_t(0);
				size_t slash = at.find_last_of('/');
				if (slash == std::string::npos) return ~uint64_t(0);
				at = slash == 0 ? "/" : at.substr(0, slash);
			}
		}
	}

	void MangaDownloader::setFolders(const std::string& downloadsRoot, const std::string& workDir)
	{
		downloadsRoot_ = downloadsRoot;
		while (downloadsRoot_.size() > 1 && downloadsRoot_.back() == '/') downloadsRoot_.pop_back();
		workDir_ = workDir;
		while (workDir_.size() > 1 && workDir_.back() == '/') workDir_.pop_back();
		lastSpaceCheck_ = -1;	// another folder may be another card: the next pump probes it now
	}

	void MangaDownloader::setFreeSpaceProbe(std::function<uint64_t()> probe)
	{
		probe_ = std::move(probe);
	}

	std::string MangaDownloader::safeName(const std::string& name)
	{
		std::string out;
		out.reserve(name.size());
		bool lastSpace = false;
		for (size_t i = 0; i < name.size();)
		{
			unsigned char c = (unsigned char)name[i];
			int n = utf8Len(c);
			if (n > 1 && i + (size_t)n <= name.size())
			{
				out.append(name, i, (size_t)n);
				lastSpace = false;
				i += (size_t)n;
				continue;
			}
			if (c == ' ' || c == '\t')
			{
				if (!out.empty() && !lastSpace)
				{
					out += ' ';
					lastSpace = true;
				}
				i++;
				continue;
			}
			if (c < 0x20 || c == 0x7f)
			{
				i++;
				continue;
			}
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
				c == '|')
			{
				// One space, not one per bad character: "Ch: 1/2?" is "Ch 1 2", not a row of gaps
				if (!out.empty() && !lastSpace)
				{
					out += ' ';
					lastSpace = true;
				}
				i++;
				continue;
			}
			out += (char)c;
			lastSpace = false;
			i++;
		}
		// FAT drops trailing dots and spaces, so "x." and "x" would be one name on the card and two in the queue
		while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
		while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
		out = trimToChars(out, 120);
		while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
		return out;
	}

	std::string MangaDownloader::itemDir(const std::string& id) const
	{
		if (workDir_.empty()) return "";
		return workDir_ + "/" + hashId(id);
	}

	std::string MangaDownloader::pagePath(int page, const std::string& ext) const
	{
		char name[16];
		snprintf(name, sizeof name, "%04d", page + 1);
		return dir_ + "/" + name + "." + ext;
	}

	void MangaDownloader::scanWork()
	{
		present_.assign((size_t)total_, "");
		DIR* d = opendir(dir_.c_str());
		if (!d) return;
		while (struct dirent* e = readdir(d))
		{
			std::string name = e->d_name;
			if (name.size() < 6 || name[4] != '.') continue;
			bool digits = true;
			for (int k = 0; k < 4; k++)
				if (name[k] < '0' || name[k] > '9') digits = false;
			if (!digits) continue;
			int page = (name[0] - '0') * 1000 + (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
			if (page < 1 || page > total_) continue;
			if (!isImageExt(lower(name.substr(5)))) continue;
			// Two files for one page is a leftover no run makes; the first name seen wins and the folder is wiped
			// after the chapter is written anyway.
			if (present_[(size_t)page - 1].empty()) present_[(size_t)page - 1] = dir_ + "/" + name;
		}
		closedir(d);
	}

	void MangaDownloader::pump(int64_t nowMs)
	{
		lastNow_ = nowMs;
		for (;;)
		{
			if (!running_)
			{
				if (queue_.allPaused()) return;
				// next() hides everything while the queue is low, so only the probe can say when to look again
				const DlItem* n = queue_.next();
				if (!n && !lowSpace_) return;
				if (!spaceOk(nowMs)) return;
				n = queue_.next();
				if (!n) return;
				startItem(*n);
				if (!running_) continue;	// an empty or broken chapter failed at once: take the next one
			}
			// Pause can also be pressed on the queue directly; the engine still must not keep a request in flight.
			if (queue_.allPaused())
			{
				dropFlights(true);
				retries_.clear();
				return;
			}
			advance();
			if (!running_) continue;
			if (done_ >= total_)
			{
				finishItem();
				continue;
			}
			while (flights_.size() < 2)
			{
				int page = -1;
				bool fromRetry = false;
				for (auto it = retries_.begin(); it != retries_.end(); ++it)
					if (it->at <= nowMs)
					{
						page = it->page;
						retries_.erase(it);
						fromRetry = true;
						break;
					}
				if (page < 0)
				{
					// Only the next two pages of the reading order: a later one would wait on a gap anyway, and
					// two is what the design allows in flight.
					for (int k = done_; k < total_ && k < done_ + 2; k++)
					{
						if (!present_[(size_t)k].empty()) continue;
						bool busy = false;
						for (const Flight& f : flights_)
							if (f.page == k) busy = true;
						for (const Retry& r : retries_)
							if (r.page == k) busy = true;
						if (busy) continue;
						page = k;
						break;
					}
				}
				if (page < 0) break;
				// A page that arrived while its retry waited (or was left by an earlier run) is already here
				if (!present_[(size_t)page].empty()) continue;
				if (!spaceOk(nowMs))
				{
					// A freed card is worth another look; the page itself has not failed
					if (fromRetry) retries_.push_back(Retry{page, nowMs + kSpaceRecheckMs});
					return;
				}
				if (!issue(page))
				{
					if (fromRetry) retries_.push_back(Retry{page, nowMs + 1000});
					return;
				}
			}
			return;
		}
	}

	void MangaDownloader::startItem(const DlItem& it)
	{
		item_ = it;
		running_ = true;
		total_ = (int)it.pageUrls.size();
		done_ = 0;
		reported_ = it.done;
		present_.assign((size_t)total_, "");
		attempts_.assign((size_t)total_, 0);
		flights_.clear();
		retries_.clear();
		st_ = Status{};
		st_.runningId = it.id;
		st_.total = total_;
		st_.lowSpace = lowSpace_;
		queue_.started(it.id);
		dir_ = itemDir(it.id);
		touch();
		if (total_ <= 0)
		{
			failItem("the chapter has no pages");
			return;
		}
		if (!mkdirs(dir_))
		{
			failItem("could not make the work folder");
			return;
		}
		scanWork();
		advance();	// what a quit or a pause left behind counts, so it is not fetched again
		if (done_ >= total_) finishItem();
	}

	void MangaDownloader::advance()
	{
		bool counted = false;
		while (done_ < total_ && !present_[(size_t)done_].empty())
		{
			uint64_t size = 0;
			statFile(present_[(size_t)done_], size);
			done_++;
			st_.done = done_;
			st_.bytes += size;
			// The queue may already have counted this page before a quit; it must not count it twice
			if (reported_ < done_)
			{
				queue_.pageDone(item_.id, size);
				reported_++;
			}
			counted = true;
		}
		if (counted) touch();
	}

	bool MangaDownloader::issue(int page)
	{
		std::string ext = extFromUrl(item_.pageUrls[(size_t)page]);
		if (ext.empty()) ext = "jpg";
		uint64_t id = fetch_->get(item_.pageUrls[(size_t)page], FetchApi::Headers{}, kPageMaxBytes,
			pagePath(page, ext));
		if (!id) return false;	// the fetcher is stopped: no attempt was made
		attempts_[(size_t)page]++;
		flights_.push_back(Flight{id, page});
		return true;
	}

	bool MangaDownloader::onFetched(const MangaFetch::Result& r)
	{
		if (!running_) return false;
		auto f = std::find_if(flights_.begin(), flights_.end(), [&](const Flight& x) { return x.fetchId == r.id; });
		if (f == flights_.end()) return false;	// paused, cancelled, or not ours: main still has other requests
		int page = f->page;
		flights_.erase(f);

		bool ok = r.error.empty() && (r.status == 0 || (r.status >= 200 && r.status < 300)) && !r.file.empty();
		std::string path;
		if (ok)
		{
			std::string ext = extFromType(r.contentType);
			if (ext.empty()) ext = extFromUrl(item_.pageUrls[(size_t)page]);
			if (ext.empty()) ext = "jpg";
			path = pagePath(page, ext);
			// The fetcher wrote under the URL's guess; the answer's type is the truthful name
			if (r.file != path) ok = std::rename(r.file.c_str(), path.c_str()) == 0;
			if (ok) ok = exists(path);
		}
		if (ok)
		{
			present_[(size_t)page] = path;
			advance();
			if (done_ >= total_) finishItem();
			else touch();
			return true;
		}

		std::string error = r.error;
		if (error.empty()) error = r.status > 0 ? "HTTP " + std::to_string(r.status) : "no answer";
		int attempt = attempts_[(size_t)page];
		if (attempt >= kMaxAttempts)
		{
			failItem(error);
			return true;
		}
		int64_t wait = kRetryWaitMs[std::min(attempt, 2) - 1];
		retries_.push_back(Retry{page, lastNow_ + wait});
		touch();
		return true;
	}

	bool MangaDownloader::spaceOk(int64_t nowMs)
	{
		if (lowSpace_ && lastSpaceCheck_ >= 0 && nowMs - lastSpaceCheck_ < kSpaceRecheckMs) return false;
		uint64_t freeBytes = probe_ ? probe_() : defaultFreeBytes(downloadsRoot_);
		lastSpaceCheck_ = nowMs;
		bool low = freeBytes < kMinFreeBytes;
		if (low != lowSpace_)
		{
			lowSpace_ = low;
			st_.lowSpace = low;
			queue_.setLowSpace(low);
			touch();
		}
		return !low;
	}

	void MangaDownloader::finishItem()
	{
		dropFlights(false);
		retries_.clear();
		std::string error;
		if (!writeChapter(error))
		{
			failItem(error.empty() ? "could not write the chapter" : error);
			return;
		}
		// The CBZ is the copy that stays: the pages would take the card twice, and the next run starts at page 1
		removeTree(dir_);
		queue_.finished(item_.id);
		queue_.save();
		resetRun();
		touch();
	}

	void MangaDownloader::failItem(const std::string& error)
	{
		dropFlights(false);
		retries_.clear();
		// The work folder stays: the retry resumes there instead of pulling the whole chapter again
		queue_.failed(item_.id, error);
		queue_.save();
		resetRun();
		touch();
	}

	bool MangaDownloader::writeChapter(std::string& error)
	{
		std::string series = safeName(item_.seriesTitle);
		if (series.empty()) series = safeName(item_.seriesId);
		if (series.empty()) series = "Unknown Series";
		std::string base = safeName(item_.chapterTitle);
		if (base.empty()) base = safeName(item_.chapterId);
		if (base.empty()) base = "Chapter";

		std::string seriesDir = downloadsRoot_ + "/" + series;
		if (cardOut(downloadsRoot_))
		{
			error = "The second card is out";
			return false;
		}
		if (downloadsRoot_.empty() || !mkdirs(seriesDir))
		{
			error = "could not make " + seriesDir;
			return false;
		}

		// A chapter written before (a re-download, another source with the same name) must not be hidden by this
		// one: FAT has no case-sensitive names to save it either, hence the suffix rather than an overwrite.
		std::string target;
		for (int n = 1; n < 1000; n++)
		{
			std::string suffix = n == 1 ? "" : " (" + std::to_string(n) + ")";
			target = seriesDir + "/" + trimToChars(base, 120 - suffix.size()) + suffix + ".cbz";
			if (!exists(target)) break;
		}

		ComicInfo ci;
		ci.series = item_.seriesTitle;
		ci.title = item_.chapterTitle;
		double number = chapterNumber(item_.chapterTitle);
		if (number >= 0) ci.number = numberText(number);
		ci.pageCount = total_;
		ci.sourceId = item_.source;
		ci.seriesKey = item_.seriesId;
		const std::string xml = writeComicInfo(ci);

		CbzWriter w;
		if (!w.open(target))
		{
			error = w.error();
			return false;
		}
		if (!w.add("ComicInfo.xml", (const uint8_t*)xml.data(), xml.size()))
		{
			error = w.error();
			w.abort();
			return false;
		}
		for (int p = 0; p < total_; p++)
		{
			if (present_[(size_t)p].empty() || !w.addFile(baseName(present_[(size_t)p]), present_[(size_t)p]))
			{
				error = present_[(size_t)p].empty() ? "a page went missing" : w.error();
				w.abort();
				return false;
			}
		}
		if (!w.finish())
		{
			error = w.error();
			return false;
		}
		return true;
	}

	void MangaDownloader::dropFlights(bool abandoned)
	{
		for (const Flight& f : flights_)
		{
			fetch_->cancel(f.fetchId);
			// A request stopped by a pause was never answered: it must not count against the page's two retries
			if (abandoned && attempts_[(size_t)f.page] > 0) attempts_[(size_t)f.page]--;
		}
		flights_.clear();
	}

	void MangaDownloader::pauseAll()
	{
		queue_.pauseAll();
		dropFlights(true);
		retries_.clear();
		touch();
	}

	void MangaDownloader::cancel(const std::string& id)
	{
		if (!running_ || item_.id != id) return;
		dropFlights(false);
		retries_.clear();
		// Running would be a state no next() ever picks again; the Downloads page resumes a Paused one
		queue_.pause(id);
		resetRun();
		touch();
	}

	void MangaDownloader::resetRun()
	{
		bool low = lowSpace_;
		running_ = false;
		item_ = DlItem();
		dir_.clear();
		present_.clear();
		attempts_.clear();
		flights_.clear();
		retries_.clear();
		done_ = 0;
		reported_ = 0;
		total_ = 0;
		st_ = Status{};
		st_.lowSpace = low;
	}
}
