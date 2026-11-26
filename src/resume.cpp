#include "resume.h"
#include "browser.h"
#include "picturelook.h"
#include "settings.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace bp
{
	// resume.tsv: "position<TAB>aid<TAB>sid<TAB>path" a line (the path last: it may hold a tab), and "#last-audio",
	// "#last-sub" lines for the languages. A file's length is a "#duration<TAB>seconds<TAB>path" line after its own:
	// a comment to a version from before it, which reads the file as it always did. A file's own look the same way:
	// "#look<TAB>id<TAB>strength<TAB>path"
	void ResumeStore::parse(const std::string& text)
	{
		entries.clear();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (line.rfind("#last-audio\t", 0) == 0) { lastAudioLang = line.substr(12); continue; }
			if (line.rfind("#last-folder\t", 0) == 0) { lastFolder = line.substr(13); continue; }
			if (line.rfind("#last-sub\t", 0) == 0)
			{
				std::string v = line.substr(10);
				lastSubOff = v == "-";
				lastSubLang = lastSubOff ? "" : v;
				continue;
			}
			if (line.rfind("#duration\t", 0) == 0)
			{
				size_t t = line.find('\t', 10);
				if (t == std::string::npos) continue;
				std::string path = line.substr(t + 1);
				// (its entry is the line before, as written; else wherever it is)
				ResumeEntry* e = !entries.empty() && entries.back().path == path ? &entries.back() : nullptr;
				for (size_t i = 0; !e && i < entries.size(); i++)
					if (entries[i].path == path) e = &entries[i];
				if (e) e->duration = atof(line.substr(10, t - 10).c_str());
				continue;
			}
			if (line.rfind("#look\t", 0) == 0)
			{
				size_t a = 6, b = line.find('\t', a), c = b == std::string::npos ? b : line.find('\t', b + 1);
				if (c == std::string::npos) continue;
				std::string path = line.substr(c + 1);
				ResumeEntry* e = !entries.empty() && entries.back().path == path ? &entries.back() : nullptr;
				for (size_t i = 0; !e && i < entries.size(); i++)
					if (entries[i].path == path) e = &entries[i];
				int look = pictureLookFromId(line.substr(a, b - a));
				if (!e || look < 0) continue;
				e->look = look;
				e->lookStrength = clampLookStrength(atoi(line.substr(b + 1, c - b - 1).c_str()));
				continue;
			}
			if (line.rfind("#place\t", 0) == 0)
			{
				// "#place<TAB>rev<TAB>when<TAB>by<TAB>path": a document's place's writer, after its entry
				size_t a = 7, b = line.find('\t', a), c = b == std::string::npos ? b : line.find('\t', b + 1),
					   d = c == std::string::npos ? c : line.find('\t', c + 1);
				if (d == std::string::npos) continue;
				std::string path = line.substr(d + 1);
				ResumeEntry* e = !entries.empty() && entries.back().path == path ? &entries.back() : nullptr;
				for (size_t i = 0; !e && i < entries.size(); i++)
					if (entries[i].path == path) e = &entries[i];
				if (!e) continue;
				e->rev = strtoull(line.substr(a, b - a).c_str(), nullptr, 10);
				e->when = atoll(line.substr(b + 1, c - b - 1).c_str());
				e->by = line.substr(c + 1, d - c - 1);
				continue;
			}
			if (line.empty() || line[0] == '#') continue;
			size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1),
				   t3 = t2 == std::string::npos ? t2 : line.find('\t', t2 + 1);
			if (t3 == std::string::npos) continue;
			ResumeEntry e;
			e.position = atof(line.substr(0, t1).c_str());
			e.aid = atoll(line.substr(t1 + 1, t2 - t1 - 1).c_str());
			e.sid = atoll(line.substr(t2 + 1, t3 - t2 - 1).c_str());
			e.path = line.substr(t3 + 1);
			if (!e.path.empty() && entries.size() < MAX_ENTRIES && !find(e.path)) entries.push_back(e);
		}
	}

	std::string ResumeStore::serialize() const
	{
		std::string out = "# banana-player: where each video was left (position, audio track, subtitle track, file)\n";
		if (!lastAudioLang.empty()) out += "#last-audio\t" + lastAudioLang + "\n";
		if (!lastFolder.empty()) out += "#last-folder\t" + lastFolder + "\n";
		if (lastSubOff) out += "#last-sub\t-\n";
		else if (!lastSubLang.empty()) out += "#last-sub\t" + lastSubLang + "\n";
		for (auto& e : entries)
		{
			char buf[64];
			snprintf(buf, sizeof buf, "%.3f\t%lld\t%lld\t", e.position, (long long)e.aid, (long long)e.sid);
			out += buf + e.path + "\n";
			if (e.duration > 0)
			{
				snprintf(buf, sizeof buf, "#duration\t%.3f\t", e.duration);
				out += buf + e.path + "\n";
			}
			if (e.look >= 0)
				out += std::string("#look\t") + pictureLookId(e.look) + "\t" + std::to_string(e.lookStrength) + "\t" + e.path + "\n";
			if (e.rev > 0)
			{
				// (a tab or a line break in the writer's name would end its field)
				std::string by = e.by;
				for (char& ch : by) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
				snprintf(buf, sizeof buf, "#place\t%llu\t%lld\t", (unsigned long long)e.rev, (long long)e.when);
				out += buf + by + "\t" + e.path + "\n";
			}
		}
		return out;
	}

	const ResumeEntry* ResumeStore::find(const std::string& path) const
	{
		for (auto& e : entries)
			if (e.path == path) return &e;
		return nullptr;
	}

	void ResumeStore::put(const ResumeEntry& e)
	{
		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].path == e.path)
			{
				entries.erase(entries.begin() + i);
				break;
			}
		entries.insert(entries.begin(), e);
		if (entries.size() > MAX_ENTRIES) entries.resize(MAX_ENTRIES);
	}

	void ResumeStore::setLook(const std::string& path, int look, int strength)
	{
		for (auto& e : entries)
			if (e.path == path)
			{
				e.look = look;
				e.lookStrength = look >= 0 ? clampLookStrength(strength) : 0;
				return;
			}
		if (look < 0) return;
		ResumeEntry e;
		e.path = path;
		e.look = look;
		e.lookStrength = clampLookStrength(strength);
		put(e);
	}

	bool ResumeStore::moved(const std::string& from, const std::string& to)
	{
		bool any = false;
		for (auto& e : entries)
		{
			std::string n;
			if (movedPath(e.path, from, to, n)) { e.path = n; any = true; }
		}
		return any;
	}

	bool worthResuming(double position, double duration)
	{
		if (position < 5) return false;
		if (duration > 0 && (position > duration - 15 || position > duration * 0.97)) return false;
		return true;
	}

	RecentProgress recentProgress(const ResumeEntry* e, bool audio)
	{
		RecentProgress r;
		r.duration = e && e->duration > 0 ? e->duration : -1;
		if (e && e->position >= 0 && !audio)
		{
			r.position = e->position;
			r.played = false;
			r.frac = r.duration > 0 ? std::clamp(e->position / r.duration, 0.0, 1.0) : 0;
		}
		return r;
	}
}
