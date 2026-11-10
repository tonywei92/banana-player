// The download engine with a fake fetcher: no thread, no server, canned pages written where the fetcher would put
// them. Each check is one rule of docs/design/manga.md section 7 (one chapter at a time, two pages at once, resume,
// low space) or one naming rule FAT makes the card's business.
#include "mangadownloader.h"
#include "cbz.h"
#include "comicinfo.h"
#include "dlqueue.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace bp;

static int failures = 0;
#define CHECK_EQ(a, b) \
	do { auto va = (a); auto vb = (b); \
		if (!(va == vb)) { failures++; fprintf(stderr, "%s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

namespace
{
	bool exists(const std::string& path)
	{
		struct stat sb;
		return stat(path.c_str(), &sb) == 0;
	}

	void rmTree(const std::string& path)
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
			rmTree(path + "/" + e->d_name);
		}
		closedir(d);
		rmdir(path.c_str());
	}

	int countEntries(const std::string& path)
	{
		DIR* d = opendir(path.c_str());
		if (!d) return 0;
		int n = 0;
		while (struct dirent* e = readdir(d))
			if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) n++;
		closedir(d);
		return n;
	}

	// The fetcher as a test wants it: get() only records, answer() writes the canned page and returns the result.
	// That way a page exists exactly when the test hands its answer to the engine, as a real fetch would.
	class FakeFetch final : public FetchApi
	{
	public:
		struct Req
		{
			uint64_t id = 0;
			std::string url, toFile;
		};

		std::vector<Req> reqs;	// every get, in order
		std::vector<Req> live;	// asked for, not answered or cancelled yet
		std::vector<uint64_t> cancelled;
		std::map<std::string, int> failLeft;	// a URL that fails this many more times
		std::map<std::string, std::string> types;	// the content type a page answers with

		uint64_t get(const std::string& url, const Headers&, int64_t, const std::string& toFile) override
		{
			Req r{next_++, url, toFile};
			reqs.push_back(r);
			live.push_back(r);
			return r.id;
		}

		void cancel(uint64_t id) override
		{
			cancelled.push_back(id);
			for (auto it = live.begin(); it != live.end(); ++it)
				if (it->id == id)
				{
					live.erase(it);
					return;
				}
		}

		size_t countUrl(const std::string& url) const
		{
			size_t n = 0;
			for (const Req& r : reqs)
				if (r.url == url) n++;
			return n;
		}

		MangaFetch::Result answer()
		{
			Req r = live.front();
			live.erase(live.begin());
			MangaFetch::Result out;
			out.id = r.id;
			out.finalUrl = r.url;
			auto f = failLeft.find(r.url);
			if (f != failLeft.end() && f->second > 0)
			{
				f->second--;
				out.status = 500;
				out.error = "server said no";
				return out;
			}
			FILE* fp = fopen(r.toFile.c_str(), "wb");
			std::string body = "PAGE " + r.url;
			if (fp)
			{
				fwrite(body.data(), 1, body.size(), fp);
				fclose(fp);
				out.file = r.toFile;
			}
			else out.error = "could not write the page";
			out.status = 200;
			auto t = types.find(r.url);
			out.contentType = t != types.end() ? t->second : "image/jpeg";
			return out;
		}

	private:
		uint64_t next_ = 1;
	};

	struct Harness
	{
		FakeFetch fake;
		DownloadQueue q;
		MangaDownloader dl;
		int64_t now = 0;

		explicit Harness(const std::string& stateDir) : dl(fake, q) { q.load(stateDir); }

		DlItem item(const std::string& id) const
		{
			for (const DlItem& it : q.items())
				if (it.id == id) return it;
			return DlItem{};
		}

		// answer what is in flight, pump, until the item ends; the clock jumps a second when nothing was handed,
		// which is past every retry backoff
		void run(const std::string& id)
		{
			for (int step = 0; step < 500; step++)
			{
				dl.pump(now);
				bool handed = false;
				while (!fake.live.empty())
				{
					MangaFetch::Result r = fake.answer();
					dl.onFetched(r);
					handed = true;
				}
				DlItem it = item(id);
				if (it.state == DlItem::Done || it.state == DlItem::Failed) return;
				now += handed ? 100 : 1000;
			}
		}
	};

	struct Tmp
	{
		std::string dir;
		Tmp()
		{
			char name[] = "/tmp/bp-downloader-XXXXXX";
			char* made = mkdtemp(name);
			if (made) dir = made;
		}
		~Tmp()
		{
			if (!dir.empty()) rmTree(dir);
		}
	};

	DlItem chapter(const std::string& id, int pages, const std::string& chapterTitle = "Chapter 1")
	{
		DlItem it;
		it.id = id;
		it.seriesId = "series-1";
		it.seriesTitle = "Test Series";
		it.chapterId = "c1";
		it.chapterTitle = chapterTitle;
		it.source = "komga";
		for (int p = 1; p <= pages; p++) it.pageUrls.push_back("http://example/page/" + std::to_string(p) + ".jpg");
		return it;
	}
}

static void testFivePageChapter()
{
	Tmp t;
	CHECK_EQ(t.dir.empty(), false);
	const std::string state = t.dir + "/state", root = t.dir + "/Manga", work = state + "/manga/downloading";
	Harness h(state);
	h.dl.setFolders(root, work);
	h.dl.setFreeSpaceProbe([] { return uint64_t(8) << 30; });

	DlItem it = chapter("s1\nc1", 5);
	h.fake.types["http://example/page/2.jpg"] = "image/png";	// the answer's type names the file, not the URL
	h.q.add(it);
	uint64_t before = h.dl.serial();
	h.run(it.id);

	CHECK_EQ(h.item(it.id).state, DlItem::Done);
	CHECK_EQ(h.fake.reqs.size(), (size_t)5);
	CHECK_EQ(h.dl.serial() > before, true);
	CHECK_EQ(h.dl.status().runningId, std::string(""));
	CHECK_EQ(h.dl.status().total, 0);

	const std::string cbz = root + "/Test Series/Chapter 1.cbz";
	CHECK_EQ(exists(cbz), true);
	Cbz z;
	CHECK_EQ(z.open(cbz), true);
	CHECK_EQ(z.pages(), (std::vector<std::string>{"0001.jpg", "0002.png", "0003.jpg", "0004.jpg", "0005.jpg"}));
	std::vector<uint8_t> out;
	CHECK_EQ(z.read("0002.png", out) && std::string(out.begin(), out.end()) == "PAGE http://example/page/2.jpg",
		true);

	std::vector<uint8_t> xml;
	CHECK_EQ(z.read("ComicInfo.xml", xml), true);
	ComicInfo ci;
	CHECK_EQ(parseComicInfo(std::string(xml.begin(), xml.end()), ci), true);
	CHECK_EQ(ci.series, std::string("Test Series"));
	CHECK_EQ(ci.title, std::string("Chapter 1"));
	CHECK_EQ(ci.number, std::string("1"));
	CHECK_EQ(ci.pageCount, 5);
	CHECK_EQ(ci.sourceId, std::string("komga"));
	CHECK_EQ(ci.seriesKey, std::string("series-1"));

	// the pages are gone: the CBZ is the copy that stays
	CHECK_EQ(countEntries(work), 0);
}

static void testRetryThenFail()
{
	Tmp t;
	const std::string state = t.dir + "/state", root = t.dir + "/Manga", work = state + "/manga/downloading";
	Harness h(state);
	h.dl.setFolders(root, work);
	h.dl.setFreeSpaceProbe([] { return uint64_t(8) << 30; });

	DlItem it = chapter("s1\nc2", 3);
	h.fake.failLeft["http://example/page/2.jpg"] = 1000;
	h.q.add(it);
	h.run(it.id);

	DlItem got = h.item(it.id);
	CHECK_EQ(got.state, DlItem::Failed);
	CHECK_EQ(got.done, 1);	// page 1 was written before page 2 gave up; a retry resumes there
	CHECK_EQ(got.error.empty(), false);
	CHECK_EQ(h.fake.countUrl("http://example/page/2.jpg"), (size_t)3);	// one try and two retries
	CHECK_EQ(h.dl.status().runningId, std::string(""));
	CHECK_EQ(exists(root + "/Test Series/Chapter 1.cbz"), false);	// a half chapter never looks whole
	CHECK_EQ(countEntries(work), 1);	// its folder waits for the retry on the Downloads page
}

static void testResumeFetchesOnlyWhatIsMissing()
{
	Tmp t;
	const std::string state = t.dir + "/state", root = t.dir + "/Manga", work = state + "/manga/downloading";
	DlItem it = chapter("s1\nc3", 5);
	{
		Harness h(state);
		h.dl.setFolders(root, work);
		h.dl.setFreeSpaceProbe([] { return uint64_t(8) << 30; });
		h.q.add(it);
		h.dl.pump(0);
		CHECK_EQ(h.fake.live.size(), (size_t)2);
		CHECK_EQ(h.dl.onFetched(h.fake.answer()), true);
		CHECK_EQ(h.dl.onFetched(h.fake.answer()), true);
		h.dl.pump(100);
		CHECK_EQ(h.fake.live.size(), (size_t)2);
		CHECK_EQ(h.dl.onFetched(h.fake.answer()), true);	// three pages are on the card now
		CHECK_EQ(h.item(it.id).done, 3);
		CHECK_EQ(h.q.save(), true);
	}

	Harness h(state);
	h.dl.setFolders(root, work);
	h.dl.setFreeSpaceProbe([] { return uint64_t(8) << 30; });
	CHECK_EQ(h.item(it.id).done, 3);	// the queue remembers where it stopped
	h.run(it.id);

	CHECK_EQ(h.item(it.id).state, DlItem::Done);
	CHECK_EQ(h.fake.reqs.size(), (size_t)2);	// only pages 4 and 5 were fetched again
	if (h.fake.reqs.size() == 2)
	{
		CHECK_EQ(h.fake.reqs[0].url, std::string("http://example/page/4.jpg"));
		CHECK_EQ(h.fake.reqs[1].url, std::string("http://example/page/5.jpg"));
	}
	Cbz z;
	CHECK_EQ(z.open(root + "/Test Series/Chapter 1.cbz"), true);
	CHECK_EQ(z.pages().size(), (size_t)5);
}

static void testLowSpaceBlocksStarting()
{
	Tmp t;
	const std::string state = t.dir + "/state", root = t.dir + "/Manga", work = state + "/manga/downloading";
	Harness h(state);
	h.dl.setFolders(root, work);
	h.dl.setFreeSpaceProbe([] { return uint64_t(100) << 20; });	// under 500 MB

	DlItem it = chapter("s1\nc4", 2);
	h.q.add(it);
	h.dl.pump(0);
	CHECK_EQ(h.fake.reqs.size(), (size_t)0);
	CHECK_EQ(h.dl.status().lowSpace, true);
	CHECK_EQ(h.q.next(), nullptr);

	h.dl.setFreeSpaceProbe([] { return uint64_t(8) << 30; });
	h.dl.pump(10 * 1000);	// the 30 s recheck hasn't come
	CHECK_EQ(h.fake.reqs.size(), (size_t)0);

	h.dl.pump(31 * 1000);
	CHECK_EQ(h.dl.status().lowSpace, false);
	CHECK_EQ(h.fake.reqs.size(), (size_t)2);
	h.now = 31 * 1000;
	h.run(it.id);
	CHECK_EQ(h.item(it.id).state, DlItem::Done);
}

static void testSafeNamesAndClashes()
{
	CHECK_EQ(MangaDownloader::safeName("  A/B\\C:D*E?F\"G<H>I|J  "), std::string("A B C D E F G H I J"));
	CHECK_EQ(MangaDownloader::safeName(std::string(200, 'a')), std::string(120, 'a'));
	CHECK_EQ(MangaDownloader::safeName(".hidden."), std::string("hidden"));
	CHECK_EQ(MangaDownloader::safeName("  ..  "), std::string(""));
	CHECK_EQ(MangaDownloader::safeName("A\t\tB"), std::string("A B"));
	CHECK_EQ(MangaDownloader::safeName("Ch: 1/2?").find_first_of("/\\:*?\"<>|"), std::string::npos);

	Tmp t;
	const std::string state = t.dir + "/state", root = t.dir + "/Manga", work = state + "/manga/downloading";
	Harness h(state);
	h.dl.setFolders(root, work);
	h.dl.setFreeSpaceProbe([] { return uint64_t(8) << 30; });

	DlItem a = chapter("s1\nc5", 1, "Ch: 1/2?"), b = chapter("s1\nc6", 1, "Ch: 1/2?");
	a.seriesTitle = b.seriesTitle = "A/B: C?";
	a.chapterId = "c5";
	b.chapterId = "c6";
	h.q.add(a);
	h.q.add(b);
	h.run(a.id);
	h.run(b.id);

	CHECK_EQ(h.item(a.id).state, DlItem::Done);
	CHECK_EQ(h.item(b.id).state, DlItem::Done);
	CHECK_EQ(exists(root + "/A B C/Ch 1 2.cbz"), true);
	CHECK_EQ(exists(root + "/A B C/Ch 1 2 (2).cbz"), true);	// the clash keeps both chapters
}

static void testPauseCancelsInFlight()
{
	Tmp t;
	const std::string state = t.dir + "/state", root = t.dir + "/Manga", work = state + "/manga/downloading";
	Harness h(state);
	h.dl.setFolders(root, work);
	h.dl.setFreeSpaceProbe([] { return uint64_t(8) << 30; });

	DlItem it = chapter("s1\nc7", 5);
	h.q.add(it);
	h.dl.pump(0);
	CHECK_EQ(h.fake.live.size(), (size_t)2);
	std::vector<uint64_t> ids;
	for (const FakeFetch::Req& r : h.fake.live) ids.push_back(r.id);

	h.dl.pauseAll();
	CHECK_EQ(h.q.allPaused(), true);
	CHECK_EQ(h.fake.cancelled.size(), (size_t)2);
	CHECK_EQ(h.dl.status().runningId, it.id);

	// an answer to a cancelled request is nobody's now, even if it arrives after the pause
	MangaFetch::Result stale;
	stale.id = ids[0];
	stale.status = 200;
	stale.file = "/nonexistent";
	stale.contentType = "image/jpeg";
	CHECK_EQ(h.dl.onFetched(stale), false);
	h.dl.pump(500);
	CHECK_EQ(h.fake.reqs.size(), (size_t)2);

	h.q.resumeAll();
	h.dl.pump(1000);
	CHECK_EQ(h.fake.reqs.size(), (size_t)4);
	CHECK_EQ(h.fake.live.size(), (size_t)2);

	h.dl.cancel(it.id);
	CHECK_EQ(h.fake.cancelled.size(), (size_t)4);
	CHECK_EQ(h.dl.status().runningId, std::string(""));
	CHECK_EQ(h.item(it.id).state, DlItem::Paused);	// not Running: a state no next() would pick again
}

int main()
{
	testFivePageChapter();
	testRetryThenFail();
	testResumeFetchesOnlyWhatIsMissing();
	testLowSpaceBlocksStarting();
	testSafeNamesAndClashes();
	testPauseCancelsInFlight();
	if (failures)
	{
		fprintf(stderr, "downloader: %d failed\n", failures);
		return 1;
	}
	printf("downloader: all passed\n");
	return 0;
}
