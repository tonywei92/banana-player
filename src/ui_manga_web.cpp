#include "ui.h"
#include "manga_names.h"
#include "manga_ui_logic.h"
#include "manganet.h"
#include <cstdio>

namespace bp
{
	// The dashboard's series view (docs/design/manga.md section 8): the chapters as the reader knows them and the
	// chapters it starts downloading. A source series' list is asked of its worker exactly as the handheld's series
	// page asks it; the page polls while the answer comes. A local series comes from the card's scan. The download
	// state is read again on every request (the queue and the card move underneath the list).

	namespace
	{
		constexpr Uint32 WEB_ASK_RETRY_MS = 30000;	// a lost answer (the reader turned off) is asked again
		constexpr Uint32 WEB_FRESH_MS = 60000;		// a list is asked again after a minute

		std::string chapterTitle(const MangaChapter& c)
		{
			if (!c.name.empty()) return c.name;
			if (c.number > 0)
			{
				char buf[40];
				snprintf(buf, sizeof buf, "Chapter %g", c.number);
				return buf;
			}
			return "Chapter";
		}
	}

	bool Ui::mangaWebChapters(const std::string& seriesId, MangaWebSeries& out)
	{
		out = MangaWebSeries{};
		out.id = seriesId;
		Uint32 now = SDL_GetTicks();
		if (seriesId.rfind("local:", 0) == 0)
		{
			// the card's scan: the dashboard can ask before the handheld's Manga app was opened
			if (!mangaScanned) mangaScan();
			const LocalSeries* s = mangaLocalSeries(seriesId);
			if (!s)
			{
				out.error = "No such series";
				return false;
			}
			out.local = true;
			out.title = s->title;
			out.source = "local";
			for (const auto& c : s->chapters)
			{
				MangaWebChapter w;
				w.id = c.path;
				w.title = c.title;
				w.number = c.number;
				if (mangaStore)
					if (const ChapterProgress* pr = mangaStore->progress(c.path))
					{
						w.read = pr->read;
						w.page = pr->page;
						w.pages = pr->pages;
					}
				if (w.pages <= 0 && c.pages > 0) w.pages = c.pages;
				w.download = "onCard";	// it is the file on the card
				out.items.push_back(std::move(w));
			}
			return true;
		}

		if (!isSourceId(seriesId))
		{
			out.error = "No such series";
			return false;
		}
		std::string full, key;
		splitSourceId(seriesId, full, key);
		if (full.empty() || key.empty())
		{
			out.error = "No such series";
			return false;
		}
		if (!mangaNet || !mangaNet->started())
		{
			out.error = "The manga reader isn't running";
			return false;
		}

		// (a few series' lists kept for the dashboard, not every series it ever showed)
		if (mangaWebCache.size() > 16 && !mangaWebCache.count(seriesId)) mangaWebCache.clear();
		MangaWebChaptersState& st = mangaWebCache[seriesId];
		st.full = full;
		st.key = key;
		if (mangaStore)
			if (const MangaSeries* ms = mangaStore->series(seriesId))
			{
				st.title = ms->title;
				st.source = ms->source;
			}
		// an answer whose serial is gone (the reader turned off, the worker dropped it) is asked again
		if (st.serial && now - st.askedAt >= WEB_ASK_RETRY_MS)
		{
			mangaNet->worker().cancel(st.serial);
			st.serial = 0;
		}
		bool stale = st.have && now - st.askedAt >= WEB_FRESH_MS;
		bool retry = st.error.empty() || now - st.askedAt >= WEB_ASK_RETRY_MS;
		if (!st.serial && ((!st.have && retry) || stale))
		{
			std::string script;
			for (const auto& src : mangaNet->repos().installed())
				if (src.fullId == full)
				{
					script = src.file;
					break;
				}
			if (script.empty())
			{
				out.error = "Source not installed";
				return false;
			}
			if (st.itemJson.empty()) st.itemJson = mangaSourceItemJson(key, st.title.empty() ? key : st.title);
			MangaWorker::Job job;
			job.sourceId = full;
			job.scriptPath = script;
			job.kind = MangaWorker::Job::Chapters;
			job.json = st.itemJson;
			job.replace = false;	// the handheld's own series page may be asking for the same source at the same time
			st.serial = mangaNet->worker().submit(job);
			st.askedAt = now;
			st.error.clear();
			if (!st.serial)
			{
				out.error = "Couldn't ask the source";
				return false;
			}
		}
		out.title = st.title.empty() ? key : st.title;
		out.source = full;
		if (st.serial)
		{
			out.pending = true;
			return false;
		}
		if (!st.have)
		{
			out.error = st.error.empty() ? "The source didn't answer" : st.error;
			return false;
		}

		// the read marks, the places and the download marks as they are now
		mangaDlEnsureCard();
		for (const auto& c : st.chapters)
		{
			MangaWebChapter w;
			std::string cid = sourceItemId(full, c.key);
			w.id = cid;
			w.title = chapterTitle(c);
			w.number = c.number > 0 ? c.number : chapterNumber(c.name);
			if (mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(cid))
				{
					w.read = pr->read;
					w.page = pr->page;
					w.pages = pr->pages;
				}
			MangaDlMark m = mangaDlMarkFor(full, seriesId, c);
			switch (m.kind)
			{
			case MangaDlMark::Queued: w.download = "queued"; break;
			case MangaDlMark::Running: w.download = "running"; break;
			case MangaDlMark::Paused: w.download = "paused"; break;
			case MangaDlMark::Failed: w.download = "failed"; break;
			case MangaDlMark::OnCard: w.download = "onCard"; break;
			default: break;
			}
			w.done = m.done;
			w.total = m.total;
			out.items.push_back(std::move(w));
		}
		return true;
	}

	bool Ui::mangaWebWorker(MangaWorker::Result& r)
	{
		if (!r.serial) return false;
		for (auto& kv : mangaWebCache)
		{
			MangaWebChaptersState& st = kv.second;
			if (st.serial != r.serial) continue;
			st.serial = 0;
			st.askedAt = SDL_GetTicks();
			if (!r.ok)
			{
				st.error = r.error.empty() ? "The source didn't answer" : r.error;
				if (!st.have) st.chapters.clear();
				return true;
			}
			st.chapters.clear();
			// the source gives them newest first; the reader's pages list them oldest first, as the handheld does
			for (auto i = r.chapters.rbegin(); i != r.chapters.rend(); ++i)
				if (!i->key.empty()) st.chapters.push_back(*i);
			st.have = true;
			st.error.clear();
			return true;
		}
		return false;
	}

	bool Ui::mangaWebDownload(const std::string& seriesId, const std::vector<std::string>& chapterIds, int& added,
		std::string& error)
	{
		added = 0;
		error.clear();
		if (!isSourceId(seriesId))
		{
			error = "These chapters are already on the handheld";
			return false;
		}
		auto it = mangaWebCache.find(seriesId);
		if (it == mangaWebCache.end() || !it->second.have)
		{
			error = "Load the chapter list first";
			return false;
		}
		if (!mangaNet || !mangaNet->started())
		{
			error = "The manga reader isn't running";
			return false;
		}
		MangaWebChaptersState& st = it->second;
		std::string script;
		for (const auto& src : mangaNet->repos().installed())
			if (src.fullId == st.full)
			{
				script = src.file;
				break;
			}
		if (script.empty())
		{
			error = "Source not installed";
			return false;
		}
		std::vector<MangaChapter> chosen;
		for (const auto& cid : chapterIds)
			for (const auto& c : st.chapters)
				if (sourceItemId(st.full, c.key) == cid)
				{
					chosen.push_back(c);
					break;
				}
		if (chosen.empty())
		{
			error = "No chapters to download";
			return false;
		}
		added = mangaDlQueueChapters(st.full, script, seriesId, st.title.empty() ? st.key : st.title, chosen);
		return true;
	}
}
