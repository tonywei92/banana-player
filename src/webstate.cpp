#include "webstate.h"
#include "json.h"
#include "netpath.h"
#include "playlist.h"
#include <cctype>
#include <cmath>

namespace bp
{
	std::string toJson(const WebSnapshot& s)
	{
		JsonWriter j;
		j.obj();
		j.key("path").str(s.path).key("title").str(s.title).key("artist").str(s.artist).key("album").str(s.album);
		if (!s.netServer.empty()) j.key("netServer").str(s.netServer);
		j.key("kind").str(s.kind);
		j.key("source").obj().key("kind").str(s.sourceKind).key("title").str(s.sourceTitle).key("id").str(s.sourceId);
		j.key("index").num(s.index).key("count").num(s.count).end();
		j.key("screen").str(s.screen);
		j.key("loading").boolean(s.loading).key("paused").boolean(s.paused);
		j.key("position").num(s.position).key("positionAt").num(s.positionAtMs);
		j.key("duration").num(s.duration).key("speed").num(s.speed).key("volume").num(s.volume).key("volumeMax").num(s.volumeMax);
		j.key("eq").obj().key("on").boolean(s.eqOn).key("preset").str(s.eqPreset).key("gains").arr();
		for (double g : s.eqGains) j.num(g);
		j.end().end();
		j.key("fx").str(s.fx).key("loop").str(s.loop).key("shuffled").boolean(s.shuffled).key("shuffleMode").boolean(s.shuffleMode);
		j.key("chapters").arr();
		for (auto& c : s.chapters) j.obj().key("title").str(c.title).key("start").num(c.start).end();
		j.end();
		j.key("chapter").num(s.chapter);
		j.key("tracks");
		if (!s.hasTracks) j.null();
		else
		{
			auto list = [&](const char* name, const std::vector<WebTrack>& v) {
				j.key(name).arr();
				for (auto& t : v)
					j.obj().key("id").num(t.id).key("label").str(t.label).key("selected").boolean(t.selected)
						.key("default").boolean(t.isDefault).key("forced").boolean(t.forced).key("external").boolean(t.external).key("lang").str(t.lang).end();
				j.end();
			};
			j.obj();
			list("audio", s.audioTracks);
			list("sub", s.subTracks);
			j.key("aid").num(s.aid).key("sid").num(s.sid).end();
		}
		j.key("clients").num(s.clients);
		if (s.session.empty()) j.key("session").obj().key("boot").num(s.sessionBoot).key("v").num(s.sessionV).end();
		else j.key("session").raw(s.session);
		j.key("playlistSerial").num(s.playlistSerial).key("liked").boolean(s.liked).key("showHiddenFolders").boolean(s.showHiddenFolders);
		j.key("mangaOn").boolean(s.mangaOn);
		j.key("incognito").boolean(s.incognito);
		j.end();
		return j.out();
	}

	bool snapshotChanged(const WebSnapshot& a, const WebSnapshot& b)
	{
		if (a.path != b.path || a.netServer != b.netServer || a.title != b.title || a.artist != b.artist || a.album != b.album || a.kind != b.kind || a.screen != b.screen ||
			a.sourceKind != b.sourceKind || a.sourceTitle != b.sourceTitle || a.sourceId != b.sourceId ||
			a.index != b.index || a.count != b.count || a.loading != b.loading || a.paused != b.paused ||
			a.duration != b.duration || a.speed != b.speed || a.volume != b.volume || a.volumeMax != b.volumeMax || a.eqOn != b.eqOn ||
			a.eqPreset != b.eqPreset || a.eqGains != b.eqGains || a.fx != b.fx || a.loop != b.loop || a.shuffleMode != b.shuffleMode ||
			a.shuffled != b.shuffled || a.chapter != b.chapter || a.sessionV != b.sessionV || a.sessionBoot != b.sessionBoot ||
			a.clients != b.clients || a.playlistSerial != b.playlistSerial || a.liked != b.liked || a.showHiddenFolders != b.showHiddenFolders || a.mangaOn != b.mangaOn || a.incognito != b.incognito || a.chapters.size() != b.chapters.size() ||
			a.hasTracks != b.hasTracks || a.audioTracks != b.audioTracks || a.subTracks != b.subTracks || a.aid != b.aid ||
			a.sid != b.sid)
			return true;
		for (size_t i = 0; i < a.chapters.size(); i++)
			if (a.chapters[i].title != b.chapters[i].title || a.chapters[i].start != b.chapters[i].start) return true;
		double elapsed = std::max<double>(0, double(b.positionAtMs - a.positionAtMs) / 1000.0);
		// nothing playing: the clock stands still (else an idle player pushed a new state every 1.5 s)
		double expected = a.position + (a.paused || a.path.empty() ? 0 : elapsed * a.speed);
		return std::fabs(expected - b.position) > 1.5;
	}

	std::string webTitle(const std::string& path)
	{
		std::string name = isNetPath(path) ? netBaseName(path) : path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
		size_t dot = name.rfind('.');
		if (dot != std::string::npos && dot > 0) name.resize(dot);
		if (!isAudioFile(path)) return name;
		size_t i = 0;
		while (i < name.size() && i < 4 && isdigit((unsigned char)name[i])) i++;
		if (i == 0 || i > 3 || i == name.size()) return name;
		size_t digits = i, j = i;
		while (j < name.size() && name[j] == ' ') j++;
		bool sep = false;
		if (j < name.size() && (name[j] == '.' || name[j] == '-' || name[j] == '_' || name[j] == ')'))
		{
			sep = true;
			j++;
			while (j < name.size() && name[j] == ' ') j++;
		}
		// "2 Fast" (a bare one-digit number) and "1984" (no separator at all) are names
		if (!sep && (j == digits || digits < 2)) return name;
		if (j >= name.size()) return name;
		return name.substr(j);
	}

	std::string webTitle(const std::string& path, const std::string& mediaTitle)
	{
		std::string base = isNetPath(path) ? netBaseName(path) : path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
		if (!mediaTitle.empty() && mediaTitle != base) return mediaTitle;
		return webTitle(path);
	}
}
