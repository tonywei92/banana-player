// The parts without a window: aspect modes, track labels, mpv's track-list, times.
#include "aspect.h"
#include "battery.h"
#include "comicinfo.h"
#include "cbz.h"
#include "dlqueue.h"
#include "doh.h"
#include "cbzwrite.h"
#include "browser.h"
#include "challenge.h"
#include "crashlog.h"
#include "durations.h"
#include "durprobe.h"
extern "C"
{
#include <libavutil/log.h>
}
#include "eq.h"
#include "format.h"
#include "ftplist.h"
#include "history.h"
#include "htmldoc.h"
#include "photo.h"
#include "picsubs.h"
#include "png.h"
#include <zlib.h>
#include "update.h"
#include "backlight.h"
#include "playlist.h"
#include "playlists.h"
#include "keyboard.h"
#include "manga_names.h"
#include "mangaservers.h"
#include "mangastore.h"
#include "mangalocal.h"
#include "manganetfolders.h"
#include "manga_ui_logic.h"
#include "mangafilters.h"
#include "mangaremote.h"
#include "mangarepo.h"
#include "mangaseriescache.h"
#include "netpath.h"
#include "opds.h"
#include "paged.h"
#include "pagedecode.h"
#include "pagecache.h"
#include "readerfetchplan.h"
#include "secret.h"
#include "servers.h"
#include "sidecar.h"
#include "soundfx.h"
#include "resume.h"
#include "session.h"
#include "settings.h"
#include "strip.h"
#include "tarx.h"
#include "tracks.h"
#include "json.h"
#include "suwayomi.h"
#include "imagesize.h"
#include "bands.h"
#include "mangajava.h"
#include "webauth.h"
#include "webmedia.h"
#include "webpath.h"
#include "webstate.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace bp;

static int failures = 0;
#define CHECK_EQ(a, b) \
	do { auto va = (a); auto vb = (b); \
		if (!(va == vb)) { failures++; fprintf(stderr, "%s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

static void testAspect()
{
	CHECK_EQ(nextAspect(AspectMode::Fit), AspectMode::Fill);
	CHECK_EQ(nextAspect(AspectMode::Fill), AspectMode::Stretch);
	CHECK_EQ(nextAspect(AspectMode::Stretch), AspectMode::Fit);
	AspectMode m = AspectMode::Fit;
	CHECK_EQ(parseAspect("stretch", m), true);
	CHECK_EQ(m, AspectMode::Stretch);
	CHECK_EQ(parseAspect("zoom", m), false);
	CHECK_EQ(m, AspectMode::Stretch);

	// every mode sets the same properties, so switching never leaves one behind
	auto props = [](AspectMode a) {
		std::map<std::string, std::string> out;
		for (auto& p : aspectProperties(a)) out[p.first] = p.second;
		return out;
	};
	auto fit = props(AspectMode::Fit), fill = props(AspectMode::Fill), stretch = props(AspectMode::Stretch);
	CHECK_EQ(fit.size(), fill.size());
	CHECK_EQ(fit.size(), stretch.size());
	CHECK_EQ(fit["keepaspect"], std::string("yes"));
	CHECK_EQ(fit["panscan"], std::string("0.0"));
	CHECK_EQ(fill["keepaspect"], std::string("yes"));
	CHECK_EQ(fill["panscan"], std::string("1.0"));
	CHECK_EQ(stretch["keepaspect"], std::string("no"));
}

static void testTime()
{
	CHECK_EQ(formatTime(0), std::string("0:00"));
	CHECK_EQ(formatTime(75.9), std::string("1:15"));
	CHECK_EQ(formatTime(3725), std::string("1:02:05"));
	CHECK_EQ(formatTime(-1), std::string("--:--"));
}

// the file lists' durations: the cache (keys, cap, -1 kept), the notes, and the probe on files that aren't what they say
static void testDurations()
{
	// a file rewritten (another size or mtime) is another key
	CHECK_EQ(durationKey("/a.mp3", 10, 5) == durationKey("/a.mp3", 11, 5), false);
	CHECK_EQ(durationKey("/a.mp3", 10, 5) == durationKey("/a.mp3", 10, 6), false);
	CHECK_EQ(durationKey("/a.mp3", 10, 5) == durationKey("/a.mp3", 10, 5), true);
	DurationCache c(3);
	double d = 0;
	CHECK_EQ(c.find("a", d), false);
	c.put("a", 1);
	c.put("b", -1);	// a known "none" is kept, never probed again
	c.put("c", 3);
	CHECK_EQ(c.find("b", d), true);
	CHECK_EQ(d, -1.0);
	CHECK_EQ(c.find("a", d), true);	// a, then b: c is the least recently used
	c.put("d", 4);
	CHECK_EQ(c.size(), (size_t)3);
	CHECK_EQ(c.find("c", d), false);
	CHECK_EQ(c.find("a", d) && d == 1, true);
	c.put("a", 2);	// replaced, not added
	CHECK_EQ(c.size(), (size_t)3);
	CHECK_EQ(c.find("a", d) && d == 2, true);
	DurationCache big(5000);
	for (int i = 0; i < 6000; i++) big.put(std::to_string(i), i);
	CHECK_EQ(big.size(), (size_t)5000);
	CHECK_EQ(big.find("0", d), false);
	CHECK_EQ(big.find("5999", d), true);

	CHECK_EQ(saneDuration(214.5), 214.5);
	CHECK_EQ(saneDuration(0), -1.0);
	CHECK_EQ(saneDuration(-3), -1.0);
	CHECK_EQ(saneDuration(NAN), -1.0);
	CHECK_EQ(saneDuration(INFINITY), -1.0);
	CHECK_EQ(saneDuration(9e9), -1.0);
	CHECK_EQ(saneDuration(268435, 16000), -1.0);	// 16 KB "lasting" three days: a header that lies
	CHECK_EQ(saneDuration(214, 3500000), 214.0);

	const std::string DOT = " \xC2\xB7 ";
	CHECK_EQ(fileNote("", 214, 3670016), "3:34" + DOT + "3.5 MB");
	CHECK_EQ(fileNote("at 12:03", 6300, 1288490188), "at 12:03 / 1:45:00" + DOT + "1.2 GB");
	CHECK_EQ(fileNote("at 0:42", -1, 14575206), "at 0:42" + DOT + "13.9 MB");
	CHECK_EQ(fileNote("", -1, 1536), std::string("1.5 KB"));
	CHECK_EQ(fileNote("", 59.9, -1), std::string("0:59"));
	CHECK_EQ(fileNote("", -1, -1), std::string());

	// the probe: a real (if tiny) file, then ones that aren't: -1 each, fast, no crash, and cached
	char dir[] = "/tmp/bp-dur-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	int level = av_log_get_level();
	av_log_set_level(AV_LOG_QUIET);	// (what libavformat says of each broken file)
	std::string D = dir;
	auto write = [&](const std::string& name, const std::string& bytes) {
		std::ofstream(D + "/" + name, std::ios::binary) << bytes;
		return D + "/" + name;
	};
	auto le = [](uint32_t v, int n) { std::string s; for (int i = 0; i < n; i++) s += char((v >> (8 * i)) & 0xff); return s; };
	// 2 s of 8 kHz mono 16-bit silence
	std::string pcm(32000, '\0');
	std::string wav = "RIFF" + le(36 + pcm.size(), 4) + "WAVEfmt " + le(16, 4) + le(1, 2) + le(1, 2) + le(8000, 4) + le(16000, 4) + le(2, 2) + le(16, 2)
		+ "data" + le(pcm.size(), 4) + pcm;
	std::mt19937 rng(7);
	std::string noise(256 * 1024, '\0');
	for (auto& ch : noise) ch = (char)(rng() & 0xff);
	// an ID3v2 tag that claims 100 MB, then nothing; an EBML header with absurd sizes; a WAV cut short claiming 4 GB
	std::string id3 = std::string("ID3\x04\x00\x00", 6) + std::string("\x7f\x7f\x7f\x7f", 4) + "TIT2";
	std::string ebml = std::string("\x1a\x45\xdf\xa3\x01\xff\xff\xff\xff\xff\xff\xff", 12) + std::string("\x18\x53\x80\x67\xff", 5) + noise.substr(0, 4096);
	std::string cut = wav.substr(0, 40) + le(0xffffffffu, 4) + pcm.substr(0, 100);
	// want: seconds, or -1 none
	// a CBR MP3 with no Xing frame (200 silent 128 kb/s frames of 417 bytes): its length from its bitrate
	std::string cbr;
	for (int i = 0; i < 200; i++) cbr += std::string("\xff\xfb\x90\x64", 4) + std::string(413, '\0');
	struct Case { std::string name, bytes; double want; };
	std::vector<Case> cases = {
		{"ok.wav", wav, 2},
		{"cbr.mp3", cbr, 200 * 417 * 8 / 128000.0},
		{"empty.mp3", "", -1},
		{"noise.mkv", noise, -1},
		{"noise.mp3", noise, -1},
		{"tag-only.mp3", id3, -1},
		{"broken.mkv", ebml, -1},
		{"cut.wav", cut, -1},
		{"truncated.mp3", noise.substr(0, 3), -1},
	};
	for (auto& k : cases)
	{
		std::string path = write(k.name, k.bytes);
		auto t0 = std::chrono::steady_clock::now();
		double secs = probeDuration(path);
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		if (k.want > 0 ? std::fabs(secs - k.want) > 0.001 : secs != -1)
		{
			failures++;
			fprintf(stderr, "probeDuration(%s) = %.3f\n", k.name.c_str(), secs);
		}
		if (ms > 2500)
		{
			failures++;
			fprintf(stderr, "probeDuration(%s) took %.0f ms\n", k.name.c_str(), ms);
		}
		double took = 1;
		CHECK_EQ(probeDuration(path, &took), secs);
		CHECK_EQ(took, 0.0);	// cached, -1 too
		double cached = 0;
		CHECK_EQ(cachedDuration(path, cached) && cached == secs, true);
		unlink(path.c_str());
	}
	// the kinds with no length in their headers: not even opened (the probe would read their ends)
	CHECK_EQ(durationInHeader("a/b.TS"), false);
	CHECK_EQ(durationInHeader("x.m2ts"), false);
	CHECK_EQ(durationInHeader("x.vob"), false);
	CHECK_EQ(durationInHeader("x.aac"), false);
	CHECK_EQ(durationInHeader("x.mkv"), true);
	CHECK_EQ(durationInHeader("x.mp3"), true);
	CHECK_EQ(durationInHeader("noext"), true);
	CHECK_EQ(durationInHeader("x.AC3"), false);
	CHECK_EQ(durationInHeader("x.m2v"), false);
	CHECK_EQ(durationInHeader("x.h264"), false);
	CHECK_EQ(durationInHeader("x.mp4"), true);
	const unsigned char m1[] = {0xff, 0xfb, 0x90, 0x64}, m2[] = {0xff, 0xf3, 0x80, 0xc4}, l2[] = {0xff, 0xfd, 0x90, 0x64}, junk[] = {0x49, 0x44, 0x33, 0x04};
	CHECK_EQ(mp3FrameBitrate(m1, 4), 128000);	// MPEG-1 Layer III, 128 kb/s, 44.1 kHz
	CHECK_EQ(mp3FrameBitrate(m2, 4), 64000);	// MPEG-2 Layer III, 64 kb/s
	CHECK_EQ(mp3FrameBitrate(l2, 4), 0);		// Layer II
	CHECK_EQ(mp3FrameBitrate(junk, 4), 0);
	CHECK_EQ(mp3FrameBitrate(m1, 3), 0);
	// frames that agree (CBR) give their bitrate; one that differs (VBR), or too few, none
	std::string frames;
	for (int i = 0; i < 10; i++) frames += std::string("\xff\xfb\x90\x64", 4) + std::string(413, '\0');
	CHECK_EQ(mp3CbrBitrate((const unsigned char*)frames.data(), frames.size()), 128000);
	CHECK_EQ(mp3CbrBitrate((const unsigned char*)frames.data(), 417 * 5), 0);
	std::string vbr = frames.substr(0, 417 * 4) + std::string("\xff\xfb\xa0\x64", 4) + std::string(518, '\0') + frames;	// a 160 kb/s frame
	CHECK_EQ(mp3CbrBitrate((const unsigned char*)vbr.data(), vbr.size()), 0);
	{
		std::string ts = write("clip.ts", wav);	// (a WAV inside: opened, it would say 2 s)
		double took = 1;
		CHECK_EQ(probeDuration(ts, &took), -1.0);
		CHECK_EQ(took, 0.0);
		unlink(ts.c_str());
	}
	CHECK_EQ(probeDuration(D + "/gone.mp3"), -1.0);
	CHECK_EQ(probeDuration(D), -1.0);	// a folder
	rmdir(dir);
	av_log_set_level(level);
}

static void testMangaNames()
{
	// pages: the picture kinds whatever their case, and nothing else
	for (const char* n : {"1.jpg", "2.JPEG", "3.png", "4.WebP", "5.gif", "6.avif", "7.BMP", "ch1/10.jpg"})
		CHECK_EQ(isPageImage(n), true);
	for (const char* n : {"", "1.txt", "page", "page.jpg.txt", "ch1/", ".1.jpg", "ch1/.1.jpg", "ch1/__MACOSX/1.jpg",
			    "__MACOSX/1.jpg"})
		CHECK_EQ(isPageImage(n), false);

	// a chapter's pages in the browser's natural order (naturalLess, tested with the browser): 2 before 10
	std::vector<std::string> pages = {"page10.jpg", "page2.jpg", "Page1.jpg", "ch1/10.jpg", "ch1/2.jpg"};
	sortPages(pages);
	CHECK_EQ(pages[0], std::string("ch1/2.jpg"));
	CHECK_EQ(pages[1], std::string("ch1/10.jpg"));
	CHECK_EQ(pages[2], std::string("Page1.jpg"));
	CHECK_EQ(pages[3], std::string("page2.jpg"));
	CHECK_EQ(pages[4], std::string("page10.jpg"));

	// the chapter markers, mixed case and all
	CHECK_EQ(chapterNumber("Chapter 12"), 12.0);
	CHECK_EQ(chapterNumber("CHAPTER 12"), 12.0);
	CHECK_EQ(chapterNumber("Ch.12"), 12.0);
	CHECK_EQ(chapterNumber("ch 12.5"), 12.5);
	CHECK_EQ(chapterNumber("c012"), 12.0);
	CHECK_EQ(chapterNumber("#12"), 12.0);
	CHECK_EQ(chapterNumber("Ep 3"), 3.0);
	CHECK_EQ(chapterNumber("EP 3"), 3.0);
	CHECK_EQ(chapterNumber("Chapter 007"), 7.0);
	CHECK_EQ(chapterNumber("Vol.2 Ch.10.5 - The Title"), 10.5);
	// no marker: the last number, but a volume's is not a chapter's
	CHECK_EQ(chapterNumber("Series Name 045.cbz"), 45.0);
	CHECK_EQ(chapterNumber("Series 2 Name 045"), 45.0);
	CHECK_EQ(chapterNumber("Series 10.5"), 10.5);
	CHECK_EQ(chapterNumber("Vol 3"), -1.0);
	CHECK_EQ(chapterNumber("Volume 02"), -1.0);
	CHECK_EQ(chapterNumber("Vol.2"), -1.0);
	CHECK_EQ(chapterNumber("Series v1.2"), -1.0);
	CHECK_EQ(chapterNumber("Series - 012 (v2)"), 12.0);	// past the volume to the chapter
	// none at all
	CHECK_EQ(chapterNumber(""), -1.0);
	CHECK_EQ(chapterNumber("no digits here"), -1.0);
	CHECK_EQ(chapterNumber("Chapter"), -1.0);

	// volumes: the word or a directly attached v
	CHECK_EQ(volumeNumber("Vol.2"), 2.0);
	CHECK_EQ(volumeNumber("Vol 2"), 2.0);
	CHECK_EQ(volumeNumber("Volume 02"), 2.0);
	CHECK_EQ(volumeNumber("v2"), 2.0);
	CHECK_EQ(volumeNumber("Series v2"), 2.0);
	CHECK_EQ(volumeNumber("VOL 07"), 7.0);
	CHECK_EQ(volumeNumber("Vol.2 Ch.10.5 - The Title"), 2.0);
	CHECK_EQ(volumeNumber(""), -1.0);
	CHECK_EQ(volumeNumber("Chapter 12"), -1.0);
	CHECK_EQ(volumeNumber("Volume"), -1.0);
	CHECK_EQ(volumeNumber("Volt 3"), -1.0);
	CHECK_EQ(volumeNumber("nav2"), -1.0);	// "v" must start a word
}

// The URL chapter's fetches: what goes out now, what is dropped when the reader moves.
static void testReaderFetchPlan()
{
	ReaderFetchPlan::State s;
	s.wanted = {4, 4, 5, 6, 7, 3};	// the Strip's list can repeat the page in view
	s.max = 3;
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{4, 5, 6}));
	s.done = {5};	// already in the page cache: no fetch
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{4, 6, 7}));
	s.inFlight = {4};	// one out: two more fit under three
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{6, 7}));
	s.failed = {6};	// a dead URL is not asked for again while wanted
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{7, 3}));
	s.inFlight = {0, 1, 2};	// the room is the cap's, not the wanted list's
	CHECK_EQ(ReaderFetchPlan::next(s), std::vector<int>{});
	s = {};
	s.wanted = {-1, 1, 1, 2};	// a bad index is dropped, a repeat is one request
	s.max = 5;
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{1, 2}));

	// the reader moved: what is in flight for a page no longer wanted at all goes
	s.wanted = {2, 3};
	s.inFlight = {1, 2, 5};
	CHECK_EQ(ReaderFetchPlan::stale(s), (std::vector<int>{1, 5}));
	s.inFlight = {2, 3};
	CHECK_EQ(ReaderFetchPlan::stale(s), std::vector<int>{});

	// a jump's target goes alone: a neighbour already out is dropped while the target is not in
	s = {};
	s.wanted = {4, 5, 6};
	s.inFlight = {3, 4, 5, 7};
	s.target = 5;
	s.targetAt = 1000;
	s.now = 1100;
	CHECK_EQ(ReaderFetchPlan::stale(s), (std::vector<int>{3, 4, 7}));
	s.targetIn = true;
	CHECK_EQ(ReaderFetchPlan::stale(s), (std::vector<int>{3, 7}));
}

// The Seek rule: a tap goes at once, a drag's seeks coalesce, and the run's last page goes once they rest.
static void testSeekRun()
{
	SeekRun r;
	// the first Seek of a run: a tap on the slider, the grid's A
	CHECK_EQ(r.seek(30, 1000) == SeekRun::Now::Commit, true);
	CHECK_EQ(r.seeks(), 1);
	CHECK_EQ(r.pending(), false);
	CHECK_EQ(r.due(1000), false);
	// more within the settle window: a drag, its target the last page passed over
	CHECK_EQ(r.seek(4, 1040) == SeekRun::Now::Hold, true);
	CHECK_EQ(r.seek(9, 1080) == SeekRun::Now::Hold, true);
	CHECK_EQ(r.pending(), true);
	CHECK_EQ(r.target(), 9);
	CHECK_EQ(r.seeks(), 3);
	// 150 ms after the last one, not before
	CHECK_EQ(r.due(1229), false);
	CHECK_EQ(r.due(1230), true);
	r.commit();
	CHECK_EQ(r.pending(), false);
	CHECK_EQ(r.target(), -1);
	CHECK_EQ(r.due(2000), false);
	// a seek after the rest is a new run, at once
	CHECK_EQ(r.seek(12, 2000) == SeekRun::Now::Commit, true);
	CHECK_EQ(r.due(2001), false);
	// a run whose target is never committed: the next seek past the window is the newest target, at once
	CHECK_EQ(r.seek(14, 2100) == SeekRun::Now::Hold, true);
	CHECK_EQ(r.seek(20, 2300) == SeekRun::Now::Commit, true);
	CHECK_EQ(r.seeks(), 1);
	// reset (a chapter closed): no run, so the next seek is a tap
	r.reset();
	CHECK_EQ(r.pending(), false);
	CHECK_EQ(r.seek(2, 1) == SeekRun::Now::Commit, true);
}

// After a jump the target goes first, alone, until it is in (or 2 s), then the preload around it.
static void testReaderFetchTargetFirst()
{
	std::vector<int> w = {4, 5, 6};
	CHECK_EQ(ReaderFetchPlan::ordered(w, 5, false, 1000, 1100), std::vector<int>{5});
	CHECK_EQ(ReaderFetchPlan::ordered(w, 5, false, 1000, 2999), std::vector<int>{5});
	CHECK_EQ(ReaderFetchPlan::ordered(w, 5, false, 1000, 3000), w);
	CHECK_EQ(ReaderFetchPlan::ordered(w, 5, true, 1000, 1100), w);	// in: the preload may go
	CHECK_EQ(ReaderFetchPlan::ordered(w, -1, false, 1000, 1100), w);	// no jump
	CHECK_EQ(ReaderFetchPlan::ordered(w, 9, false, 1000, 1100), w);	// not in the list: nothing to wait for

	ReaderFetchPlan::State s;
	s.wanted = {4, 5, 6};
	s.max = 3;
	s.target = 5;
	s.targetAt = 1000;
	s.now = 1100;
	// the target alone: the neighbours do not take the free slots
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{5}));
	// its request out: still no neighbour starts
	s.inFlight = {5};
	CHECK_EQ(ReaderFetchPlan::next(s), std::vector<int>{});
	// in the cache: the preload follows
	s.inFlight = {};
	s.done = {5};
	s.targetIn = true;
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{4, 6}));
	// a failed target is not waited on: the preload can start after it
	s.done = {};
	s.failed = {5};
	s.targetIn = true;
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{4, 6}));
	// 2 s without it: the preload starts anyway
	s = {};
	s.wanted = {4, 5, 6};
	s.max = 3;
	s.target = 5;
	s.targetAt = 1000;
	s.now = 3000;
	CHECK_EQ(ReaderFetchPlan::next(s), (std::vector<int>{4, 5, 6}));
}

// The browser check (design 10's backlog): a source or a page behind Cloudflare's or DDoS-Guard's challenge must say
// so plainly, while an ordinary refusal (a plain 403, nginx's 503, an image) must not be mistaken for one.
static void testBrowserCheck()
{
	// Cloudflare's own header is enough, whatever the case of its name or value.
	CHECK_EQ(detectBrowserCheck(403, {{"cf-mitigated", "challenge"}}, ""), BrowserCheck::Cloudflare);
	CHECK_EQ(detectBrowserCheck(503, {{"CF-Mitigated", " Challenge "}}, ""), BrowserCheck::Cloudflare);
	// Or the markers in the page, beside Cloudflare's own server header.
	CHECK_EQ(detectBrowserCheck(403, {{"server", "cloudflare"}},
		"<!DOCTYPE html><title>Just a moment...</title>"), BrowserCheck::Cloudflare);
	CHECK_EQ(detectBrowserCheck(200, {{"server", "cloudflare"}}, "<script>window._cf_chl_opt={}</script>"),
		BrowserCheck::Cloudflare);
	CHECK_EQ(detectBrowserCheck(403, {{"server", "cloudflare"}},
		"<script src=\"/cdn-cgi/challenge-platform/h/b/orchestrate/chl_page/v1\"></script>"),
		BrowserCheck::Cloudflare);
	// An ordinary Cloudflare-served answer is not a check, header or not.
	CHECK_EQ(detectBrowserCheck(403, {{"server", "cloudflare"}, {"content-type", "text/html"}},
		"<html><body>Forbidden</body></html>"), BrowserCheck::None);
	CHECK_EQ(detectBrowserCheck(200, {{"server", "cloudflare"}, {"content-type", "image/jpeg"}},
		std::string("\xff\xd8\xff\xe0\x00\x10JFIF", 10)), BrowserCheck::None);
	// DDoS-Guard's block page names its own server; its page text is the fallback when a hop hides it.
	CHECK_EQ(detectBrowserCheck(403, {{"server", "ddos-guard"}}, ""), BrowserCheck::DdosGuard);
	CHECK_EQ(detectBrowserCheck(503, {{"Server", "DDoS-Guard"}}, ""), BrowserCheck::DdosGuard);
	CHECK_EQ(detectBrowserCheck(403, {}, "<title>DDoS-Guard</title>"), BrowserCheck::DdosGuard);
	// Ordinary failures are none of these: a plain 403, a 503 from nginx, a PNG.
	CHECK_EQ(detectBrowserCheck(403, {{"server", "nginx"}}, "<html><body>403 Forbidden</body></html>"),
		BrowserCheck::None);
	CHECK_EQ(detectBrowserCheck(503, {{"server", "nginx"}, {"content-type", "text/html"}},
		"<html><head><title>503 Service Temporarily Unavailable</title></head></html>"), BrowserCheck::None);
	CHECK_EQ(detectBrowserCheck(200, {{"content-type", "image/png"}}, std::string("\x89PNG\r\n\x1a\n", 8)),
		BrowserCheck::None);
	// The line both the worker and the reader show; None has no line.
	CHECK_EQ(std::string(browserCheckMessage(BrowserCheck::Cloudflare)),
		std::string("This source asks for a browser check (Cloudflare). Banana Player can't pass it yet."));
	CHECK_EQ(std::string(browserCheckMessage(BrowserCheck::DdosGuard)),
		std::string("This source asks for a browser check (DDoS-Guard). Banana Player can't pass it yet."));
	CHECK_EQ(std::string(browserCheckMessage(BrowserCheck::None)).empty(), true);
}

// 5.4's public-address rule: what a stranger's script may reach. Both sides of every range's boundary, the
// wrappers that embed an IPv4, zones and garbage.
static void testPublicAddress()
{
	for (const char* ip : {"1.0.0.0", "8.8.8.8", "1.1.1.1", "9.255.255.255", "11.0.0.1", "100.63.255.255",
		"100.128.0.0", "126.255.255.255", "128.0.0.1", "172.15.255.255", "172.32.0.1", "192.0.1.0", "192.0.3.0",
		"192.167.1.1", "192.169.1.1", "198.17.255.255", "198.20.0.0", "198.51.99.255", "198.51.101.0",
		"203.0.112.255", "203.0.114.0", "223.255.255.255", "2606:4700::1111", "2001:4860:4860::8888",
		"2001:db7:ffff::1", "2001:db9::1", "100:0:0:1::1", "feff::1", "::2", "::ffff:8.8.8.8", "64:ff9b::808:808",
		"2002:808:808::1", "8.8.8.8%en0", "2606:4700::1111%wlan0"})
	{
		CHECK_EQ(publicAddress(ip), true);
		if (!publicAddress(ip)) fprintf(stderr, "  not public: %s\n", ip);
	}
	for (const char* ip : {"0.0.0.0", "0.0.0.1", "0.255.255.255", "10.0.0.1", "10.255.255.255", "127.0.0.1",
		"127.255.255.255", "169.254.0.1", "169.254.255.255", "172.16.0.1", "172.31.255.255", "192.168.0.0",
		"192.168.255.255", "100.64.0.0", "100.127.255.255", "192.0.0.0", "192.0.0.255", "192.0.2.0", "192.0.2.255",
		"198.18.0.0", "198.19.255.255", "198.51.100.0", "198.51.100.255", "203.0.113.0", "203.0.113.255",
		"224.0.0.0", "231.0.0.0", "239.255.255.255", "240.0.0.0", "255.255.255.255", "::", "::1", "fe80::1",
		"fe80::1%wlan0", "febf::1", "fc00::1", "fdff::1", "ff00::", "ff02::1", "ffff::1", "2001:db8::",
		"2001:db8:ffff:ffff:ffff:ffff:ffff:ffff", "100::", "100::ffff:ffff:ffff:ffff", "64:ff9b::a00:1",
		"64:ff9b::7f00:1", "64:ff9b::c0a8:105", "::ffff:192.168.1.5", "::ffff:127.0.0.1", "::ffff:10.1.1.1",
		"::ffff:0.0.0.0", "::ffff:100.64.0.1", "2002:a00:1::", "2002:7f00:1::", "2002:c0a8:105::"})
	{
		CHECK_EQ(publicAddress(ip), false);
		if (publicAddress(ip)) fprintf(stderr, "  public: %s\n", ip);
	}
	// not an IP literal at all: a name is resolved first, so it is never "public" by itself
	for (const char* ip : {"", "localhost", "evil.com", "192.168.1", "192.168.1.256", "192.168.1.1.1",
		"not an ip", "[::1]", "::1x", " 10.0.0.1", "10.0.0.1 ", "0x7f.0.0.1", "2130706433", "8.8.8.8/32",
		"1.2.3", "fe80::1%"})
	{
		CHECK_EQ(publicAddress(ip), false);
		if (publicAddress(ip)) fprintf(stderr, "  public garbage: %s\n", ip);
	}
}

// the wire form of a Secure DNS packet, written by hand as the answers arrive
static void dohPut16(std::string& s, uint16_t v) { s.push_back((char)(v >> 8)); s.push_back((char)v); }
static void dohPut32(std::string& s, uint32_t v)
{
	s.push_back((char)(v >> 24)); s.push_back((char)(v >> 16)); s.push_back((char)(v >> 8)); s.push_back((char)v);
}
static void dohPutName(std::string& s, const std::string& name)
{
	size_t start = 0;
	for (;;)
	{
		size_t dot = name.find('.', start);
		std::string label = name.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		s.push_back((char)label.size());
		s += label;
		if (dot == std::string::npos) break;
		start = dot + 1;
	}
	s.push_back('\0');
}
static void dohPutPtr(std::string& s, size_t off)
{
	s.push_back((char)(0xC0 | (off >> 8)));
	s.push_back((char)off);
}

// Secure DNS (Manga settings): the RFC 8484 query — a header asking one question, the name's labels, type and class
static void testDohQuery()
{
	std::vector<uint8_t> q = dohQuery("api.mangadex.org", DOH_TYPE_A, 0x1234);
	CHECK_EQ(q.size(), (size_t)34);
	CHECK_EQ((int)q[0], 0x12);
	CHECK_EQ((int)q[1], 0x34);
	CHECK_EQ((int)q[2], 0x01);	// recursion desired
	CHECK_EQ((int)q[3], 0x00);
	CHECK_EQ((int)q[4], 0);
	CHECK_EQ((int)q[5], 1);		// one question
	CHECK_EQ((int)q[12], 3);
	CHECK_EQ(std::string((const char*)&q[13], 3), std::string("api"));
	CHECK_EQ((int)q[16], 8);
	CHECK_EQ(std::string((const char*)&q[17], 8), std::string("mangadex"));
	CHECK_EQ((int)q[25], 3);
	CHECK_EQ(std::string((const char*)&q[26], 3), std::string("org"));
	CHECK_EQ((int)q[29], 0);	// the root
	CHECK_EQ((int)q[30], 0);
	CHECK_EQ((int)q[31], 1);	// type A
	CHECK_EQ((int)q[32], 0);
	CHECK_EQ((int)q[33], 1);	// class IN
	CHECK_EQ(dohQuery("api.mangadex.org.", DOH_TYPE_A, 0x1234), q);	// the root's dot changes nothing
	CHECK_EQ(dohQuery(std::string(64, 'a'), DOH_TYPE_A, 1).empty(), true);
	CHECK_EQ(dohQuery("", DOH_TYPE_A, 1).empty(), true);
	CHECK_EQ(dohQuery("a..b", DOH_TYPE_A, 1).empty(), true);
}

// an answer with a CNAME chain: www.example.com -> edge.example.net -> cdn.example.org, whose A and AAAA records
// answer, the owners given as compression pointers (what Cloudflare sends)
static void testDohParseCname()
{
	std::string p;
	dohPut16(p, 0x4321);
	dohPut16(p, 0x8180);	// a response, recursion available
	dohPut16(p, 1);
	dohPut16(p, 4);		// two CNAMEs and an address of each family
	dohPut16(p, 0);
	dohPut16(p, 0);
	size_t qoff = p.size();
	dohPutName(p, "www.example.com");
	dohPut16(p, DOH_TYPE_A);
	dohPut16(p, 1);
	std::string edge;
	dohPutName(edge, "edge.example.net");
	std::string cdn;
	dohPutName(cdn, "cdn.example.org");
	dohPutPtr(p, qoff); dohPut16(p, 5); dohPut16(p, 1); dohPut32(p, 120); dohPut16(p, (uint16_t)edge.size());
	size_t eoff = p.size();
	p += edge;
	dohPutPtr(p, eoff); dohPut16(p, 5); dohPut16(p, 1); dohPut32(p, 60); dohPut16(p, (uint16_t)cdn.size());
	size_t coff = p.size();
	p += cdn;
	dohPutPtr(p, coff); dohPut16(p, DOH_TYPE_A); dohPut16(p, 1); dohPut32(p, 30); dohPut16(p, 4);
	p += std::string("\x5d\xb8\xd8\x22", 4);	// 93.184.216.34
	dohPutPtr(p, coff); dohPut16(p, DOH_TYPE_AAAA); dohPut16(p, 1); dohPut32(p, 45); dohPut16(p, 16);
	p += std::string("\x20\x01\x0d\xb8", 4) + std::string(11, '\0') + std::string(1, '\x01');

	DohAnswer a = parseDohAnswer(p, 0x4321, "www.example.com", DOH_TYPE_A);
	CHECK_EQ(a.status, DohAnswer::Ok);
	CHECK_EQ(a.addresses.size(), (size_t)1);
	CHECK_EQ(a.addresses.empty() ? std::string() : a.addresses[0], std::string("93.184.216.34"));
	CHECK_EQ(a.ttl, (uint32_t)30);	// the shortest link of the chain
	// the question says A, so a parser told AAAA refuses it: the query and the answer must be the same question
	CHECK_EQ(parseDohAnswer(p, 0x4321, "www.example.com", DOH_TYPE_AAAA).status, DohAnswer::Malformed);
	// asked as AAAA, the same chain answers with the other address
	std::string p6 = p;
	p6[qoff + 17] = 0;	// (the question's name is 17 bytes: www(4) example(8) com(4) root(1))
	p6[qoff + 18] = 28;
	DohAnswer v6 = parseDohAnswer(p6, 0x4321, "www.example.com", DOH_TYPE_AAAA);
	CHECK_EQ(v6.status, DohAnswer::Ok);
	CHECK_EQ(v6.addresses.size(), (size_t)1);
	CHECK_EQ(v6.addresses.empty() ? std::string() : v6.addresses[0], std::string("2001:db8::1"));
	CHECK_EQ(v6.ttl, (uint32_t)45);
}

// NOERROR with no address: the name exists but has none of that kind (NODATA, not a failure)
static void testDohParseNoRecords()
{
	std::string p;
	dohPut16(p, 0x0102); dohPut16(p, 0x8180); dohPut16(p, 1); dohPut16(p, 0); dohPut16(p, 0); dohPut16(p, 0);
	dohPutName(p, "example.com"); dohPut16(p, DOH_TYPE_A); dohPut16(p, 1);
	DohAnswer a = parseDohAnswer(p, 0x0102, "example.com", DOH_TYPE_A);
	CHECK_EQ(a.status, DohAnswer::NoRecords);
	CHECK_EQ(a.addresses.size(), (size_t)0);
	CHECK_EQ(a.error.empty(), true);
}

// NXDOMAIN: the name isn't there at all
static void testDohParseNxdomain()
{
	std::string p;
	dohPut16(p, 0x1111); dohPut16(p, 0x8183); dohPut16(p, 1); dohPut16(p, 0); dohPut16(p, 0); dohPut16(p, 0);
	dohPutName(p, "gone.example"); dohPut16(p, DOH_TYPE_A); dohPut16(p, 1);
	DohAnswer a = parseDohAnswer(p, 0x1111, "gone.example", DOH_TYPE_A);
	CHECK_EQ(a.status, DohAnswer::NameError);
}

// a cut answer: the truncation bit (what a resolver sets when its packet didn't fit); then a packet shorter than its
// own header, one answering another query, and an A record whose length isn't 4
static void testDohParseTruncated()
{
	std::string p;
	dohPut16(p, 0x2222); dohPut16(p, 0x8380); dohPut16(p, 1); dohPut16(p, 0); dohPut16(p, 0); dohPut16(p, 0);
	CHECK_EQ(parseDohAnswer(p, 0x2222, "example.com", DOH_TYPE_A).status, DohAnswer::Truncated);
	std::string shortPacket("\x22\x22\x83", 3);
	CHECK_EQ(parseDohAnswer(shortPacket, 0x2222, "example.com", DOH_TYPE_A).status, DohAnswer::Malformed);
	std::string p2;
	dohPut16(p2, 0x2222); dohPut16(p2, 0x8180); dohPut16(p2, 1); dohPut16(p2, 0); dohPut16(p2, 0); dohPut16(p2, 0);
	dohPutName(p2, "example.com"); dohPut16(p2, DOH_TYPE_A); dohPut16(p2, 1);
	CHECK_EQ(parseDohAnswer(p2, 0x3333, "example.com", DOH_TYPE_A).status, DohAnswer::Malformed);
	std::string p3;
	dohPut16(p3, 0x4444); dohPut16(p3, 0x8180); dohPut16(p3, 1); dohPut16(p3, 1); dohPut16(p3, 0); dohPut16(p3, 0);
	size_t qoff = p3.size();
	dohPutName(p3, "bad.example"); dohPut16(p3, DOH_TYPE_A); dohPut16(p3, 1);
	dohPutPtr(p3, qoff); dohPut16(p3, DOH_TYPE_A); dohPut16(p3, 1); dohPut32(p3, 10); dohPut16(p3, 5);
	p3 += std::string("\x01\x02\x03\x04\x05", 5);
	CHECK_EQ(parseDohAnswer(p3, 0x4444, "bad.example", DOH_TYPE_A).status, DohAnswer::Malformed);
}

// the cache: names lowercased, the TTL capped, a zero TTL not kept, and an entry gone at its expiry
static void testDohWantedFor()
{
	// public names go through DoH; literals and names only the local network knows keep the system's lookup
	CHECK_EQ(dohWantedFor("https://api.mangadex.org/manga"), true);
	CHECK_EQ(dohWantedFor("https://user:pw@komga.example.com:25600/api"), true);
	CHECK_EQ(dohWantedFor("http://192.168.1.20:25600/api"), false);
	CHECK_EQ(dohWantedFor("http://[fd00::2]:8080/"), false);
	CHECK_EQ(dohWantedFor("http://nas.local:25600/"), false);
	CHECK_EQ(dohWantedFor("http://NAS.LAN/opds"), false);
	CHECK_EQ(dohWantedFor("http://nas/opds"), false);
	CHECK_EQ(dohWantedFor("http://kavita.home.arpa/api"), false);
}

static void testDohCache()
{
	DohCache c;
	std::vector<std::string> out;
	c.put("Example.COM", DOH_TYPE_A, {"1.2.3.4"}, 120, 100.0);
	CHECK_EQ(c.size(), (size_t)1);
	CHECK_EQ(c.get("example.com", DOH_TYPE_A, 100.5, out), true);
	CHECK_EQ(out.size(), (size_t)1);
	CHECK_EQ(out.empty() ? std::string() : out[0], std::string("1.2.3.4"));
	out.clear();
	CHECK_EQ(c.get("example.com", DOH_TYPE_A, 220.1, out), false);
	c.put("example.com", DOH_TYPE_AAAA, {"2606:4700::1111"}, 9000, 0.0);
	CHECK_EQ(c.get("example.com", DOH_TYPE_AAAA, 299.0, out), true);
	out.clear();
	CHECK_EQ(c.get("example.com", DOH_TYPE_AAAA, 301.0, out), false);	// kMaxTtl, not the answer's 9000
	c.put("zero.example", DOH_TYPE_A, {"1.2.3.5"}, 0, 0.0);
	CHECK_EQ(c.get("zero.example", DOH_TYPE_A, 0.0, out), false);
	c.put("empty.example", DOH_TYPE_A, {}, 120, 0.0);
	CHECK_EQ(c.size(), (size_t)2);
	c.clear();
	CHECK_EQ(c.size(), (size_t)0);
}

// the servers' catalogue: the requests a Komga, Kavita or OPDS server is asked with, and the same answers read
// back into the shapes a source script returns (every body here is canned and invented, the way they are written)
static void testMangaRemote()
{
	MangaServer k;
	k.kind = "komga";
	k.name = "Home Komga";
	k.url = "http://komga.lan:25600/";	// trailing slash and all: joining is the model's job
	k.user = "reader";
	auto hdr = [](const RemoteRequest& r, const std::string& n)
	{
		for (const std::pair<std::string, std::string>& h : r.headers)
			if (h.first == n) return h.second;
		return std::string();
	};

	// Basic auth as RFC 7617 writes it, base64 over "user:password"; no user, no header — and never a header for
	// Kavita, whose OPDS url carries the api key in its path
	CHECK_EQ(remoteAuth(k, "s3cret", "http://komga.lan:25600/api/v1/libraries").headers.size(), (size_t)2);
	CHECK_EQ(hdr(remoteAuth(k, "s3cret", "u"), "Authorization"), std::string("Basic cmVhZGVyOnMzY3JldA=="));
	CHECK_EQ(hdr(remoteAuth(k, "s3cret", "u"), "Accept"), std::string("application/json"));
	MangaServer guest = k;
	guest.user = "";
	RemoteRequest g = remoteAuth(guest, "pw", "u");
	CHECK_EQ(g.headers.size(), (size_t)1);
	CHECK_EQ(hdr(g, "Authorization"), std::string(""));
	MangaServer kav;
	kav.kind = "kavita";
	kav.name = "Kavita";
	kav.url = "https://kavita.lan:5000/api/Opds/9c3f0a61be/";
	kav.user = "tony";
	RemoteRequest ka = remoteAuth(kav, "ignored", kav.url);
	CHECK_EQ(ka.headers.size(), (size_t)1);	// the key is in the url, so the password is never sent
	CHECK_EQ(hdr(ka, "Accept"), std::string("application/atom+xml"));

	// every Komga request the model builds
	CHECK_EQ(remoteKomgaLibraries(k, "s3cret").url, std::string("http://komga.lan:25600/api/v1/libraries"));
	CHECK_EQ(remoteKomgaSeries(k, "", "manga", 2).url, std::string(
		"http://komga.lan:25600/api/v1/series?library_id=manga&page=2&size=40&sort=metadata.titleSort"));
	// the search text is escaped: a "&" in a query is the server's separator, not part of the text
	CHECK_EQ(remoteKomgaSearch(k, "", "Night & Day 10", 0).url, std::string(
		"http://komga.lan:25600/api/v1/series?search=Night%20%26%20Day%2010&page=0&size=40"));
	CHECK_EQ(remoteKomgaBooks(k, "", "s7", 1).url, std::string(
		"http://komga.lan:25600/api/v1/series/s7/books?page=1&size=100&sort=metadata.numberSort"));
	RemoteRequest pg = remoteKomgaPage(k, "s3cret", "b7", 1);	// Komga's own page numbers start at 1
	CHECK_EQ(pg.url, std::string("http://komga.lan:25600/api/v1/books/b7/pages/1"));
	CHECK_EQ(hdr(pg, "Accept"), std::string("image/*"));	// a page is a picture whatever the api answers with
	CHECK_EQ(hdr(pg, "Authorization"), std::string("Basic cmVhZGVyOnMzY3JldA=="));
	CHECK_EQ(remoteKomgaPage(k, "", "b7", 0).url, std::string(""));	// below the first page is not a request
	CHECK_EQ(remoteKomgaSeriesCover(k, "", "s7").url, std::string("http://komga.lan:25600/api/v1/series/s7/thumbnail"));
	CHECK_EQ(hdr(remoteKomgaSeriesCover(k, "", "s7"), "Accept"), std::string("image/*"));
	CHECK_EQ(remoteKomgaBookCover(k, "", "b7").url, std::string("http://komga.lan:25600/api/v1/books/b7/thumbnail"));
	// the same server's OPDS face hangs under its url; the other kinds are stored as their OPDS url
	RemoteRequest ks = remoteOpdsStart(k, "");
	CHECK_EQ(ks.url, std::string("http://komga.lan:25600/opds/v1.2"));
	CHECK_EQ(hdr(ks, "Accept"), std::string("application/atom+xml"));
	RemoteRequest os = remoteOpdsStart(kav, "");
	CHECK_EQ(os.url, kav.url);

	// the libraries answer, which is a bare array, into folders — and the paged shape through the same door
	const std::string libs = R"([
		{"id":"lib-manga","name":"Manga","mediaType":"MANGA"},
		{"id":"lib-comics","name":"Comics","mediaType":"COMICS"}
	])";
	RemoteList folders;
	CHECK_EQ(remoteKomgaLibraryList(libs, folders), true);
	CHECK_EQ(folders.items.size(), (size_t)2);
	CHECK_EQ(folders.items[0].key, std::string("lib-manga"));
	CHECK_EQ(folders.items[0].title, std::string("Manga"));
	CHECK_EQ(folders.items[0].folder, true);
	CHECK_EQ(folders.items[0].cover, std::string(""));	// Komga offers no library thumbnails
	CHECK_EQ(folders.more, false);
	CHECK_EQ(remoteKomgaLibraryList(R"({"content":[{"id":"l","name":"One"}],"totalPages":1})", folders), true);
	CHECK_EQ(folders.items.size(), (size_t)1);
	CHECK_EQ(remoteKomgaLibraryList("nonsense", folders), false);

	// a page of series: keys, thumbnail urls, and whether a page follows
	const std::string series = R"({
		"content":[
			{"id":"SERIES1","name":"Paper Lanterns","booksCount":12,"deleted":false,
			 "metadata":{"title":"Paper Lanterns","titleSort":"paper lanterns","status":"ONGOING"}},
			{"id":"SERIES2","name":"","metadata":{"title":"Quiet Hours"}}
		],
		"page":0,"size":40,"totalElements":61,"totalPages":3
	})";
	RemoteList list;
	CHECK_EQ(remoteKomgaSeriesList(k.url, series, 0, list), true);
	CHECK_EQ(list.items.size(), (size_t)2);
	CHECK_EQ(list.items[0].key, std::string("SERIES1"));
	CHECK_EQ(list.items[1].title, std::string("Quiet Hours"));	// a nameless item shows its metadata title
	CHECK_EQ(list.items[0].cover, std::string("http://komga.lan:25600/api/v1/series/SERIES1/thumbnail"));
	CHECK_EQ(list.items[0].folder, false);
	CHECK_EQ(list.more, true);		// three pages, and this was the first of them
	CHECK_EQ(list.next, std::string(""));	// Komga pages by number: the caller asks for page 1
	CHECK_EQ(remoteKomgaSeriesList(k.url, series, 2, list), true);
	CHECK_EQ(list.more, false);		// the last page, fetched
	CHECK_EQ(remoteKomgaSeriesList(k.url, R"({"nope":1})", 0, list), false);
	CHECK_EQ(remoteKomgaSeriesList(k.url, "[]", 0, list), false);	// not one of Komga's paged answers

	// a page of books: chapters keyed by their ids, numbered, with the page count the media reports —
	// an object in older Komga's answers, an array of files in newer ones
	const std::string books = R"({
		"content":[
			{"id":"BOOK1","seriesId":"SERIES1","name":"Chapter 1 - First Light","number":1,"numberSort":1,
			 "media":{"size":1024,"pages":24,"mediaType":"IMAGE"}},
			{"id":"BOOK2","name":"Story Bridge","number":10.5,"numberSort":10.5,"media":[{"pagesCount":31}]},
			{"id":"BOOK3","name":"Chapter 7.5","metadata":{"number":"7.5"}}
		],
		"totalPages":2
	})";
	std::vector<RemoteChapter> chapters;
	bool more = false;
	CHECK_EQ(remoteKomgaChapterList(books, 0, chapters, more), true);
	CHECK_EQ(chapters.size(), (size_t)3);
	CHECK_EQ(chapters[0].key, std::string("BOOK1"));
	CHECK_EQ(chapters[0].name, std::string("Chapter 1 - First Light"));
	CHECK_EQ(chapters[0].number, 1.0);
	CHECK_EQ(chapters[0].pages, 24);
	CHECK_EQ(chapters[1].number, 10.5);
	CHECK_EQ(chapters[1].pages, 31);
	CHECK_EQ(chapters[2].number, 7.5);	// no sort value from the server: the number the name carries
	CHECK_EQ(chapters[2].pages, -1);	// and no media at all: unknown, not zero
	CHECK_EQ(more, true);
	CHECK_EQ(remoteKomgaChapterList(books, 1, chapters, more), true);
	CHECK_EQ(more, false);

	// an OPDS navigation feed: folders whose keys are absolute, whatever shape the hrefs came in, and the
	// feed's own next link
	const std::string nav = R"NAV(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <id>urn:opds:root</id>
  <title>The Lantern Shelf</title>
  <link rel="search" type="application/opensearchdescription+xml" href="/opds/search.xml"/>
  <link rel="next" type="application/atom+xml;profile=opds-catalog" href="/opds/shelves?page=2"/>
  <entry>
    <id>urn:shelf:comics</id>
    <title>Comics</title>
    <link rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation" href="opds/shelf/comics"/>
    <link rel="http://opds-spec.org/image" type="image/jpeg" href="opds/covers/comics.jpg"/>
  </entry>
  <entry>
    <id>urn:shelf:manga</id>
    <title>Manga</title>
    <link rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation" href="/opds/shelf/manga"/>
  </entry>
  <entry>
    <id>urn:about</id>
    <title>An entry that opens nothing</title>
  </entry>
</feed>
)NAV";
	RemoteList shelves;
	CHECK_EQ(remoteOpdsList("https://books.lan/opds", nav, shelves), true);
	CHECK_EQ(shelves.items.size(), (size_t)2);	// the entry with neither a way down nor a way to read is dropped
	CHECK_EQ(shelves.items[0].folder, true);
	CHECK_EQ(shelves.items[0].key, std::string("https://books.lan/opds/shelf/comics"));
	CHECK_EQ(shelves.items[0].cover, std::string("https://books.lan/opds/covers/comics.jpg"));
	CHECK_EQ(shelves.items[1].key, std::string("https://books.lan/opds/shelf/manga"));
	CHECK_EQ(shelves.items[1].cover, std::string(""));
	CHECK_EQ(shelves.more, true);
	CHECK_EQ(shelves.next, std::string("https://books.lan/opds/shelves?page=2"));
	CHECK_EQ(remoteOpdsList("https://books.lan/opds", "<html>the server's error page</html>", shelves), false);

	// an acquisition feed: a PSE chapter streams its pages, a plain one hands a whole CBZ, a shelf isn't one
	const std::string acq = R"ACQ(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:pse="http://vaemendis.net/opds-pse/ns">
  <id>urn:series:paperlanterns</id>
  <entry>
    <id>urn:chapter:1</id>
    <title>Chapter 1</title>
    <link rel="http://opds-spec.org/acquisition" type="application/vnd.comicbook+zip" href="/files/ch1.cbz"/>
    <link rel="http://vaemendis.net/opds-pse/stream" type="image/jpeg"
          href="/opds/series/7/ch/1/{pageNumber}?width={maxWidth}" pse:count="3"/>
  </entry>
  <entry>
    <id>urn:chapter:2</id>
    <title>Chapter 2 - The Night Market</title>
    <link rel="http://opds-spec.org/acquisition" type="application/vnd.comicbook+zip" href="files/ch2.cbz"/>
  </entry>
  <entry>
    <id>urn:series:other</id>
    <title>Another series</title>
    <link rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition" href="/opds/series/9"/>
  </entry>
</feed>
)ACQ";
	std::vector<RemoteChapter> ops;
	CHECK_EQ(remoteOpdsChapters("https://books.lan/opds/series/7", acq, ops), true);
	CHECK_EQ(ops.size(), (size_t)2);	// the shelf was skipped
	CHECK_EQ(ops[0].name, std::string("Chapter 1"));
	CHECK_EQ(ops[0].number, 1.0);
	CHECK_EQ(ops[0].pages, 3);	// the stream's own count
	CHECK_EQ(ops[0].key, std::string("https://books.lan/opds/series/7/ch/1/{pageNumber}?width={maxWidth}"));
	CHECK_EQ(ops[0].url, std::string("https://books.lan/files/ch1.cbz"));	// the file it can also be downloaded as
	std::vector<std::string> pageUrls = remoteOpdsPageUrls(ops[0], 1024);
	CHECK_EQ(pageUrls.size(), (size_t)3);	// PSE numbers its pages from 0
	CHECK_EQ(pageUrls[0], std::string("https://books.lan/opds/series/7/ch/1/0?width=1024"));
	CHECK_EQ(pageUrls[2], std::string("https://books.lan/opds/series/7/ch/1/2?width=1024"));
	CHECK_EQ(ops[1].url, std::string("https://books.lan/opds/series/files/ch2.cbz"));	// relative: resolved in the feed's own folder
	CHECK_EQ(ops[1].pages, -1);	// no stream: a whole file to download
	CHECK_EQ(remoteOpdsPageUrls(ops[1], 1024).size(), (size_t)0);
	CHECK_EQ(ops[1].number, 2.0);	// OPDS carries no number: the name is all there is
	// the same feed read as a list: chapters become items, and the shelf a folder
	RemoteList mixed;
	CHECK_EQ(remoteOpdsList("https://books.lan/opds/series/7", acq, mixed), true);
	CHECK_EQ(mixed.items.size(), (size_t)3);
	CHECK_EQ(mixed.items[0].folder, false);
	CHECK_EQ(mixed.items[0].key, std::string("https://books.lan/files/ch1.cbz"));
	CHECK_EQ(mixed.items[2].folder, true);
	CHECK_EQ(mixed.items[2].key, std::string("https://books.lan/opds/series/9"));

	// search: the OpenSearch description behind the feed's search link carries the query template, escaped
	// entities and all, and a parameter separator of "&" means a query with a "&" must not smuggle one in
	const std::string desc = R"OSC(<?xml version="1.0" encoding="utf-8"?>
<OpenSearchDescription xmlns="http://a9.com/-/spec/opensearch/1.1/">
  <ShortName>Lantern search</ShortName>
  <Url type="application/opensearchdescription+xml" rel="self" template="https://books.lan/opds/search.xml"/>
  <Url type="application/atom+xml" template="/opds/search?query={searchTerms}&amp;size=20"/>
</OpenSearchDescription>
)OSC";
	MangaServer op;
	op.kind = "opds";
	op.name = "Shelf OPDS";
	op.url = "https://books.lan/opds";
	op.user = "tony";
	RemoteRequest sq = remoteOpdsSearch(op, "hunter2", "https://books.lan/opds/search.xml", desc, "Night & Day");
	CHECK_EQ(sq.url, std::string("https://books.lan/opds/search?query=Night%20%26%20Day&size=20"));
	CHECK_EQ(hdr(sq, "Authorization"), std::string("Basic dG9ueTpodW50ZXIy"));
	CHECK_EQ(hdr(sq, "Accept"), std::string("application/atom+xml"));	// a feed of results, even from Kavita
	const std::string descPct =
		"<OpenSearchDescription><Url template=\"s?q=%7BsearchTerms%7D\"/></OpenSearchDescription>";
	CHECK_EQ(remoteOpdsSearch(op, "", "http://h/opds/search.xml", descPct, "a b").url,
	         std::string("http://h/opds/s?q=a%20b"));
	// a description that holds no query template is no search: the request asks for nothing
	CHECK_EQ(remoteOpdsSearch(op, "", "http://h/s.xml", "<OpenSearchDescription/>", "x").url, std::string(""));
}

// the comic servers the reader reads from: added, changed, removed and kept in a file; the password is sealed, so
// the file never holds it in plain text
static void testMangaServers()
{
	char tmpl[] = "/tmp/bp-manga-servers-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string root = made;
	std::string state = root + "/state";
	std::string file = state + "/manga/servers.tsv";

	MangaServerStore st;
	CHECK_EQ(st.load(state), true);		// nothing there yet: an empty store
	CHECK_EQ(st.list().size(), size_t(0));
	CHECK_EQ(MangaServerStore().load(""), false);	// nowhere to keep them
	int v = st.version();

	// a server added
	MangaServer k;
	k.kind = "komga";
	k.name = "Komga";
	k.url = "http://komga.local:25600";
	k.user = "reader";
	std::string kid = st.add(k);
	CHECK_EQ(st.version() > v, true);	// every change grows the version
	CHECK_EQ(kid.size(), (size_t)8);
	CHECK_EQ(kid.find_first_not_of("0123456789abcdef"), std::string::npos);
	CHECK_EQ(st.find("nope") == nullptr, true);
	const MangaServer* found = st.find(kid);
	CHECK_EQ(found != nullptr, true);
	if (found)
	{
		CHECK_EQ(found->kind, std::string("komga"));
		CHECK_EQ(found->name, std::string("Komga"));
		CHECK_EQ(found->url, std::string("http://komga.local:25600"));
		CHECK_EQ(found->user, std::string("reader"));
		CHECK_EQ(found->hasPassword(), false);
	}

	// the password is sealed, never kept in plain text
	v = st.version();
	CHECK_EQ(st.setPassword(kid, "s3cret"), true);
	CHECK_EQ(st.version() > v, true);
	CHECK_EQ(st.password(kid), std::string("s3cret"));
	CHECK_EQ(st.find(kid)->hasPassword(), true);
	CHECK_EQ(st.setPassword("nope", "x"), false);	// no such server
	CHECK_EQ(st.password("nope"), std::string(""));

	// a second one, and the list is by name
	MangaServer o;
	o.kind = "opds";
	o.name = "Aardvark OPDS";
	o.url = "https://books.example.org/opds";
	std::string oid = st.add(o);
	CHECK_EQ(oid.size(), (size_t)8);
	CHECK_EQ(st.list().size(), size_t(2));
	std::vector<MangaServer> ls = st.list();
	CHECK_EQ(ls[0].name, std::string("Aardvark OPDS"));
	CHECK_EQ(ls[1].name, std::string("Komga"));

	// changed
	v = st.version();
	MangaServer changed = *st.find(oid);
	changed.name = "Zebra OPDS";
	CHECK_EQ(st.update(changed), true);
	CHECK_EQ(st.version() > v, true);
	CHECK_EQ(st.list()[1].name, std::string("Zebra OPDS"));
	CHECK_EQ(st.find(oid)->name, std::string("Zebra OPDS"));
	// a change that knows nothing of the password doesn't throw it away
	CHECK_EQ(st.password(kid), std::string("s3cret"));
	CHECK_EQ(st.update(MangaServer()), false);	// no id, nothing to change

	// the kinds and the names we refuse
	MangaServer bad;
	bad.kind = "samba";
	bad.name = "NAS";
	bad.url = "http://nas.local";
	CHECK_EQ(st.add(bad), std::string(""));
	bad.kind = "komga";
	bad.name = "";
	CHECK_EQ(st.add(bad), std::string(""));
	bad.name = std::string(41, 'x');
	CHECK_EQ(st.add(bad), std::string(""));
	bad.name = std::string(40, 'x');
	std::string longName = st.add(bad);
	CHECK_EQ(longName.size(), (size_t)8);	// 40 characters is a name
	st.remove(longName);
	bad.name = "Tab\there";
	CHECK_EQ(st.add(bad), std::string(""));
	// the urls we refuse
	bad.name = "Bad url";
	for (const char* u : {"ftp://nas.local", "http://", "http://ho st/", "http://nas.local:port/", "/nas",
			      "nas.local", "http://na s.local"})
	{
		bad.url = u;
		CHECK_EQ(st.add(bad), std::string(""));
	}
	bad.url = "http://nas.local/" + std::string(2048 - 17, 'x');
	std::string longUrl = st.add(bad);
	CHECK_EQ(longUrl.size(), (size_t)8);	// 2048 characters is a url
	st.remove(longUrl);
	bad.url = "http://nas.local/" + std::string(2048 - 16, 'x');
	CHECK_EQ(st.add(bad), std::string(""));	// one more is too long
	CHECK_EQ(st.list().size(), size_t(2));	// none of them was kept

	// credentials in a url are taken out of it: they are never kept
	CHECK_EQ(isMangaServerUrl("http://reader:hunter2@nas.local/"), false);
	CHECK_EQ(withoutUrlCredentials("http://reader:hunter2@nas.local/x"), std::string("http://nas.local/x"));
	CHECK_EQ(withoutUrlCredentials("http://nas.local/x"), std::string("http://nas.local/x"));
	MangaServer c;
	c.kind = "kavita";
	c.name = "Kavita";
	c.url = "http://reader:hunter2@kavita.local:5000/kavita";
	std::string cid = st.add(c);
	CHECK_EQ(cid.size(), (size_t)8);
	const MangaServer* cs = st.find(cid);
	CHECK_EQ(cs != nullptr, true);
	if (cs)
	{
		CHECK_EQ(cs->url, std::string("http://kavita.local:5000/kavita"));
		CHECK_EQ(cs->url.find('@'), std::string::npos);
	}

	// saved and read back
	CHECK_EQ(st.save(), true);
	std::string text;
	{
		std::ifstream f(file);
		std::stringstream ss;
		ss << f.rdbuf();
		text = ss.str();
	}
	CHECK_EQ(text.find("s3cret") == std::string::npos, true);	// never in plain text
	CHECK_EQ(text.find("hunter2") == std::string::npos, true);	// and never in the url
	CHECK_EQ(text.find("v1:") != std::string::npos, true);	// the sealed one is there
	CHECK_EQ(text.find("komga.local:25600") != std::string::npos, true);
	MangaServerStore st2;
	CHECK_EQ(st2.load(state), true);
	CHECK_EQ(st2.list().size(), size_t(3));
	const MangaServer* back = st2.find(kid);
	CHECK_EQ(back != nullptr, true);
	if (back)
	{
		CHECK_EQ(back->kind, std::string("komga"));
		CHECK_EQ(back->name, std::string("Komga"));
		CHECK_EQ(back->url, std::string("http://komga.local:25600"));
		CHECK_EQ(back->user, std::string("reader"));
		CHECK_EQ(back->hasPassword(), true);
	}
	CHECK_EQ(st2.password(kid), std::string("s3cret"));	// and it opens again
	CHECK_EQ(st2.find(cid)->url, std::string("http://kavita.local:5000/kavita"));
	CHECK_EQ(st2.find(oid)->name, std::string("Zebra OPDS"));

	// a broken line is skipped; the rest of the file loads
	std::string broken = root + "/broken";
	mkdir(broken.c_str(), 0755);
	mkdir((broken + "/manga").c_str(), 0755);
	{
		std::ofstream f(broken + "/manga/servers.tsv");
		f << "# banana-player manga servers v1\n";
		f << "aaaa1111\tkomga\tKomga\thttp://komga.local:25600\treader\t\n";
		f << "this line has no fields\n";
		f << "bbbb2222\tmamba\tWrong kind\thttp://nas.local/\t\t\n";
		f << "cccc3333\tkomga\t\t\thttp://nas.local/\t\t\n";	// no name
		f << "dddd4444\tkomga\tWith creds\thttp://u:p@nas.local/\t\t\n";	// credentials in a url
		f << "eeee5555\topds\tExtra\thttps://books.example.org/opds\t\tcolumns\ta\tlater\n";	// a later version's columns
	}
	MangaServerStore st3;
	CHECK_EQ(st3.load(broken), true);
	CHECK_EQ(st3.list().size(), size_t(2));
	CHECK_EQ(st3.find("aaaa1111") != nullptr, true);
	CHECK_EQ(st3.find("bbbb2222") == nullptr, true);
	CHECK_EQ(st3.find("cccc3333") == nullptr, true);
	CHECK_EQ(st3.find("dddd4444") == nullptr, true);
	CHECK_EQ(st3.find("eeee5555") != nullptr, true);

	// removed
	v = st.version();
	st.remove(kid);
	CHECK_EQ(st.version() > v, true);
	CHECK_EQ(st.find(kid) == nullptr, true);
	CHECK_EQ(st.list().size(), size_t(2));
	MangaServerStore st4;
	CHECK_EQ(st4.load(state), true);
	CHECK_EQ(st4.list().size(), size_t(2));	// and it stays gone in the file

	// the folders are gone again
	unlink(file.c_str());
	unlink((state + "/manga/servers.key").c_str());
	rmdir((state + "/manga").c_str());
	rmdir(state.c_str());
	unlink((broken + "/manga/servers.tsv").c_str());
	unlink((broken + "/manga/servers.key").c_str());
	rmdir((broken + "/manga").c_str());
	rmdir(broken.c_str());
	rmdir(root.c_str());
}

// Kavita's api key: it lives sealed and nowhere else. An address holding it is split into {key} and the sealed
// password (and an old saved one is migrated on load), requests put it back only when they are built, and every
// answer has it turned back into {key} before anything reads it, so ids, hrefs and page urls never carry it
static void testMangaKavitaKey()
{
	char tmpl[] = "/tmp/bp-manga-kavita-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string root = made, state = root + "/state", file = state + "/manga/servers.tsv";
	auto readFile = [](const std::string& path)
	{
		std::ifstream f(path);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	};

	MangaServerStore st;
	CHECK_EQ(st.load(state), true);

	// an address as Kavita prints it (its own "Opds" spelling): the key comes out and {key} takes its place
	const std::string key = "9c3f0a61be";
	MangaServer k;
	k.kind = "kavita";
	k.name = "Kavita";
	k.url = "https://kavita.lan:5000/api/Opds/" + key + "/";
	std::string kid = st.add(k);
	CHECK_EQ(kid.size(), (size_t)8);
	const MangaServer* saved = st.find(kid);
	CHECK_EQ(saved != nullptr, true);
	if (saved)
	{
		CHECK_EQ(saved->url, std::string("https://kavita.lan:5000/api/Opds/{key}/"));
		CHECK_EQ(saved->url.find(key), std::string::npos);
		CHECK_EQ(saved->hasPassword(), true);
	}
	CHECK_EQ(st.password(kid), key);	// and it opens again as the sealed password
	std::string text = readFile(file);
	CHECK_EQ(text.find(key) == std::string::npos, true);	// never in plain text, never in the url
	CHECK_EQ(text.find("{key}") != std::string::npos, true);
	CHECK_EQ(text.find("v1:") != std::string::npos, true);

	// a key typed in the form's API key field, with an address that has none: Kavita's OPDS path is where it goes
	MangaServer t;
	t.kind = "kavita";
	t.name = "Typed key";
	t.url = "https://kavita.lan:5000";
	std::string tid = st.add(t);
	CHECK_EQ(tid.size(), (size_t)8);
	CHECK_EQ(st.find(tid)->url, std::string("https://kavita.lan:5000/api/opds/{key}"));
	CHECK_EQ(st.setPassword(tid, "typedkey123"), true);
	CHECK_EQ(st.password(tid), std::string("typedkey123"));
	// the endpoint without a key takes one too; any other address is left alone
	MangaServer e;
	e.kind = "kavita";
	e.name = "Endpoint";
	e.url = "https://kavita.lan:5000/api/opds/";
	std::string eid = st.add(e);
	CHECK_EQ(st.find(eid)->url, std::string("https://kavita.lan:5000/api/opds/{key}"));
	MangaServer other;
	other.kind = "kavita";
	other.name = "Elsewhere";
	other.url = "https://kavita.lan:5000/kavita";
	std::string oid = st.add(other);
	CHECK_EQ(st.find(oid)->url, std::string("https://kavita.lan:5000/kavita"));

	// requests: {key} becomes the opened key, only in the url, and never a header
	MangaServer req = *st.find(kid);
	std::string pw = st.password(kid);
	RemoteRequest rq = remoteAuth(req, pw, req.url);
	CHECK_EQ(rq.url, std::string("https://kavita.lan:5000/api/Opds/" + key + "/"));
	CHECK_EQ(rq.headers.size(), (size_t)1);
	CHECK_EQ(remoteOpdsStart(req, pw).url, rq.url);
	CHECK_EQ(remoteWithKey(req, pw, "https://h/api/opds/{key}/x"), std::string("https://h/api/opds/" + key + "/x"));
	MangaServer komga = req;
	komga.kind = "komga";
	CHECK_EQ(remoteWithKey(komga, pw, "https://h/{key}"), std::string("https://h/{key}"));	// another kind's url
	CHECK_EQ(remoteWithKey(req, "", "https://h/{key}"), std::string("https://h/{key}"));	// nothing to put in

	// every Kavita answer repeats the address, key and all: the key is {key} before a parser reads the body
	const std::string nav = R"NAV(<feed xmlns="http://www.w3.org/2005/Atom">
  <id>urn:kavita:opds:9c3f0a61be</id>
  <link rel="next" href="https://kavita.lan:5000/api/Opds/9c3f0a61be/libraries?page=2"/>
  <entry>
    <id>urn:kavita:series:11</id>
    <title>Paper Lanterns</title>
    <link rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"
          href="https://kavita.lan:5000/api/Opds/9c3f0a61be/series/11"/>
    <link rel="http://opds-spec.org/image" type="image/jpeg"
          href="https://kavita.lan:5000/api/Opds/9c3f0a61be/series/11/cover.jpg"/>
  </entry>
</feed>)NAV";
	std::string clean = remoteWithoutKey(req, pw, nav);
	CHECK_EQ(clean.find(key), std::string::npos);
	CHECK_EQ(clean.find("{key}") != std::string::npos, true);
	RemoteList list;
	CHECK_EQ(remoteOpdsList("https://kavita.lan:5000/api/Opds/{key}/", clean, list), true);
	CHECK_EQ(list.items.size(), (size_t)1);
	CHECK_EQ(list.items[0].key, std::string("https://kavita.lan:5000/api/Opds/{key}/series/11"));
	CHECK_EQ(list.items[0].cover, std::string("https://kavita.lan:5000/api/Opds/{key}/series/11/cover.jpg"));
	CHECK_EQ(list.next, std::string("https://kavita.lan:5000/api/Opds/{key}/libraries?page=2"));

	// a chapter's stream template keeps {key} too; the reader's page urls get the key back only when the request is
	// built (ui_manga_servers.cpp's chapter open does what remoteWithKey does here)
	const std::string acq = R"ACQ(<feed xmlns="http://www.w3.org/2005/Atom" xmlns:pse="http://vaemendis.net/opds-pse/ns">
  <entry>
    <id>urn:kavita:chapter:1</id>
    <title>Chapter 1</title>
    <link rel="http://vaemendis.net/opds-pse/stream" type="image/jpeg"
          href="https://kavita.lan:5000/api/Opds/9c3f0a61be/series/11/ch/1/{pageNumber}?width={maxWidth}" pse:count="2"/>
  </entry>
</feed>)ACQ";
	std::vector<RemoteChapter> chapters;
	CHECK_EQ(remoteOpdsChapters("https://kavita.lan:5000/api/Opds/{key}/series/11",
		remoteWithoutKey(req, pw, acq), chapters), true);
	CHECK_EQ(chapters.size(), (size_t)1);
	CHECK_EQ(chapters[0].key, std::string(
		"https://kavita.lan:5000/api/Opds/{key}/series/11/ch/1/{pageNumber}?width={maxWidth}"));
	std::vector<std::string> pages = remoteOpdsPageUrls(chapters[0], 640);
	CHECK_EQ(pages.size(), (size_t)2);
	CHECK_EQ(pages[0], std::string("https://kavita.lan:5000/api/Opds/{key}/series/11/ch/1/0?width=640"));
	CHECK_EQ(pages[0].find(key), std::string::npos);
	CHECK_EQ(remoteWithKey(req, pw, pages[0]),
		std::string("https://kavita.lan:5000/api/Opds/" + key + "/series/11/ch/1/0?width=640"));
	// the key never reaches a body that isn't Kavita's, and a body with no key to hide comes back as it came
	CHECK_EQ(remoteWithoutKey(komga, pw, nav), nav);
	CHECK_EQ(remoteWithoutKey(req, "", nav), nav);

	// an old saved server whose url still holds the key is migrated when the store loads: sealed, and written back
	std::string old = root + "/old";
	mkdir(old.c_str(), 0755);
	mkdir((old + "/manga").c_str(), 0755);
	{
		std::ofstream f(old + "/manga/servers.tsv");
		f << "# banana-player manga servers v1\n";
		f << "aaaa1111\tkavita\tOld Kavita\thttps://kavita.lan:5000/api/opds/0ldk3y/\t\t\n";
	}
	MangaServerStore migrated;
	CHECK_EQ(migrated.load(old), true);
	const MangaServer* m = migrated.find("aaaa1111");
	CHECK_EQ(m != nullptr, true);
	if (m)
	{
		CHECK_EQ(m->url, std::string("https://kavita.lan:5000/api/opds/{key}/"));
		CHECK_EQ(m->url.find("0ldk3y"), std::string::npos);
	}
	CHECK_EQ(migrated.password("aaaa1111"), std::string("0ldk3y"));
	std::string back = readFile(old + "/manga/servers.tsv");
	CHECK_EQ(back.find("0ldk3y") == std::string::npos, true);
	CHECK_EQ(back.find("{key}") != std::string::npos, true);
	CHECK_EQ(back.find("v1:") != std::string::npos, true);

	unlink(file.c_str());
	unlink((state + "/manga/servers.key").c_str());
	rmdir((state + "/manga").c_str());
	rmdir(state.c_str());
	unlink((old + "/manga/servers.tsv").c_str());
	unlink((old + "/manga/servers.key").c_str());
	rmdir((old + "/manga").c_str());
	rmdir(old.c_str());
	rmdir(root.c_str());
}

// bp.html for the source scripts (docs/design/manga.md 5.2): parse a page, find nodes by CSS, read them
static void testHtmlDoc()
{
	HtmlDoc doc;
	CHECK_EQ(doc.parse(
		"<!DOCTYPE html><html><head><title>Manga &amp; More</title>"
		"<style>.hide { display: none }</style></head><body>"
		"<div id='main' class='page'>"
		"  <div class='box'><ul class='list'>"
		"    <li class='item' data-src='a.jpg'><a href='/m/1'><b>One</b> &amp; more</a></li>"
		"    <li class='item'><a href='/m/2'>Two</a><img src='blank.gif' data-src='b.jpg'></li>"
		"    <li class='item hide'><a href='/m/3'>Three &lt;4&gt;</a></li>"
		"  </ul></div>"
		"  <script>var x = 1 < 2;</script>"
		"</div></body></html>"), true);

	// by tag, class, id, attribute; script and style are nodes like any other
	CHECK_EQ(doc.select("li").size(), size_t(3));
	CHECK_EQ(doc.select(".item").size(), size_t(3));
	CHECK_EQ(doc.select("li.hide").size(), size_t(1));
	CHECK_EQ(doc.select("#main").size(), size_t(1));
	CHECK_EQ(doc.select("[data-src]").size(), size_t(2));
	CHECK_EQ(doc.select("script").size(), size_t(1));
	CHECK_EQ(doc.select("style").size(), size_t(1));

	// descendant and child combinators: the li's are inside the div but are no div's children
	CHECK_EQ(doc.select("div li").size(), size_t(3));
	CHECK_EQ(doc.select("ul > li").size(), size_t(3));
	CHECK_EQ(doc.select("div > li").size(), size_t(0));
	CHECK_EQ(doc.select("li b").size(), size_t(1));
	// :nth-child; and a comma list finds each node once even when #main matches two of its parts
	CHECK_EQ(doc.select("li:nth-child(2)").size(), size_t(1));
	CHECK_EQ(doc.select("img, title").size(), size_t(2));
	CHECK_EQ(doc.select("#main, .list, #main").size(), size_t(2));

	// text(): the descendants' text joined, entities read, whitespace collapsed and trimmed
	CHECK_EQ(doc.selectFirst("li.item").text(), std::string("One & more"));
	CHECK_EQ(doc.selectFirst("b").text(), std::string("One"));
	CHECK_EQ(doc.selectFirst("li.hide").text(), std::string("Three <4>"));
	CHECK_EQ(doc.selectFirst("title").text(), std::string("Manga & More"));

	// attr(): present and absent, the name matched as HTML matches it; tag() names the element
	CHECK_EQ(doc.selectFirst("li.item").attr("data-src"), std::string("a.jpg"));
	CHECK_EQ(doc.selectFirst("a").attr("href"), std::string("/m/1"));
	CHECK_EQ(doc.selectFirst("a").attr("title"), std::string(""));
	CHECK_EQ(doc.selectFirst("img").attr("SRC"), std::string("blank.gif"));
	CHECK_EQ(doc.selectFirst("img").tag(), std::string("img"));

	// html(): outerHTML back as written (lexbor's serializer uses double quotes and keeps the escape)
	CHECK_EQ(doc.selectFirst("a").html(), std::string("<a href=\"/m/1\"><b>One</b> &amp; more</a>"));
	// a void element: attributes in their written order, no closing tag
	CHECK_EQ(doc.selectFirst("img").html(), std::string("<img src=\"blank.gif\" data-src=\"b.jpg\">"));

	// a node's select searches only inside it, never returning the node itself
	HtmlNode main = doc.selectFirst("#main");
	CHECK_EQ(main.valid(), true);
	CHECK_EQ(main.select("li").size(), size_t(3));
	CHECK_EQ(main.select("#main").size(), size_t(0));
	HtmlNode li1 = doc.selectFirst("li.item");
	CHECK_EQ(li1.select("a").size(), size_t(1));
	CHECK_EQ(li1.select("img").size(), size_t(0));	// the img lives in the next li
	CHECK_EQ(li1.selectFirst("b").text(), std::string("One"));

	// a broken selector, and an empty selector, are nothing found, not a crash
	CHECK_EQ(doc.select("div[[").size(), size_t(0));
	CHECK_EQ(doc.select("##").size(), size_t(0));
	CHECK_EQ(doc.select("").size(), size_t(0));
	CHECK_EQ(doc.selectFirst("###").valid(), false);

	// empty input parses and holds nothing; a default handle answers instead of throwing
	HtmlDoc empty;
	CHECK_EQ(empty.parse(""), true);
	CHECK_EQ(empty.select("div").size(), size_t(0));
	CHECK_EQ(empty.selectFirst("div").valid(), false);
	HtmlNode none;
	CHECK_EQ(none.valid(), false);
	CHECK_EQ(none.text(), std::string(""));
	CHECK_EQ(none.attr("href"), std::string(""));
	CHECK_EQ(none.html(), std::string(""));
	CHECK_EQ(none.select("div").size(), size_t(0));

	// malformed HTML parses as browsers do: p closes before li, crossed tags are fixed up
	HtmlDoc bad;
	CHECK_EQ(bad.parse("<div class='x'><p>text<li>item</div>"), true);
	CHECK_EQ(bad.select("p").size(), size_t(1));
	CHECK_EQ(bad.select("li").size(), size_t(1));
	CHECK_EQ(bad.selectFirst("div.x > li").text(), std::string("item"));
	CHECK_EQ(bad.parse("<div><span></div><span>"), true);	// the re-parse also drops the old tree, cleanly
	CHECK_EQ(bad.select("div span").size(), size_t(1));

	// the caps: over 8 MB in is refused whole (the document stays usable), one select returns at most 20000
	HtmlDoc cap;
	CHECK_EQ(cap.parse(std::string(8u * 1024u * 1024u + 1, 'x')), false);
	std::string many;
	for (int i = 0; i < 20005; i++) many += "<div></div>";
	CHECK_EQ(cap.parse(many), true);
	CHECK_EQ(cap.select("div").size(), size_t(20000));
	CHECK_EQ(cap.selectFirst("div").valid(), true);
}

// the page cache: a page goes in and comes back, the least recently used goes first over the cap, and the index
// survives a restart
static void testPageCache()
{
	char tmpl[] = "/tmp/bp-pagecache-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string root = made;
	std::string dir = root + "/state/manga/cache";
	const uint8_t five[5] = {1, 2, 3, 4, 5};
	std::vector<uint8_t> ten(10, 0x5A);

	{
		// the cache folder is made when it isn't there
		PageCache c;
		CHECK_EQ(c.open(dir, 1000), true);
		CHECK_EQ(c.count(), size_t(0));
		CHECK_EQ(c.size(), (uint64_t)0);

		// a page goes in and comes back
		CHECK_EQ(c.put("https://example.com/one.jpg", five, sizeof five), true);
		std::vector<uint8_t> out;
		CHECK_EQ(c.get("https://example.com/one.jpg", out), true);
		std::vector<uint8_t> want(five, five + sizeof five);
		CHECK_EQ(out == want, true);
		CHECK_EQ(c.count(), size_t(1));
		CHECK_EQ(c.size(), (uint64_t)sizeof five);
		CHECK_EQ(c.has("https://example.com/one.jpg"), true);
		CHECK_EQ(c.has("https://example.com/two.jpg"), false);
		CHECK_EQ(c.get("https://example.com/two.jpg", out), false);

		// the path is the same every time and stays inside the folder: the name is the key's hash
		std::string p = c.pathFor("https://example.com/one.jpg");
		CHECK_EQ(p, c.pathFor("https://example.com/one.jpg"));
		CHECK_EQ(p.find(dir + "/") == 0, true);
		CHECK_EQ(p.find("..") == std::string::npos, true);
		CHECK_EQ(p.substr(dir.size() + 1, 2).size(), size_t(2));	// the fan-out folder
		CHECK_EQ(p[dir.size() + 3], '/');
		CHECK_EQ(p.substr(p.find_last_of('/') + 1).size(), size_t(20));	// 16 hex characters and ".bin"
		CHECK_EQ(access(p.c_str(), F_OK), 0);

		// the index holds no key: a page's URL can carry a credential (Kavita's API key), so only its digest is kept
		const uint8_t one[] = {1};
		CHECK_EQ(c.put("https://kavita.example/api/opds/SECRETKEY123/page/1", one, sizeof one), true);
		CHECK_EQ(c.save(), true);
		std::vector<uint8_t> idx;
		FILE* f = fopen((dir + "/index.tsv").c_str(), "rb");
		CHECK_EQ(f != nullptr, true);
		if (f)
		{
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) idx.insert(idx.end(), buf, buf + n);
			fclose(f);
		}
		std::string idxText(idx.begin(), idx.end());
		CHECK_EQ(idxText.find("SECRETKEY123"), std::string::npos);
		CHECK_EQ(idxText.find("example.com"), std::string::npos);
		CHECK_EQ(c.has("https://kavita.example/api/opds/SECRETKEY123/page/1"), true);
	}

	{
		// the least recently used page goes first, and a page that is asked for is safe for a while
		PageCache c;
		CHECK_EQ(c.open(root + "/lru", 30), true);
		CHECK_EQ(c.put("a", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("b", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("c", ten.data(), ten.size()), true);
		std::vector<uint8_t> out;
		CHECK_EQ(c.get("a", out), true);	// a is used now
		CHECK_EQ(c.has("b"), true);		// b is used now too: c is the oldest
		CHECK_EQ(c.put("d", ten.data(), ten.size()), true);
		CHECK_EQ(c.count(), size_t(3));
		CHECK_EQ(c.size(), (uint64_t)30);
		CHECK_EQ(c.has("c"), false);
		CHECK_EQ(c.has("a"), true);
		CHECK_EQ(c.has("b"), true);
		CHECK_EQ(c.has("d"), true);
	}

	{
		// the cap holds over many pages
		PageCache c;
		CHECK_EQ(c.open(root + "/many", 1000), true);
		std::vector<uint8_t> page(100, 0x33);
		for (int i = 0; i < 50; i++)
		{
			std::string key = "page-" + std::to_string(i);
			CHECK_EQ(c.put(key, page.data(), page.size()), true);
		}
		CHECK_EQ(c.size(), (uint64_t)1000);
		CHECK_EQ(c.count(), size_t(10));
		CHECK_EQ(c.has("page-0"), false);	// the oldest went
		CHECK_EQ(c.has("page-49"), true);

		// a smaller cap gives the oldest pages up at once
		c.setLimit(300);
		CHECK_EQ(c.size(), (uint64_t)300);
		CHECK_EQ(c.count(), size_t(3));

		// a page bigger than the whole cap is refused: it would never fit
		std::vector<uint8_t> big(400, 0x44);
		CHECK_EQ(c.put("big", big.data(), big.size()), false);
		CHECK_EQ(c.has("big"), false);
		CHECK_EQ(c.size(), (uint64_t)300);
		CHECK_EQ(c.count(), size_t(3));

		// the cache empties
		c.clear();
		CHECK_EQ(c.count(), size_t(0));
		CHECK_EQ(c.size(), (uint64_t)0);
		CHECK_EQ(c.has("page-49"), false);
		CHECK_EQ(access(c.pathFor("page-49").c_str(), F_OK), -1);
	}

	{
		// another cache keeps its pages
		PageCache c;
		CHECK_EQ(c.open(root + "/keep", 1000), true);
		CHECK_EQ(c.put("one", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("two", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("three", ten.data(), ten.size()), true);
	}
	{
		PageCache c;
		CHECK_EQ(c.open(root + "/keep", 1000), true);
		CHECK_EQ(c.count(), size_t(3));
		CHECK_EQ(c.size(), (uint64_t)30);
		CHECK_EQ(access(c.pathFor("one").c_str(), F_OK), 0);
		CHECK_EQ(access(c.pathFor("three").c_str(), F_OK), 0);
	}
	{
		// the index is where it should be, with a line per page
		std::ifstream in((root + "/keep/index.tsv").c_str());
		std::string line;
		int lines = 0;
		while (std::getline(in, line))
			if (!line.empty() && line[0] != '#') lines++;
		CHECK_EQ(lines, 3);
	}

	{
		// the order the pages were used in comes back with the index
		PageCache c;
		CHECK_EQ(c.open(root + "/order", 1000), true);
		CHECK_EQ(c.put("one", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("two", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("three", ten.data(), ten.size()), true);
		std::vector<uint8_t> out;
		CHECK_EQ(c.get("two", out), true);	// two is used now: one is the oldest
		CHECK_EQ(c.save(), true);
	}
	{
		PageCache c;
		CHECK_EQ(c.open(root + "/order", 20), true);
		CHECK_EQ(c.count(), size_t(2));
		CHECK_EQ(c.has("one"), false);	// the oldest page went first again
		CHECK_EQ(c.has("two"), true);
		CHECK_EQ(c.has("three"), true);
	}

	{
		// a page whose file is gone is dropped when the cache opens again
		PageCache c;
		CHECK_EQ(c.open(root + "/gone", 1000), true);
		CHECK_EQ(c.put("a", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("b", ten.data(), ten.size()), true);
		CHECK_EQ(c.save(), true);
		unlink(c.pathFor("a").c_str());
	}
	{
		PageCache c;
		CHECK_EQ(c.open(root + "/gone", 1000), true);
		CHECK_EQ(c.count(), size_t(1));
		CHECK_EQ(c.size(), (uint64_t)10);
		CHECK_EQ(c.has("a"), false);
		CHECK_EQ(c.has("b"), true);
	}

	{
		// a damaged index is rebuilt from the files
		PageCache c;
		CHECK_EQ(c.open(root + "/corrupt", 1000), true);
		CHECK_EQ(c.put("x", ten.data(), ten.size()), true);
		CHECK_EQ(c.put("y", five, sizeof five), true);
		CHECK_EQ(c.save(), true);
	}
	{
		FILE* f = fopen((root + "/corrupt/index.tsv").c_str(), "wb");
		if (f)
		{
			fputs("this is not an index\n", f);
			fclose(f);
		}
		PageCache c;
		CHECK_EQ(c.open(root + "/corrupt", 1000), true);
		CHECK_EQ(c.count(), size_t(2));	// the pages are counted all the same
		CHECK_EQ(c.size(), (uint64_t)(ten.size() + sizeof five));
		CHECK_EQ(c.has("x"), true);		// the name is the key's hash: the page is found anyway
		std::vector<uint8_t> out;
		CHECK_EQ(c.get("y", out), true);
		CHECK_EQ(out.size(), sizeof five);
	}

	std::string cmd = "rm -rf '" + root + "'";
	if (system(cmd.c_str()) != 0) fprintf(stderr, "could not remove %s\n", root.c_str());
}

// the cover cache: the same PageCache the reader's pages use, opened at <state>/manga/cache/covers with a 50 MB cap.
// the pure parts are PageCache's own (key from URL via fnv1a, LRU eviction); this test names the cover-cache-specific
// contract: a URL is the key, two different URLs land in different files, and a cap that fits only a few covers
// gives the oldest up
static void testCoverCache()
{
	char tmpl[] = "/tmp/bp-covercache-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string root = made;
	std::string dir = root + "/covers";
	// two covers the reader might fetch: one small, one larger
	std::vector<uint8_t> small(1024, 0xAA);
	std::vector<uint8_t> large(100 * 1024, 0xBB);
	const uint64_t cap = 250 * 1024;	// 250 KB: fits two large, not three

	{
		PageCache c;
		CHECK_EQ(c.open(dir, cap), true);

		// a URL as key: the same URL gives the same path every time (fnv1a over the bytes)
		std::string url1 = "https://cdn.example.org/covers/series-42.jpg?v=3";
		std::string url2 = "https://cdn.example.org/covers/series-99.png";
		CHECK_EQ(c.pathFor(url1), c.pathFor(url1));
		CHECK_EQ(c.pathFor(url1) != c.pathFor(url2), true);
		CHECK_EQ(c.pathFor(url1).find(dir + "/") == 0, true);
		CHECK_EQ(c.pathFor(url1).find("..") == std::string::npos, true);

		// roundtrip: bytes in, bytes back, exactly
		CHECK_EQ(c.put(url1, small.data(), small.size()), true);
		std::vector<uint8_t> out;
		CHECK_EQ(c.get(url1, out), true);
		CHECK_EQ(out == small, true);
		CHECK_EQ(c.has(url1), true);
		CHECK_EQ(c.has(url2), false);
		CHECK_EQ(c.count(), size_t(1));
		CHECK_EQ(c.size(), (uint64_t)small.size());

		// the cap holds: three large covers over a 250 KB cap gives the oldest up
		CHECK_EQ(c.put("https://a/1", large.data(), large.size()), true);
		CHECK_EQ(c.put("https://a/2", large.data(), large.size()), true);
		CHECK_EQ(c.count(), size_t(3));		// the small one, then the two large
		CHECK_EQ(c.put("https://a/3", large.data(), large.size()), true);
		CHECK_EQ(c.size() <= cap, true);
		CHECK_EQ(c.has(url1), false);	// the small one from before is the oldest: gone
		CHECK_EQ(c.has("https://a/1"), false);	// the first large is the next oldest: gone too
		CHECK_EQ(c.has("https://a/2"), true);
		CHECK_EQ(c.has("https://a/3"), true);

		// a cover bigger than the whole cap is refused (it would empty the cache for itself)
		std::vector<uint8_t> huge(cap + 1, 0xCC);
		CHECK_EQ(c.put("https://huge/x", huge.data(), huge.size()), false);
		CHECK_EQ(c.has("https://huge/x"), false);

		// the order survives a restart through save/open (the index on disk)
		CHECK_EQ(c.get("https://a/2", out), true);	// 2 is used now: 3 is the oldest
		CHECK_EQ(c.save(), true);
	}
	{
		// reopen with a smaller cap: only one large cover fits, so the oldest (3) is evicted
		PageCache c;
		CHECK_EQ(c.open(dir, 150 * 1024), true);
		CHECK_EQ(c.count(), size_t(1));
		CHECK_EQ(c.has("https://a/3"), false);	// the oldest went at the smaller cap
		CHECK_EQ(c.has("https://a/2"), true);	// the one just used survives
	}

	std::string cmd = "rm -rf '" + root + "'";
	if (system(cmd.c_str()) != 0) fprintf(stderr, "could not remove %s\n", root.c_str());
}

// the downloads queue: the order, pause per item and for all, the card's low space, and the TSV that lets a quit resume
// at the next page not written
static void testDlQueue()
{
	char tmpl[] = "/tmp/bp-dlq-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string root = made;
	std::string state = root + "/state";

	auto mk = [](const std::string& series, const std::string& chapter, int pages) {
		DlItem it;
		it.seriesId = series;
		it.chapterId = chapter;
		it.id = series + "\n" + chapter;	// the key the queue and the page use
		it.seriesTitle = "Series " + series;
		it.chapterTitle = "Chapter " + chapter;
		it.source = "komga/shelf";
		it.added = 1000;
		it.target = "/data/Manga/" + series + "/" + chapter + ".cbz";
		for (int i = 0; i < pages; i++)
			it.pageUrls.push_back("https://example.org/" + series + "/" + chapter + "/" + std::to_string(i + 1) + ".jpg");
		return it;
	};

	DownloadQueue q;
	CHECK_EQ(q.load(state), true);	// no file yet: an empty queue
	CHECK_EQ(q.items().size(), size_t(0));
	CHECK_EQ(q.next() == nullptr, true);
	CHECK_EQ(q.pendingPages(), 0);
	CHECK_EQ(DownloadQueue().load(""), false);	// nowhere to read from
	uint64_t serial = q.serial();

	DlItem a = mk("s1", "c1", 3), b = mk("s1", "c2", 2), c = mk("s2", "c1", 4);
	q.add(a);
	CHECK_EQ(q.serial() > serial, true);	// every change grows the serial
	serial = q.serial();
	q.add(b);
	q.add(c);
	CHECK_EQ(q.items().size(), size_t(3));
	CHECK_EQ(q.serial() > serial, true);

	// the key the two ids make when the caller leaves it out
	DlItem noid = mk("s3", "c9", 1);
	noid.id.clear();
	q.add(noid);
	CHECK_EQ(q.items().size(), size_t(4));
	CHECK_EQ(q.items()[3].id, std::string("s3\nc9"));
	q.remove("s3\nc9");
	CHECK_EQ(q.items().size(), size_t(3));

	// a duplicate id is one item, whatever else it carries
	DlItem dup = a;
	dup.chapterTitle = "Chapter 1 (again)";
	serial = q.serial();
	q.add(dup);
	CHECK_EQ(q.items().size(), size_t(3));
	CHECK_EQ(q.items()[0].chapterTitle, a.chapterTitle);
	CHECK_EQ(q.serial(), serial);	// nothing changed, so nothing redraws

	// the first waiting one, in the order they were added
	CHECK_EQ(q.next() != nullptr, true);
	if (q.next()) CHECK_EQ(q.next()->id, a.id);

	// pausing one takes it out of the running without touching the others
	q.pause(b.id);
	CHECK_EQ(q.items()[1].state, DlItem::Paused);
	CHECK_EQ(q.next() && q.next()->id == a.id, true);

	q.started(a.id);
	CHECK_EQ(q.items()[0].state, DlItem::Running);
	CHECK_EQ(q.next() && q.next()->id == c.id, true);	// A running, B paused

	// all paused: nothing starts; resuming all gives the queue back, and B stays paused on its own
	q.pauseAll();
	CHECK_EQ(q.allPaused(), true);
	CHECK_EQ(q.next() == nullptr, true);
	q.resumeAll();
	CHECK_EQ(q.allPaused(), false);
	CHECK_EQ(q.next() && q.next()->id == c.id, true);
	CHECK_EQ(q.items()[1].state, DlItem::Paused);
	// resuming one chapter while all are paused means downloads running again
	q.pauseAll();
	q.resume(b.id);
	CHECK_EQ(q.allPaused(), false);
	CHECK_EQ(q.next() && q.next()->id == b.id, true);

	// the card under 500 MB: nothing starts, the one running keeps going
	serial = q.serial();
	q.setLowSpace(true);
	CHECK_EQ(q.next() == nullptr, true);
	CHECK_EQ(q.serial() > serial, true);
	q.pageDone(a.id, 100);
	CHECK_EQ(q.items()[0].done, 1);
	CHECK_EQ(q.items()[0].bytes, (uint64_t)100);
	CHECK_EQ(q.pendingPages(), 2 + 2 + 4);	// A's other two, B's two, C's four
	q.setLowSpace(false);
	CHECK_EQ(q.next() != nullptr, true);

	// pages counted in order; a failure keeps them, and a retry resumes at the next one
	q.pageDone(a.id, 50);
	CHECK_EQ(q.items()[0].done, 2);
	CHECK_EQ(q.items()[0].bytes, (uint64_t)150);
	q.pageDone(b.id, 10);
	q.failed(b.id, "network gone");
	CHECK_EQ(q.items()[1].state, DlItem::Failed);
	CHECK_EQ(q.items()[1].done, 1);
	CHECK_EQ(q.items()[1].error, std::string("network gone"));
	CHECK_EQ(q.next() && q.next()->id == c.id, true);	// a failed one doesn't run by itself
	q.retry(b.id);
	CHECK_EQ(q.items()[1].state, DlItem::Waiting);
	CHECK_EQ(q.items()[1].done, 1);	// where it stopped
	CHECK_EQ(q.items()[1].error, std::string(""));
	CHECK_EQ(q.next() && q.next()->id == b.id, true);
	CHECK_EQ(q.pendingPages(), 1 + 1 + 4);

	// finished: out of the queue's work, still in memory for the page until clearDone()
	q.finished(a.id);
	CHECK_EQ(q.items()[0].state, DlItem::Done);
	CHECK_EQ(q.items().size(), size_t(3));
	CHECK_EQ(q.pendingPages(), 1 + 4);
	CHECK_EQ(q.next() && q.next()->id == b.id, true);

	// a chapter under way when the player quits comes back waiting
	q.started(c.id);
	CHECK_EQ(q.items()[2].state, DlItem::Running);
	CHECK_EQ(q.save(), true);

	// the file: the header, the two unfinished chapters, and no Done one
	{
		std::ifstream f(state + "/manga/downloads-queue.tsv");
		std::string line;
		std::getline(f, line);
		CHECK_EQ(line, std::string("# banana-player manga downloads-queue v1"));
		std::string rest, l;
		while (std::getline(f, l)) rest += l + "\n";
		CHECK_EQ(rest.find(a.target) == std::string::npos, true);
		CHECK_EQ(rest.find(b.target) != std::string::npos, true);
	}

	// saved and read back: every field, in the queue's order, Running as Waiting
	DownloadQueue q2;
	CHECK_EQ(q2.load(state), true);
	std::vector<DlItem> its = q2.items();
	CHECK_EQ(its.size(), size_t(2));
	if (its.size() == 2)
	{
		CHECK_EQ(its[0].id, b.id);
		CHECK_EQ(its[0].seriesId, b.seriesId);
		CHECK_EQ(its[0].seriesTitle, b.seriesTitle);
		CHECK_EQ(its[0].chapterId, b.chapterId);
		CHECK_EQ(its[0].chapterTitle, b.chapterTitle);
		CHECK_EQ(its[0].source, b.source);
		CHECK_EQ(its[0].pageUrls, b.pageUrls);
		CHECK_EQ(its[0].done, 1);
		CHECK_EQ(its[0].bytes, (uint64_t)10);
		CHECK_EQ(its[0].added, b.added);
		CHECK_EQ(its[0].state, DlItem::Waiting);
		CHECK_EQ(its[0].target, b.target);
		CHECK_EQ(its[1].id, c.id);
		CHECK_EQ(its[1].state, DlItem::Waiting);	// was Running: a quit mid-chapter resumes
		CHECK_EQ(its[1].done, 0);
		CHECK_EQ(its[1].pageUrls, c.pageUrls);
	}
	CHECK_EQ(q2.next() && q2.next()->id == b.id, true);

	// every field through the file, with tabs and newlines in titles, errors and URLs
	DownloadQueue q3;
	CHECK_EQ(q3.load(root + "/odd"), true);
	DlItem odd;
	odd.seriesId = "odd\t1";
	odd.chapterId = "ch\n2";
	odd.id = odd.seriesId + "\n" + odd.chapterId;
	odd.seriesTitle = "Tab\there\nNew line\\Back";
	odd.chapterTitle = "Ch\t2\naway";
	odd.source = "src\twith\ttabs";
	odd.pageUrls = {"https://example.org/a\tb.jpg", "https://example.org/c\nd.jpg", "https://example.org/e\\f.jpg"};
	odd.done = 1;
	odd.state = DlItem::Failed;
	odd.error = "gone\taway\nand back";
	odd.added = 1234567890123LL;
	odd.bytes = 9876543210ULL;
	odd.target = "/data/Manga/odd\ttarget\n.cbz";
	q3.add(odd);
	q3.failed(odd.id, odd.error);	// add() starts clean; the error comes from the worker's report
	CHECK_EQ(q3.save(), true);
	DownloadQueue q4;
	CHECK_EQ(q4.load(root + "/odd"), true);
	std::vector<DlItem> oddBack = q4.items();
	CHECK_EQ(oddBack.size(), size_t(1));
	if (oddBack.size() == 1)
	{
		const DlItem& r = oddBack[0];
		CHECK_EQ(r.id, odd.id);
		CHECK_EQ(r.seriesId, odd.seriesId);
		CHECK_EQ(r.seriesTitle, odd.seriesTitle);
		CHECK_EQ(r.chapterId, odd.chapterId);
		CHECK_EQ(r.chapterTitle, odd.chapterTitle);
		CHECK_EQ(r.source, odd.source);
		CHECK_EQ(r.pageUrls, odd.pageUrls);	// each one back as it went in, the tab and the newline too
		CHECK_EQ(r.done, 1);
		CHECK_EQ(r.state, DlItem::Failed);
		CHECK_EQ(r.error, odd.error);
		CHECK_EQ(r.added, odd.added);
		CHECK_EQ(r.bytes, odd.bytes);
		CHECK_EQ(r.target, odd.target);
	}
	// a retry after the load resumes at the page it stopped on
	q4.retry(odd.id);
	CHECK_EQ(q4.items()[0].state, DlItem::Waiting);
	CHECK_EQ(q4.items()[0].done, 1);

	// a broken line in the file is skipped; the rest of the queue loads
	{
		mkdir((root + "/bad").c_str(), 0755);
		mkdir((root + "/bad/manga").c_str(), 0755);
		std::ofstream f(root + "/bad/manga/downloads-queue.tsv");
		f << "# banana-player manga downloads-queue v1\n";
		f << "g1\ts\tGood\ts\tOne\tsrc\twaiting\t0\t0\t100\t\tt1\thttps://x/1.jpg\n";
		f << "this line has no tabs at all\n";
		f << "g2\ts\tGood\ts\tTwo\tsrc\trunning\t1\t5\t200\t\t\thttps://x/2.jpg\n";
		f << "g3\ts\tGood\ts\tThree\tsrc\tsleeping\t0\t0\t300\t\t\thttps://x/3.jpg\n";	// no such state
		f << "g4\ts\tGood\ts\tFour\tsrc\twaiting\t9\t0\t400\t\t\thttps://x/4.jpg\n";	// done past its pages
		f << "g5\ts\tGood\ts\tFive\tsrc\twaiting\t0\tx\t500\t\t\thttps://x/5.jpg\n";	// bytes that aren't a number
		f << "g6\ts\tGood\ts\tSix\n";	// too few columns
	}
	DownloadQueue qb;
	CHECK_EQ(qb.load(root + "/bad"), true);
	CHECK_EQ(qb.items().size(), size_t(2));
	if (qb.items().size() == 2)
	{
		CHECK_EQ(qb.items()[0].id, std::string("g1"));
		CHECK_EQ(qb.items()[1].id, std::string("g2"));
		CHECK_EQ(qb.items()[1].state, DlItem::Waiting);	// running in the file
		CHECK_EQ(qb.items()[1].done, 1);
		CHECK_EQ(qb.items()[1].pageUrls.size(), size_t(1));
	}

	// Done items leave memory only when the page says so
	serial = q.serial();
	q.clearDone();
	CHECK_EQ(q.items().size(), size_t(2));
	CHECK_EQ(q.serial() > serial, true);
	CHECK_EQ(q.items()[0].id, b.id);

	unlink((state + "/manga/downloads-queue.tsv").c_str());
	rmdir((state + "/manga").c_str());
	rmdir(state.c_str());
	unlink((root + "/odd/manga/downloads-queue.tsv").c_str());
	rmdir((root + "/odd/manga").c_str());
	rmdir((root + "/odd").c_str());
	unlink((root + "/bad/manga/downloads-queue.tsv").c_str());
	rmdir((root + "/bad/manga").c_str());
	rmdir((root + "/bad").c_str());
	rmdir(root.c_str());
}

// the repository index (design 5.3): reading it, validating its sources, checksums, versions, resolving file URLs
static void testMangaRepo()
{
	static const std::string SHA = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";	// "abc"

	// a valid index in the design's exact format: relative file and icon, api as a number, nsfw as a bool; an
	// escaped quote in a name reads back whole; one source with an impossible id and one with a broken version
	// are skipped and named, the rest kept
	std::string good = R"JSON({
		"format": 1,
		"name": "Test repository",
		"homepage": "https://repos.example.org",
		"sources": [
			{ "id": "ember-reader", "name": "Ember Reader", "lang": "en", "version": "1.2.0", "api": 1, "nsfw": false,
			  "file": "sources/ember-reader.lua", "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
			  "size": 18342, "icon": "icons/ember-reader.png", "description": "One line." },
			{ "id": "tide.scanner", "name": "Tide \"Scanner\"", "lang": "pt-br", "version": "0.9", "api": "1", "nsfw": true,
			  "file": "sources/tide.lua", "sha256": "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD" },
			{ "id": "sky-manga", "name": "Sky Manga", "lang": "all", "version": "2",
			  "file": "https://cdn.example.net/sources/sky.lua", "sha256": "0000000000000000000000000000000000000000000000000000000000000000" },
			{ "id": "BAD_ID!", "name": "Broken", "lang": "en", "version": "1.0", "file": "sources/broken.lua", "sha256": "9f2c" },
			{ "id": "late", "name": "Late", "lang": "en", "version": "1.2.x", "file": "sources/late.lua", "sha256": "9f2c" }
		]
	})JSON";
	RepoIndex idx;
	std::string err;
	CHECK_EQ(parseRepoIndex(good, idx, err), true);
	CHECK_EQ(idx.name, std::string("Test repository"));
	CHECK_EQ(idx.homepage, std::string("https://repos.example.org"));
	CHECK_EQ(idx.formatVersion, 1);
	CHECK_EQ(idx.sources.size(), (size_t)3);	// the two broken ones skipped
	CHECK_EQ(err.find("skipped source 4 (BAD_ID!)") != std::string::npos, true);
	CHECK_EQ(err.find("skipped source 5 (late)") != std::string::npos, true);
	CHECK_EQ(idx.sources[0].id, std::string("ember-reader"));
	CHECK_EQ(idx.sources[0].url, std::string("sources/ember-reader.lua"));	// as the index writes it, relative
	CHECK_EQ(idx.sources[0].icon, std::string("icons/ember-reader.png"));
	CHECK_EQ(idx.sources[0].minApp, std::string("1"));	// the api number kept as text
	CHECK_EQ(idx.sources[0].adult, false);
	CHECK_EQ(idx.sources[0].sha256, SHA);
	CHECK_EQ(idx.sources[1].name, std::string("Tide \"Scanner\""));
	CHECK_EQ(idx.sources[1].adult, true);
	CHECK_EQ(idx.sources[1].lang, std::string("pt-br"));
	CHECK_EQ(idx.sources[2].url, std::string("https://cdn.example.net/sources/sky.lua"));
	CHECK_EQ(idx.sources[2].minApp, std::string(""));	// no api in the index
	CHECK_EQ(idx.sources[2].icon, std::string(""));

	// an index with a format this build doesn't read is refused whole (design 5.2's rule for a newer script api)
	CHECK_EQ(parseRepoIndex("{\"format\": 2, \"name\": \"x\", \"sources\": []}", idx, err), false);
	CHECK_EQ(err.find("format") != std::string::npos, true);
	// format missing reads as 1
	CHECK_EQ(parseRepoIndex("{\"name\": \"x\", \"sources\": []}", idx, err), true);
	CHECK_EQ(idx.formatVersion, 1);

	// malformed JSON false, in every way it can be malformed
	for (const char* bad : {"", "{", "{\"format\": 1, \"sources\": [", "not json", "{} {}", "true", "[1,2]",
						 "{\"format\": 1}", "{\"format\": \"x\", \"sources\": []}"})
		CHECK_EQ(parseRepoIndex(bad, idx, err), false);

	// the caps: 1 MB of input (design 5.3), 2000 sources
	CHECK_EQ(parseRepoIndex(std::string(1024 * 1024 + 1, 'x'), idx, err), false);
	std::string many = "{\"format\": 1, \"sources\": [";
	for (int k = 0; k < 2001; k++)
	{
		if (k) many += ",";
		many += "{\"id\":\"s" + std::to_string(k) + "\",\"name\":\"S\",\"lang\":\"en\",\"version\":\"1\",\"file\":\"s.lua\",\"sha256\":\"" + SHA + "\"}";
	}
	CHECK_EQ(parseRepoIndex(many + "]}", idx, err), false);
	CHECK_EQ(err.find("2000") != std::string::npos, true);
	// exactly 2000 fits
	std::string just2000 = "{\"format\": 1, \"sources\": [";
	for (int k = 0; k < 2000; k++)
	{
		if (k) just2000 += ",";
		just2000 += "{\"id\":\"s" + std::to_string(k) + "\",\"name\":\"S\",\"lang\":\"en\",\"version\":\"1\",\"file\":\"s.lua\",\"sha256\":\"" + SHA + "\"}";
	}
	CHECK_EQ(parseRepoIndex(just2000 + "]}", idx, err), true);
	CHECK_EQ(idx.sources.size(), (size_t)2000);

	// the design's repositories are HTTPS (5.3): an absolute http file is skipped; and an icon with a space in it
	CHECK_EQ(parseRepoIndex("{\"format\":1,\"sources\":[{\"id\":\"a\",\"name\":\"A\",\"lang\":\"en\",\"version\":\"1\",\"file\":\"http://cdn.example/a.lua\",\"sha256\":\"" + SHA + "\"}]}", idx, err), true);
	CHECK_EQ(idx.sources.size(), (size_t)0);
	CHECK_EQ(err.find("not https") != std::string::npos, true);

	// SHA-256 over bytes: the FIPS test vector for "abc" and for the empty string
	CHECK_EQ(sha256Hex((const uint8_t*)"abc", 3), SHA);
	CHECK_EQ(sha256Hex((const uint8_t*)"", 0), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(sha256Hex(nullptr, 0), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(sha256Hex(nullptr, 5), std::string(""));	// bytes claimed but none given
	// the index's digest, upper or lower case
	CHECK_EQ(sha256Matches(SHA, (const uint8_t*)"abc", 3), true);
	CHECK_EQ(sha256Matches("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", (const uint8_t*)"abc", 3), true);
	CHECK_EQ(sha256Matches(SHA, (const uint8_t*)"abd", 3), false);
	CHECK_EQ(sha256Matches("ba7816bf", (const uint8_t*)"abc", 3), false);	// a truncated expected never matches

	// versions compare numerically per part (update.h's rule, shared with the app's own updater)
	CHECK_EQ(compareVersions("1.10", "1.9"), 1);
	CHECK_EQ(compareVersions("1.9", "1.10"), -1);
	CHECK_EQ(compareVersions("1.2.0", "1.2.0"), 0);
	CHECK_EQ(compareVersions("1.2", "1.2.0"), 0);
	CHECK_EQ(compareVersions("0.5.10", "0.5.9"), 1);
	CHECK_EQ(updateAvailable("1.2.0", "1.3.0"), true);	// the index is higher: show Update
	CHECK_EQ(updateAvailable("1.3.0", "1.3.0"), false);
	CHECK_EQ(updateAvailable("1.3.0", "1.2.9"), false);
	CHECK_EQ(updateAvailable("1.9", "1.10"), true);

	// resolving a source's file against the index URL (design 5.3: relative to it)
	CHECK_EQ(repoResolve("https://repos.example.org/main/index.json", "sources/ember.lua"),
		     std::string("https://repos.example.org/main/sources/ember.lua"));
	CHECK_EQ(repoResolve("https://repos.example.org/main/index.json", "/icons/tide.png"),
		     std::string("https://repos.example.org/icons/tide.png"));
	CHECK_EQ(repoResolve("https://repos.example.org/main/index.json", "https://cdn.example.net/a.lua"),
		     std::string("https://cdn.example.net/a.lua"));	// absolute stays
	CHECK_EQ(repoResolve("https://repos.example.org/main/index.json", "../shared/a.lua"),
		     std::string("https://repos.example.org/shared/a.lua"));
	CHECK_EQ(repoResolve("https://repos.example.org/main/index.json", "./a.lua"),
		     std::string("https://repos.example.org/main/a.lua"));
	CHECK_EQ(repoResolve("https://repos.example.org", "a.lua"), std::string("https://repos.example.org/a.lua"));
	CHECK_EQ(repoResolve("https://h/dir/index.json?v=3", "a.lua"), std::string("https://h/dir/a.lua"));	// the index's query is its own
	CHECK_EQ(repoResolve("https://h/dir/index.json", "a.lua?x=1"), std::string("https://h/dir/a.lua?x=1"));
	CHECK_EQ(repoResolve("http://h/x/index.json", "a.lua"), std::string("http://h/x/a.lua"));
	CHECK_EQ(repoResolve("https://h/index.json", ""), std::string("https://h/index.json"));

	// the shape of a URL this code will hold: http(s), a host, no credentials, no spaces or controls, ≤ 2048
	for (const char* ok : {"https://repos.example.org/index.json", "http://192.168.1.5/index.json",
			       "https://h:8443/a?q=1#frag", "HTTPS://UPPER.example/x"})
		CHECK_EQ(validRepoUrl(ok), true);
	std::string tooLong = "https://h/" + std::string(2048 - 10, 'a');	// exactly 2048 fits ("https://h/" is 10)
	CHECK_EQ(validRepoUrl(tooLong), true);
	tooLong += "a";
	for (const char* no : {"", "https://", "https:///x", "ftp://host/x", "https://user:pass@host/x",
			       "https://host name/x", "https:/x", "not-a-url", "https://a\nb"})
		CHECK_EQ(validRepoUrl(no), false);
	CHECK_EQ(validRepoUrl(tooLong), false);
}

// the pure server readers: OPDS 1.2 Atom feeds and Komga's REST list answers (docs/design/manga.md 5.1)
static void testOpds()
{
	// a Kavita-style navigation feed: prefixed Atom (the prefixes must not matter), a comment, entities, CDATA, an
	// escaped-html summary, and the paging links
	const std::string nav = R"NAV(<?xml version="1.0" encoding="utf-8"?>
<!-- served by Kavita -->
<k:feed xmlns:k="http://www.w3.org/2005/Atom" xmlns:thr="http://purl.org/syndicate/threading">
  <k:id>urn:kavita:opds:libraries</k:id>
  <k:title>The &amp; Catalog</k:title>
  <k:link rel="self" type="application/atom+xml" href="https://srv.example:5000/api/opds/my-library"/>
  <k:link rel="start" type="application/atom+xml;profile=opds-catalog" href="/api/opds"/>
  <k:link rel="search" type="application/opensearchdescription+xml" href="/api/opds/searchdescription.xml"/>
  <k:link rel="next" type="application/atom+xml;profile=opds-catalog" href="/api/opds/my-library?page=2"/>
  <k:link rel="up" type="application/atom+xml;profile=opds-catalog" href="/api/opds"/>
  <k:entry>
    <k:id>urn:kavita:series:11</k:id>
    <k:title>Golden &amp; Silver &#x2014; Vol. 1</k:title>
    <k:author><k:name>Sun &amp; Moon Studio</k:name></k:author>
    <k:updated>2026-11-02T08:00:00Z</k:updated>
    <k:summary>&lt;p&gt;Two &lt;b&gt;siblings&lt;/b&gt;   and a   lantern.&lt;/p&gt;</k:summary>
    <k:link rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition" href="/api/opds/series/11" thr:count="42"/>
    <k:link rel="http://opds-spec.org/image" type="image/jpeg" href="/api/opds/series/11/cover"/>
  </k:entry>
  <k:entry>
    <k:id>urn:kavita:series:12</k:id>
    <k:title><![CDATA[Quiet Hours & Night Shifts]]></k:title>
    <k:content type="html">&lt;div&gt;&lt;p&gt;A   tale &amp; a tail.&lt;/p&gt;&lt;/div&gt;</k:content>
    <k:link rel="http://opds-spec.org/image/thumbnail" type="image/jpeg" href="covers/12.jpg"/>
  </k:entry>
</k:feed>
)NAV";
	OpdsFeed feed;
	CHECK_EQ(parseOpds(nav, feed), true);
	CHECK_EQ(feed.id, std::string("urn:kavita:opds:libraries"));
	CHECK_EQ(feed.title, std::string("The & Catalog"));
	CHECK_EQ(feed.start, std::string("/api/opds"));
	CHECK_EQ(feed.search, std::string("/api/opds/searchdescription.xml"));
	CHECK_EQ(feed.next, std::string("/api/opds/my-library?page=2"));
	CHECK_EQ(feed.up, std::string("/api/opds"));
	CHECK_EQ(feed.entries.size(), (size_t)2);
	const OpdsEntry& e = feed.entries[0];
	CHECK_EQ(e.id, std::string("urn:kavita:series:11"));
	CHECK_EQ(e.title, std::string("Golden & Silver \xE2\x80\x94 Vol. 1"));
	CHECK_EQ(e.author, std::string("Sun & Moon Studio"));
	CHECK_EQ(e.updated, std::string("2026-11-02T08:00:00Z"));
	CHECK_EQ(e.summary, std::string("Two siblings and a lantern."));	// tags stripped, entities decoded, spaces closed up
	CHECK_EQ(e.links.size(), (size_t)2);
	CHECK_EQ(e.links[0].count, 42);	// thr:count
	CHECK_EQ(e.links[1].count, -1);
	CHECK_EQ(e.nav(), std::string("/api/opds/series/11"));
	CHECK_EQ(e.cover(), std::string("/api/opds/series/11/cover"));
	CHECK_EQ(e.thumbnail(), std::string(""));
	CHECK_EQ(e.acquisition(), std::string(""));
	CHECK_EQ(e.pse(), std::string(""));
	const OpdsEntry& e2 = feed.entries[1];
	CHECK_EQ(e2.title, std::string("Quiet Hours & Night Shifts"));	// CDATA is literal
	CHECK_EQ(e2.summary, std::string("A tale & a tail."));	// from <content type="html">
	CHECK_EQ(e2.thumbnail(), std::string("covers/12.jpg"));
	// every href resolves against the feed's own URL
	CHECK_EQ(opdsResolve("https://srv.example:5000/api/opds/my-library", e2.thumbnail()),
	         std::string("https://srv.example:5000/api/opds/covers/12.jpg"));
	CHECK_EQ(opdsResolve("https://srv.example:5000/api/opds/my-library", feed.next),
	         std::string("https://srv.example:5000/api/opds/my-library?page=2"));

	// an acquisition feed with the Page Streaming Extension, written with a default namespace and a different prefix
	// for PSE: what Komga answers for a series' books
	const std::string acq = R"ACQ(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:p="http://vaemendis.net/opds-pse/ns">
  <id>urn:komga:series:7:books</id>
  <title>Books of Paper Lanterns</title>
  <link rel="start" type="application/atom+xml;profile=opds-catalog" href="/opds"/>
  <entry>
    <id>urn:komga:book:21</id>
    <title>Chapter One</title>
    <link rel="item" type="application/atom+xml" href="books/21"/>
    <link rel="http://opds-spec.org/acquisition" type="application/pdf" href="/files/21.pdf"/>
    <link rel="http://opds-spec.org/acquisition/cbz" type="application/vnd.comicbook+zip" href="/files/21.cbz"/>
    <link rel="http://vaemendis.net/opds-pse/stream" type="image/jpeg"
          href="/api/series/7/books/21/page/{pageNumber}?width={maxWidth}" p:count="37"/>
  </entry>
  <entry>
    <id>urn:komga:book:22</id>
    <title>Chapter Two</title>
    <link rel="http://opds-spec.org/acquisition" type="application/zip" href="/files/22.zip"/>
    <link rel="http://opds-spec.org/image" type="image/png" href="img/22.png"/>
  </entry>
</feed>
)ACQ";
	OpdsFeed shop;
	CHECK_EQ(parseOpds(acq, shop), true);
	CHECK_EQ(shop.start, std::string("/opds"));
	CHECK_EQ(shop.entries.size(), (size_t)2);
	const OpdsEntry& b = shop.entries[0];
	CHECK_EQ(b.nav(), std::string("books/21"));
	// a comic on a shelf is a zip: the cbz beats the pdf that came first
	CHECK_EQ(b.acquisition(), std::string("/files/21.cbz"));
	CHECK_EQ(b.pse(), std::string("/api/series/7/books/21/page/{pageNumber}?width={maxWidth}"));
	int pseCount = -1;
	for (const OpdsLink& l : b.links)
		if (l.rel == "http://vaemendis.net/opds-pse/stream") pseCount = l.count;
	CHECK_EQ(pseCount, 37);	// p:count, matched by namespace URI not prefix
	CHECK_EQ(shop.entries[1].acquisition(), std::string("/files/22.zip"));
	CHECK_EQ(shop.entries[1].cover(), std::string("img/22.png"));
	CHECK_EQ(shop.entries[1].pse(), std::string(""));
	std::string pageUrl = opdsResolve("https://komga.example/opds/v1/series/7", b.pse());
	CHECK_EQ(pageUrl, std::string("https://komga.example/api/series/7/books/21/page/{pageNumber}?width={maxWidth}"));
	CHECK_EQ(opdsPseUrl(pageUrl, 12, 900),
	         std::string("https://komga.example/api/series/7/books/21/page/12?width=900"));
	CHECK_EQ(opdsPseUrl("/s/21/%7BpageNumber%7D?w=%7BmaxWidth%7D", 3, 640), std::string("/s/21/3?w=640"));

	// opdsResolve, one case per shape a server actually writes
	CHECK_EQ(opdsResolve("http://h:80/opds/library", "http://cdn.x/a.jpg"), std::string("http://cdn.x/a.jpg"));
	CHECK_EQ(opdsResolve("http://h:80/opds/library", "sub/x"), std::string("http://h:80/opds/sub/x"));
	CHECK_EQ(opdsResolve("http://h:80/opds/library", "../x"), std::string("http://h:80/x"));
	CHECK_EQ(opdsResolve("http://h:80/opds/library", "a/../b"), std::string("http://h:80/opds/b"));
	CHECK_EQ(opdsResolve("http://h:80/opds/library", "/api/x"), std::string("http://h:80/api/x"));
	CHECK_EQ(opdsResolve("http://h:80/opds/library?a=1&b=2", "?c=3"), std::string("http://h:80/opds/library?c=3"));
	CHECK_EQ(opdsResolve("http://h:80/opds/", "x"), std::string("http://h:80/opds/x"));
	CHECK_EQ(opdsResolve("http://h:80", "x"), std::string("http://h:80/x"));
	CHECK_EQ(opdsResolve("http://h:80/opds/library", "//cdn.x/y"), std::string("http://cdn.x/y"));
	CHECK_EQ(opdsResolve("http://h:80/opds/library", ""), std::string("http://h:80/opds/library"));

	// Kavita's reading-state marks before an entry's title go (the font has no glyph for them); a title of marks stays
	{
		OpdsFeed k;
		CHECK_EQ(parseOpds("<feed xmlns=\"http://www.w3.org/2005/Atom\"><id>f</id><title>F</title>"
			"<entry><id>1</id><title>\xE2\x97\x91 Saga - Chapter 1</title></entry>"
			"<entry><id>2</id><title>\xE2\xAD\x98 Saga - Chapter 2</title></entry>"
			"<entry><id>3</id><title>\xE2\xAC\xA4  Saga - Chapter 3</title></entry>"
			"<entry><id>4</id><title>\xE2\x97\x91</title></entry>"
			"<entry><id>5</id><title>Plain \xE2\x97\x91 inside</title></entry></feed>", k), true);
		CHECK_EQ(k.entries.size(), (size_t)5);
		CHECK_EQ(k.entries[0].title, std::string("Saga - Chapter 1"));
		CHECK_EQ(k.entries[1].title, std::string("Saga - Chapter 2"));
		CHECK_EQ(k.entries[2].title, std::string("Saga - Chapter 3"));
		CHECK_EQ(k.entries[3].title, std::string("\xE2\x97\x91"));
		CHECK_EQ(k.entries[4].title, std::string("Plain \xE2\x97\x91 inside"));
		// an id given twice (Kavita's Continue Reading copy first): the later one stays, in its place
		OpdsFeed d;
		CHECK_EQ(parseOpds("<feed xmlns=\"http://www.w3.org/2005/Atom\"><id>f</id><title>F</title>"
			"<entry><id>2</id><title>Continue Reading from: Ch 1</title></entry>"
			"<entry><id>2</id><title>Ch 1</title></entry><entry><id>1</id><title>Ch 2</title></entry>"
			"<entry><title>no id</title></entry><entry><title>no id</title></entry></feed>", d), true);
		CHECK_EQ(d.entries.size(), (size_t)4);
		CHECK_EQ(d.entries[0].title, std::string("Ch 1"));
		CHECK_EQ(d.entries[1].title, std::string("Ch 2"));
	}

	// not a feed, in every way a body can fail to be one
	OpdsFeed no;
	CHECK_EQ(parseOpds("", no), false);
	CHECK_EQ(parseOpds("no xml at all", no), false);
	CHECK_EQ(parseOpds("<rss><channel><title>RSS, not Atom</title></channel></rss>", no), false);
	CHECK_EQ(parseOpds(R"(<ComicInfo><Series>S</Series></ComicInfo>)", no), false);
	// truncated input answers false at every cut, and crashes at none (stop short of the feed's own close)
	for (size_t cut = 4; cut + 12 < nav.size(); cut += 37)
	{
		OpdsFeed part;
		CHECK_EQ(parseOpds(nav.substr(0, cut), part), false);
	}
	// an empty feed is still a feed
	OpdsFeed tiny;
	CHECK_EQ(parseOpds(R"(<feed xmlns="http://www.w3.org/2005/Atom" xmlns:t="http://www.w3.org/2005/Atom"/>)", tiny),
	         true);
	CHECK_EQ(tiny.entries.size(), (size_t)0);
	// over 8 MB is refused rather than cut short
	CHECK_EQ(parseOpds(std::string((8u << 20) + 1, 'x'), no), false);
	// and a runaway feed is capped at 10 000 entries
	std::string many = R"(<feed xmlns="http://www.w3.org/2005/Atom">)";
	for (int i = 0; i < 10001; i++) many += "<entry><id>x" + std::to_string(i) + "</id></entry>";
	many += "</feed>";
	OpdsFeed capped;
	CHECK_EQ(parseOpds(many, capped), true);
	CHECK_EQ(capped.entries.size(), (size_t)10000);

	// Komga REST: a page of series, with its extra fields (booksCount, flags) unknown but harmless
	std::vector<KomgaItem> items;
	int totalPages = -1;
	const std::string kgSeries = R"JSON({"content":[
	  {"id":"01HXYZ","name":"Paper Lanterns","numberSort":1.0,"booksCount":12,
	   "metadata":{"title":"Paper Lanterns","status":"ONGOING","authors":[{"name":"Sun & Moon Studio"}]}},
	  {"id":"01JABC","metadata":{"title":"Quiet Hours"}}
	],"page":0,"pageSize":2,"totalPages":3,"totalElements":5,"empty":false,"first":true,"last":false,"filtered":false})JSON";
	CHECK_EQ(parseKomgaPage(kgSeries, items, totalPages), true);
	CHECK_EQ(totalPages, 3);
	CHECK_EQ(items.size(), (size_t)2);
	CHECK_EQ(items[0].id, std::string("01HXYZ"));
	CHECK_EQ(items[0].name, std::string("Paper Lanterns"));
	CHECK_EQ(items[0].number, std::string(""));
	CHECK_EQ(items[0].numberSort, 1.0);
	CHECK_EQ(items[0].pages, -1);
	CHECK_EQ(items[1].name, std::string("Quiet Hours"));	// no name on the item: the metadata title stands in

	// a page of books: metadata.number/numberSort, the media's page count (object form in old Komga, array in new),
	// JSON string escapes incl. a surrogate pair
	items.clear();
	const std::string kgBooks = R"JSON({"content":[
	  {"id":"01BOOK1","name":"Book One","metadata":{"number":"1","numberSort":1.5},
	   "media":{"pagesCount":24,"size":1000000,"format":"CBZ"}},
	  {"id":"01BOOK2","name":"Ch\u00e9ri \uD83D\uDE00 Two","metadata":{"number":"1.5","summary":"a \"quoted\" tale"},
	   "media":[{"pages":38,"width":800,"height":1200,"biTDepth":8,"colorType":"COLOR"}]}
	],"page":0,"pageSize":20,"totalPages":1,"totalElements":2,"empty":false})JSON";
	CHECK_EQ(parseKomgaPage(kgBooks, items, totalPages), true);
	CHECK_EQ(totalPages, 1);
	CHECK_EQ(items.size(), (size_t)2);
	CHECK_EQ(items[0].number, std::string("1"));
	CHECK_EQ(items[0].numberSort, 1.5);
	CHECK_EQ(items[0].pages, 24);
	CHECK_EQ(items[1].number, std::string("1.5"));
	CHECK_EQ(items[1].numberSort, -1.0);
	CHECK_EQ(items[1].pages, 38);
	CHECK_EQ(items[1].name, std::string("Ch\xC3\xA9ri \xF0\x9F\x98\x80 Two"));

	// malformed JSON is false without a crash; items untouched
	std::vector<KomgaItem> keep;
	int t2 = 0;
	CHECK_EQ(parseKomgaPage("{", keep, t2), false);
	CHECK_EQ(parseKomgaPage("not json", keep, t2), false);
	CHECK_EQ(parseKomgaPage("[1,2]", keep, t2), false);	// not an object
	CHECK_EQ(parseKomgaPage("{}", keep, t2), false);	// no content array
	CHECK_EQ(parseKomgaPage(R"({"content":"oops"})", keep, t2), false);
	CHECK_EQ(parseKomgaPage(R"({"content":[{"id":)", keep, t2), false);
	CHECK_EQ(parseKomgaPage(R"({"content":[{"broken]")", keep, t2), false);
	CHECK_EQ(parseKomgaPage(std::string("{\"content\":[[") + std::string(400, '[') + std::string(400, ']') + "]]}", keep, t2),
	         false);	// nested deeper than the reader trusts
	CHECK_EQ(parseKomgaPage(std::string((8u << 20) + 1, 'x'), keep, t2), false);
	CHECK_EQ(keep.size(), (size_t)0);
	// the cap is shared with feeds
	std::string manyKg = "{\"content\":[";
	for (int i = 0; i < 10001; i++) manyKg += i ? ",{\"id\":\"x\"}" : "{\"id\":\"x\"}";
	manyKg += "],\"totalPages\":1}";
	std::vector<KomgaItem> manyItems;
	CHECK_EQ(parseKomgaPage(manyKg, manyItems, t2), true);
	CHECK_EQ(manyItems.size(), (size_t)10000);

	// joined without doubling or dropping the slash, however the user typed the server's address
	CHECK_EQ(komgaUrl("https://komga.example", "/api/v1/series"), std::string("https://komga.example/api/v1/series"));
	CHECK_EQ(komgaUrl("https://komga.example/", "/api/v1/series"), std::string("https://komga.example/api/v1/series"));
	CHECK_EQ(komgaUrl("https://komga.example/", "api/v1/series"), std::string("https://komga.example/api/v1/series"));
	CHECK_EQ(komgaUrl("https://komga.example//", "api/v1"), std::string("https://komga.example/api/v1"));
	CHECK_EQ(komgaUrl("https://komga.example/", ""), std::string("https://komga.example"));
	CHECK_EQ(komgaUrl("", "/api/v1/books"), std::string("/api/v1/books"));
}

// the CBZ writer: what a download becomes on the card, read back with the reader that will open it
static void testCbzWrite()
{
	const std::string P = std::string(BP_TESTS_DIR) + "/pages/";
	const std::string jpgP = P + "page.jpg", pngP = P + "page.png";
	auto file = [](const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};
	const std::vector<uint8_t> jpg = file(jpgP), png = file(pngP);
	CHECK_EQ(jpg.empty(), false);
	CHECK_EQ(png.empty(), false);
	std::vector<uint8_t> out;

	char tmpl[] = "/tmp/bp-cbzwrite-XXXXXX";
	char* made = mkdtemp(tmpl);
	CHECK_EQ(made != nullptr, true);
	if (!made) return;
	const std::string D = made;

	ComicInfo c;
	c.series = "Test Series";
	c.number = "3";
	c.volume = 1;
	c.title = "Chapter Three";
	c.pageCount = 3;
	c.rightToLeft = true;
	c.summary = "A page & a half";
	c.sourceId = "komga";
	c.seriesKey = "abc-123";
	const std::string xml = writeComicInfo(c);

	// a chapter: three pages added with addFile and the metadata with add; a stale .part must not survive
	const std::string chapter = D + "/ch.cbz";
	{ std::ofstream stale(chapter + ".part"); stale << "half a download from a crashed run"; }
	{
		CbzWriter w;
		CHECK_EQ(w.addFile("001.jpg", jpgP), false);	// nothing open yet
		CHECK_EQ(w.error().empty(), false);
		CHECK_EQ(w.open(chapter), true);
		CHECK_EQ(w.open(chapter), false);			// one archive at a time
		CHECK_EQ(w.addFile("001.jpg", jpgP), true);
		CHECK_EQ(w.addFile("002.png", pngP), true);
		CHECK_EQ(w.addFile("003.jpg", jpgP), true);
		CHECK_EQ(w.add("ComicInfo.xml", (const uint8_t*)xml.data(), xml.size()), true);
		CHECK_EQ(w.add("001.jpg", png.data(), png.size()), false);	// a name already in the archive
		CHECK_EQ(w.error().empty(), false);
		CHECK_EQ(w.add("../escape.jpg", png.data(), png.size()), false);	// would leave the folder
		CHECK_EQ(w.add("/abs.jpg", png.data(), png.size()), false);
		CHECK_EQ(w.add("a\\b.jpg", png.data(), png.size()), false);
		CHECK_EQ(w.add("", png.data(), png.size()), false);
		CHECK_EQ(w.addFile("nope.jpg", D + "/no-such-file"), false);	// a source that isn't there
		CHECK_EQ(w.error().empty(), false);
		CHECK_EQ(w.finish(), true);
		CHECK_EQ(w.add("late.jpg", png.data(), png.size()), false);	// finished: the archive is closed
	}

	// reads back with the same reader a chapter uses: byte-identical entries, stored not deflated
	Cbz z;
	CHECK_EQ(z.open(chapter), true);
	CHECK_EQ(z.error().empty(), true);
	CHECK_EQ(z.entries().size(), (size_t)4);
	CHECK_EQ(z.pages(), (std::vector<std::string>{"001.jpg", "002.png", "003.jpg"}));
	for (const CbzEntry& e : z.entries())
	{
		CHECK_EQ(e.method, (uint32_t)0);
		CHECK_EQ(e.packed, e.size);	// stored: nothing was compressed away
	}
	CHECK_EQ(z.read("001.jpg", out) && out == jpg, true);
	CHECK_EQ(z.read("002.png", out) && out == png, true);
	CHECK_EQ(z.read("003.jpg", out) && out == jpg, true);
	CHECK_EQ(z.read("ComicInfo.xml", out) && std::string(out.begin(), out.end()) == xml, true);
	ComicInfo c2;
	CHECK_EQ(parseComicInfo(std::string(out.begin(), out.end()), c2), true);
	CHECK_EQ(c2.series, std::string("Test Series"));
	CHECK_EQ(c2.number, std::string("3"));
	CHECK_EQ(c2.volume, 1);
	CHECK_EQ(c2.title, std::string("Chapter Three"));
	CHECK_EQ(c2.pageCount, 3);
	CHECK_EQ(c2.rightToLeft, true);
	CHECK_EQ(c2.summary, std::string("A page & a half"));
	CHECK_EQ(c2.sourceId, std::string("komga"));
	CHECK_EQ(c2.seriesKey, std::string("abc-123"));
	z.close();
	// the .part was renamed into place: no half chapter is left behind
	{ std::ifstream t(chapter + ".part"); CHECK_EQ(t.good(), false); }

	// a UTF-8 name: bit 11 set on it and not on the ASCII one, and it reads back as it was written
	{
		const std::string jp = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88 page.jpg";	// "Test page.jpg"
		CbzWriter w;
		CHECK_EQ(w.open(D + "/utf.cbz"), true);
		CHECK_EQ(w.add("001.jpg", jpg.data(), jpg.size()), true);
		CHECK_EQ(w.add(jp, jpg.data(), jpg.size()), true);
		CHECK_EQ(w.finish(), true);
		// the second entry's local header follows the first's (30 bytes + a 7-byte name) and its data, and
		// the flags sit 6 in from the signature
		const std::vector<uint8_t> raw = file(D + "/utf.cbz");
		const size_t second = 30 + 7 + jpg.size();
		CHECK_EQ(raw[second] == 'P' && raw[second + 1] == 'K' && raw[second + 2] == 3 && raw[second + 3] == 4, true);
		CHECK_EQ(raw[6] | (raw[7] << 8), 0);
		CHECK_EQ(raw[second + 6] | (raw[second + 7] << 8), 0x800);
		CHECK_EQ((int)raw[second + 30], 0xE3);	// the raw UTF-8 name starts here
		Cbz u;
		CHECK_EQ(u.open(D + "/utf.cbz"), true);
		CHECK_EQ(u.entries().size(), (size_t)2);
		CHECK_EQ(u.pages(), (std::vector<std::string>{"001.jpg", jp}));	// the extension past the Japanese counts
		CHECK_EQ(u.read(jp, out) && out == jpg, true);
		CHECK_EQ(u.read("001.jpg", out) && out == jpg, true);
	}

	// abort() leaves neither the archive nor its .part, and a dropped writer aborts by itself
	{
		CbzWriter w;
		CHECK_EQ(w.open(D + "/aborted.cbz"), true);
		CHECK_EQ(w.addFile("001.jpg", jpgP), true);
		w.abort();
		std::ifstream a(D + "/aborted.cbz"), b(D + "/aborted.cbz.part");
		CHECK_EQ(a.good(), false);
		CHECK_EQ(b.good(), false);
		CHECK_EQ(w.open(D + "/left.cbz"), true);	// reusable after an abort
		CHECK_EQ(w.add("001.jpg", jpg.data(), jpg.size()), true);
	}	// w goes out of scope half-written
	{ std::ifstream b(D + "/left.cbz.part"); CHECK_EQ(b.good(), false); }
	{ std::ifstream a(D + "/left.cbz"); CHECK_EQ(a.good(), false); }

	// the entry cap: a chapter never has this many, and the reader would refuse them (no ZIP64)
	{
		CbzWriter w;
		CHECK_EQ(w.open(D + "/cap.cbz"), true);
		for (int k = 0; k < 10000; k++)
			CHECK_EQ(w.add(std::to_string(k) + ".jpg", nullptr, 0), true);
		CHECK_EQ(w.add("10000.jpg", nullptr, 0), false);
		CHECK_EQ(w.error().empty(), false);
		w.abort();
	}

	// an empty archive is a valid one: Cbz opens it and finds no pages
	{
		CbzWriter w;
		CHECK_EQ(w.open(D + "/empty.cbz"), true);
		CHECK_EQ(w.finish(), true);
		Cbz e;
		CHECK_EQ(e.open(D + "/empty.cbz"), true);
		CHECK_EQ(e.entries().empty(), true);
		CHECK_EQ(e.pages().empty(), true);
	}

	for (const char* n : {"ch.cbz", "utf.cbz", "cap.cbz", "empty.cbz"}) std::remove((D + "/" + n).c_str());
	rmdir(D.c_str());
}

// the manga pages' pure decisions: a generated cover's colours, what Resume opens, how old a History row reads
static void testMangaUiLogic()
{
	// Book (issue #7): the store's string, which panel gets the spread's first page, the turn, the keys, a swipe
	{
		int m = -1, b = -1;
		CHECK_EQ(parseMangaReadingMode("rtl/book-left", m, b), true);
		CHECK_EQ(m, MANGA_RTL);
		CHECK_EQ(b, MANGA_BOOK_LEFT);
		CHECK_EQ(parseMangaReadingMode("ltr/book-right", m, b) && m == MANGA_LTR && b == MANGA_BOOK_RIGHT, true);
		CHECK_EQ(parseMangaReadingMode("ltr", m, b) && m == MANGA_LTR && b == MANGA_BOOK_OFF, true);
		CHECK_EQ(parseMangaReadingMode("strip/book-left", m, b) && m == MANGA_STRIP && b == MANGA_BOOK_OFF, true);	// no strip book
		CHECK_EQ(!parseMangaReadingMode("", m, b), true);
		CHECK_EQ(!parseMangaReadingMode("sideways", m, b), true);
		CHECK_EQ(mangaReadingModeId(MANGA_RTL, MANGA_BOOK_LEFT), std::string("rtl/book-left"));
		CHECK_EQ(mangaReadingModeId(MANGA_LTR, MANGA_BOOK_OFF), std::string("ltr"));
		CHECK_EQ(mangaReadingModeId(MANGA_STRIP, MANGA_BOOK_RIGHT), std::string("strip"));
		for (int mode = 0; mode < MANGA_MODES; mode++)
			for (int book = 0; book < MANGA_BOOKS; book++)
			{
				int m2 = -1, b2 = -1;
				CHECK_EQ(parseMangaReadingMode(mangaReadingModeId(mode, book), m2, b2), true);
				CHECK_EQ(m2, mode);
				CHECK_EQ(b2, mode == MANGA_STRIP ? MANGA_BOOK_OFF : book);
			}
		// held top-left: the top panel is the reader's left. right to left puts the first page on the right
		CHECK_EQ(!mangaBookEarlierOnTop(MANGA_BOOK_LEFT, true), true);
		CHECK_EQ(mangaBookEarlierOnTop(MANGA_BOOK_LEFT, false), true);
		CHECK_EQ(mangaBookEarlierOnTop(MANGA_BOOK_RIGHT, true), true);
		CHECK_EQ(!mangaBookEarlierOnTop(MANGA_BOOK_RIGHT, false), true);
		CHECK_EQ(mangaBookAngle(MANGA_BOOK_LEFT), 90);
		CHECK_EQ(mangaBookAngle(MANGA_BOOK_RIGHT), 270);
		// top-left: UP points to the reader's left, which is forward in right to left
		CHECK_EQ(mangaBookStep(MANGA_BOOK_LEFT, true, false, -1), 1);
		CHECK_EQ(mangaBookStep(MANGA_BOOK_LEFT, true, false, 1), -1);
		CHECK_EQ(mangaBookStep(MANGA_BOOK_LEFT, false, false, 1), 1);
		// top-right: UP points to the reader's right
		CHECK_EQ(mangaBookStep(MANGA_BOOK_RIGHT, true, false, 1), 1);
		CHECK_EQ(mangaBookStep(MANGA_BOOK_RIGHT, false, false, -1), 1);
		// Swap Left/Right turns it round
		CHECK_EQ(mangaBookStep(MANGA_BOOK_LEFT, true, true, -1), -1);
		CHECK_EQ(mangaBookStep(MANGA_BOOK_LEFT, true, false, 0), 0);
		// a finger moving to the reader's left (up the panel, top-left) turns as the right key: back in right to left
		CHECK_EQ(mangaBookSwipe(MANGA_BOOK_LEFT, true, false, -0.2), -1);
		CHECK_EQ(mangaBookSwipe(MANGA_BOOK_LEFT, false, false, -0.2), 1);
		// top-right: up the panel is the reader's right; a finger going right turns as the left key: forward in rtl
		CHECK_EQ(mangaBookSwipe(MANGA_BOOK_RIGHT, true, false, -0.2), 1);
		CHECK_EQ(mangaBookSwipe(MANGA_BOOK_RIGHT, true, false, 0.0), 0);
	}
	// a title's cover is its own and always the same; the top is dark and the bottom mid
	MangaCoverColors a = mangaCoverColors("Ember Lantern"), b = mangaCoverColors("Ember Lantern");
	CHECK_EQ(a.topR, b.topR);
	CHECK_EQ(a.topG, b.topG);
	CHECK_EQ(a.topB, b.topB);
	CHECK_EQ(a.bottomR, b.bottomR);
	CHECK_EQ(a.bottomG, b.bottomG);
	CHECK_EQ(a.bottomB, b.bottomB);
	std::set<std::tuple<int, int, int>> tops;
	for (int i = 0; i < 40; i++)
	{
		MangaCoverColors c = mangaCoverColors("Series " + std::to_string(i));
		tops.insert({c.topR, c.topG, c.topB});
		CHECK_EQ(c.topR + c.topG + c.topB < c.bottomR + c.bottomG + c.bottomB, true);
	}
	CHECK_EQ(tops.size() > 1, true);	// not every title the same book

	// Resume: the first started-but-unread chapter at its page, else the first unread, else the first
	std::vector<MangaChapterMark> m(3);
	CHECK_EQ(mangaResume({}).chapter, -1);

	m[0].read = true;
	m[1].read = true;
	m[2].read = true;
	CHECK_EQ(mangaResume(m).chapter, 0);
	CHECK_EQ(mangaResume(m).page, 0);

	m[2].read = false;
	CHECK_EQ(mangaResume(m).chapter, 2);
	CHECK_EQ(mangaResume(m).page, 0);

	m[2].page = 6;
	m[2].started = true;
	m[1].read = false;
	// the started one wins over the earlier unread one (a chapter merely unread isn't where you left off)
	CHECK_EQ(mangaResume(m).chapter, 2);
	CHECK_EQ(mangaResume(m).page, 6);

	m[1].started = true;
	m[1].page = 4;
	CHECK_EQ(mangaResume(m).chapter, 1);
	CHECK_EQ(mangaResume(m).page, 4);

	// how old a History row reads
	const int64_t t = 2000000000;	// (big enough that a year before it is still a time)
	CHECK_EQ(mangaAgo(0, t), std::string(""));
	CHECK_EQ(mangaAgo(t + 60, t), std::string(""));
	CHECK_EQ(mangaAgo(t, t), std::string("now"));
	CHECK_EQ(mangaAgo(t - 59, t), std::string("now"));
	CHECK_EQ(mangaAgo(t - 60, t), std::string("1 min ago"));
	CHECK_EQ(mangaAgo(t - 120, t), std::string("2 min ago"));
	CHECK_EQ(mangaAgo(t - 3600, t), std::string("1 h ago"));
	CHECK_EQ(mangaAgo(t - 86400, t), std::string("1 day ago"));
	CHECK_EQ(mangaAgo(t - 2 * 86400, t), std::string("2 days ago"));
	CHECK_EQ(mangaAgo(t - 7 * 86400, t), std::string("1 week ago"));
	CHECK_EQ(mangaAgo(t - 30 * 86400, t), std::string("1 month ago"));
	CHECK_EQ(mangaAgo(t - 365 * 86400, t), std::string("1 year ago"));
	CHECK_EQ(mangaAgo(t - 2 * 365 * 86400, t), std::string("2 years ago"));

	// the item table a source series opened from the Library gets back: its key and title, escaped
	CHECK_EQ(mangaSourceItemJson("abc/12", "A \"quoted\" title"), std::string("{\"key\":\"abc/12\",\"title\":\"A \\\"quoted\\\" title\"}"));
	CHECK_EQ(mangaSourceItemJson("a\\b", "Tab\there"), std::string("{\"key\":\"a\\\\b\",\"title\":\"Tab\\u0009here\"}"));
}

// a source series' page's cache (design 6): the file's name, its JSON round trip and how old a check reads
static void testMangaSeriesCache()
{
	// the name is the digest of the series id, never the id or its URL (a URL has '/' and ':' and can be long)
	const std::string id = "src:repo-hash/source:https://site.example/manga/one?x=1";
	std::string p1 = mangaSeriesCachePath("/state", id);
	CHECK_EQ(p1.rfind("/state/manga/series/", 0), size_t(0));
	CHECK_EQ(p1.compare(p1.size() - 5, 5, ".json"), 0);
	std::string name = p1.substr(p1.rfind('/') + 1);
	CHECK_EQ(name.size(), size_t(64 + 5));
	CHECK_EQ(name.find(':'), std::string::npos);
	CHECK_EQ(name.find("https"), std::string::npos);
	CHECK_EQ(p1, mangaSeriesCachePath("/state", id));
	CHECK_EQ(p1 == mangaSeriesCachePath("/state", id + "2"), false);
	CHECK_EQ(mangaSeriesCachePath("", id), std::string());
	CHECK_EQ(mangaSeriesCachePath("/state", ""), std::string());

	// a round trip through the file
	char tmpl[] = "/tmp/bp-manga-series-cache-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string state = made;

	MangaSeriesCache c;
	c.fetchedAt = 1760000000;
	c.detailsOn = c.chaptersOn = true;
	c.details.title = "Ember \"Lantern\"";
	c.details.author = "Ann";
	c.details.artist = "Bo";
	c.details.description = "Line one\nline two \xE2\x80\x94 \xC3\xBCn\xC3\xAF" "code";
	c.details.status = "ongoing";
	c.details.cover = "https://site.example/c.jpg";
	c.details.genres = {"Action", "Slice of Life"};
	MangaChapter a;
	a.key = "/c/9";
	a.name = "Chapter 9";
	a.group = "Group";
	a.number = 9.5;
	a.date = 1731200000;
	a.json = "{\"key\":\"/c/9\",\"title\":\"Chapter \\\"9\\\"\"}";
	c.chapters.push_back(a);
	MangaChapter b;
	b.key = "12";
	b.name = "Chapter 10";
	b.number = 10;
	b.json = "12";
	c.chapters.push_back(b);

	CHECK_EQ(mangaSeriesCacheWrite(state, id, c), true);
	std::string path = mangaSeriesCachePath(state, id);
	std::ifstream cacheFile(path, std::ios::binary);
	std::string text((std::istreambuf_iterator<char>(cacheFile)), std::istreambuf_iterator<char>());
	cacheFile.close();
	CHECK_EQ(text.empty(), false);
	MangaSeriesCache back;
	CHECK_EQ(mangaSeriesCacheRead(state, id, back), true);
	CHECK_EQ(back.fetchedAt, c.fetchedAt);
	CHECK_EQ(back.detailsOn, true);
	CHECK_EQ(back.chaptersOn, true);
	CHECK_EQ(back.details.title, c.details.title);
	CHECK_EQ(back.details.author, c.details.author);
	CHECK_EQ(back.details.artist, c.details.artist);
	CHECK_EQ(back.details.description, c.details.description);
	CHECK_EQ(back.details.status, c.details.status);
	CHECK_EQ(back.details.cover, c.details.cover);
	CHECK_EQ(back.details.genres.size(), size_t(2));
	CHECK_EQ(back.details.genres[1], std::string("Slice of Life"));
	CHECK_EQ(back.chapters.size(), size_t(2));
	CHECK_EQ(back.chapters[0].key, a.key);
	CHECK_EQ(back.chapters[0].name, a.name);
	CHECK_EQ(back.chapters[0].group, a.group);
	CHECK_EQ(back.chapters[0].number, 9.5);
	CHECK_EQ(back.chapters[0].date, a.date);
	CHECK_EQ(back.chapters[0].json, a.json);
	CHECK_EQ(back.chapters[1].key, b.key);
	CHECK_EQ(back.chapters[1].json, b.json);

	// a missing series is no cache; damaged or other-format text isn't one either (no error to show)
	MangaSeriesCache none;
	CHECK_EQ(mangaSeriesCacheRead(state, id + "x", none), false);
	CHECK_EQ(mangaSeriesCacheParse(text.substr(0, text.size() / 2), none), false);
	CHECK_EQ(mangaSeriesCacheParse(text + "{}", none), false);
	CHECK_EQ(mangaSeriesCacheParse("{\"format\":2}", none), false);
	CHECK_EQ(mangaSeriesCacheWrite("", id, c), false);	// nowhere to keep it

	// how old a check reads, and what counts as stale (more than a day: the page offers L2 Refresh)
	const int64_t t = 2000000000;
	CHECK_EQ(mangaCheckedAgo(0, t), std::string(""));
	CHECK_EQ(mangaCheckedAgo(t, t), std::string("today"));
	CHECK_EQ(mangaCheckedAgo(t - 86399, t), std::string("today"));
	CHECK_EQ(mangaCheckedAgo(t - 86400, t), std::string("1 day"));
	CHECK_EQ(mangaCheckedAgo(t - 2 * 86400, t), std::string("2 days"));
	CHECK_EQ(mangaCheckedAgo(t - 400 * 86400, t), std::string("400 days"));
	CHECK_EQ(mangaCheckedAgo(t + 3600, t), std::string("today"));	// a clock behind the file's stamp
	CHECK_EQ(mangaCheckedStale(0, t), false);
	CHECK_EQ(mangaCheckedStale(t, t), false);
	CHECK_EQ(mangaCheckedStale(t - 86399, t), false);
	CHECK_EQ(mangaCheckedStale(t - 86400, t), true);

	unlink(path.c_str());
	rmdir((state + "/manga/series").c_str());
	rmdir((state + "/manga").c_str());
	rmdir(state.c_str());
}

// the source filters a sheet holds (5.2.1): parsing the source's definitions, defaults, the object search() gets
static void testMangaFilters()
{
	const std::string json =
		"[{\"id\":\"sort\",\"type\":\"sort\",\"name\":\"Sort\",\"options\":[{\"id\":\"popular\",\"name\":\"Popular\"},"
		"{\"id\":\"latest\",\"name\":\"Latest\"}],\"default\":{\"by\":\"latest\",\"asc\":true}},"
		"{\"id\":\"status\",\"type\":\"select\",\"name\":\"Status\",\"options\":[{\"id\":\"any\",\"name\":\"Any\"},"
		"{\"id\":\"ongoing\",\"name\":\"Ongoing\"}],\"default\":\"ongoing\"},"
		"{\"id\":\"only\",\"type\":\"check\",\"name\":\"Only with chapters\",\"default\":true},"
		"{\"id\":\"types\",\"type\":\"multi\",\"name\":\"Type\",\"options\":[{\"id\":\"manga\",\"name\":\"Manga\"},"
		"{\"id\":\"manhwa\",\"name\":\"Manhwa\"}],\"default\":[\"manga\"]},"
		"{\"id\":\"genres\",\"type\":\"tri\",\"name\":\"Genres\",\"options\":[{\"id\":\"action\",\"name\":\"Action\"},"
		"{\"id\":\"romance\",\"name\":\"Romance\"}],\"default\":{\"include\":[\"romance\"]}},"
		"{\"id\":\"author\",\"type\":\"text\",\"name\":\"Author\",\"default\":\"Ann\"},"
		"{\"id\":\"plain\",\"type\":\"sort\",\"name\":\"Plain\",\"options\":[{\"id\":\"name\",\"name\":\"Name\"},"
		"{\"id\":\"date\",\"name\":\"Date\"}]},"
		"{\"id\":\"tags\",\"type\":\"group\",\"name\":\"Tags\",\"filters\":[{\"id\":\"theme\",\"type\":\"tri\","
		"\"name\":\"Themes\",\"options\":[{\"id\":\"isekai\",\"name\":\"Isekai\"}]}]}]";

	std::vector<MangaFilter> defs;
	std::string error;
	CHECK_EQ(mangaFiltersParse(json, defs, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(defs.size(), (size_t)8);
	CHECK_EQ(defs[0].name, std::string("Sort"));
	CHECK_EQ(defs[7].children.size(), (size_t)1);

	std::vector<MangaFilterValue> vals = mangaFilterDefaults(defs);
	CHECK_EQ(vals[0].sortBy, std::string("latest"));
	CHECK_EQ(vals[0].sortAsc, true);
	CHECK_EQ(vals[1].select, std::string("ongoing"));
	CHECK_EQ(vals[2].check, true);
	CHECK_EQ(vals[3].multi.size(), (size_t)2);
	CHECK_EQ(vals[3].multi[0], 1);
	CHECK_EQ(vals[3].multi[1], 0);
	CHECK_EQ(vals[4].tri[0], 0);
	CHECK_EQ(vals[4].tri[1], 1);
	CHECK_EQ(vals[5].text, std::string("Ann"));
	// a sort that declares no direction starts descending, and takes its first option
	CHECK_EQ(vals[6].sortBy, std::string("name"));
	CHECK_EQ(vals[6].sortAsc, false);
	CHECK_EQ(vals[7].children.size(), (size_t)1);
	CHECK_EQ(vals[7].children[0].tri.size(), (size_t)1);

	// the chosen values become exactly the object 5.2.1 gives search(): ids, {by, asc}, bools, include/exclude
	vals[0].sortBy = "popular";
	vals[0].sortAsc = false;
	vals[1].select = "any";
	vals[2].check = false;
	vals[3].multi[0] = 0;
	vals[3].multi[1] = 1;
	vals[4].tri[0] = 1;
	vals[4].tri[1] = 2;
	vals[5].text = " Bob ";
	vals[7].children[0].tri[0] = 1;
	CHECK_EQ(mangaFilterValuesJson(defs, vals),
		std::string("{\"sort\":{\"by\":\"popular\",\"asc\":false},\"status\":\"any\",\"only\":false,"
			"\"types\":[\"manhwa\"],\"genres\":{\"include\":[\"action\"],\"exclude\":[\"romance\"]},"
			"\"author\":\"Bob\",\"plain\":{\"by\":\"name\",\"asc\":false},"
			"\"tags\":{\"theme\":{\"include\":[\"isekai\"],\"exclude\":[]}}}"));

	// a row's summary: the option's name, On/Off, the ticks, and a group's children counted
	CHECK_EQ(mangaFilterSummary(defs[0], vals[0]), std::string("Popular"));
	CHECK_EQ(mangaFilterSummary(defs[1], vals[1]), std::string("Any"));
	CHECK_EQ(mangaFilterSummary(defs[2], vals[2]), std::string("Off"));
	CHECK_EQ(mangaFilterSummary(defs[3], vals[3]), std::string("\xE2\x9C\x93" "1"));
	CHECK_EQ(mangaFilterSummary(defs[4], vals[4]), std::string("\xE2\x9C\x93" "1 \xE2\x9C\x97" "1"));
	CHECK_EQ(mangaFilterSummary(defs[5], vals[5]), std::string("Bob"));
	CHECK_EQ(mangaFilterSummary(defs[7], vals[7]), std::string("\xE2\x9C\x93" "1"));

	// a filter or option the source no longer lists falls back to its default; the list lengths fit again
	vals[1].select = "gone";
	vals[0].sortBy = "gone";
	vals[3].multi.resize(1, 0);
	vals[4].tri.resize(3, 0);
	mangaFilterFit(defs, vals);
	CHECK_EQ(vals[1].select, std::string("ongoing"));
	CHECK_EQ(vals[0].sortBy, std::string("latest"));
	CHECK_EQ(vals[3].multi.size(), (size_t)2);
	CHECK_EQ(vals[4].tri.size(), (size_t)2);

	// a malformed list is an error; unknown or empty definitions are dropped quietly
	std::vector<MangaFilter> bad;
	CHECK_EQ(mangaFiltersParse("not json", bad, error), false);
	CHECK_EQ(error.empty(), false);
	std::vector<MangaFilter> odd;
	CHECK_EQ(mangaFiltersParse("[{\"id\":\"x\",\"type\":\"nope\"},{\"id\":\"\",\"type\":\"text\"},"
		"{\"type\":\"text\"},{\"id\":\"y\",\"type\":\"text\"}]", odd, error), true);
	CHECK_EQ(odd.size(), (size_t)1);
	CHECK_EQ(odd[0].id, std::string("y"));
	CHECK_EQ(mangaFiltersParse("[]", odd, error), true);
	CHECK_EQ(odd.size(), (size_t)0);
}

// the paged modes: rows of centred pages, drag and snap, and Right/Left through tall pages
static void testPaged()
{
	// the file's own near(): the global one is declared past this point
	auto near = [](double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; };
	auto find = [](const std::vector<Paged::Placement>& v, int page)
	{
		for (auto& q : v) if (q.page == page) return q;
		return Paged::Placement{page, 0, 0, 0, 0};	// w == 0: the page isn't in the view
	};

	// layout: the placeholder 1.5 fills this 4:3 view at fit Page, and at rest only that page is in the view
	Paged p;
	p.setView(400, 600);
	p.setPages(4);
	CHECK_EQ(p.pageCount(), 4);
	CHECK_EQ(p.current(), 0);
	CHECK_EQ(p.moving(), false);
	auto pl = p.placements();
	CHECK_EQ(pl.size(), (size_t)1);
	CHECK_EQ(near(pl[0].x, 0), true);
	CHECK_EQ(near(pl[0].y, 0), true);
	CHECK_EQ(near(pl[0].w, 400), true);
	CHECK_EQ(near(pl[0].h, 600), true);
	CHECK_EQ(p.vscroll(), 0.0);

	// a page smaller than the view is centred: x when fit Page narrows it, y when it is short
	Paged narrow;
	narrow.setView(400, 600);
	narrow.setPages(2, 3.0);	// drawn 200 x 600: the equal neighbour touches the view's edge, so two are in it
	pl = narrow.placements();
	CHECK_EQ(pl.size(), (size_t)2);
	CHECK_EQ(near(find(pl, 0).x, 100), true);
	CHECK_EQ(near(find(pl, 0).w, 200), true);
	CHECK_EQ(near(find(pl, 0).y, 0), true);
	Paged low;
	low.setView(400, 600);
	low.setPages(2, 0.5);	// drawn 400 x 200
	pl = low.placements();
	CHECK_EQ(pl.size(), (size_t)1);
	CHECK_EQ(near(find(pl, 0).y, 200), true);
	CHECK_EQ(near(find(pl, 0).h, 200), true);

	// an ltr drag moves the row 1:1 (a finger right walks back along the row), x goes negative, current is the
	// page the row's middle is nearest to
	p.goTo(1, 0, false);
	CHECK_EQ(p.current(), 1);
	p.touchDown(0);
	p.drag(150, 0, 50);
	pl = p.placements();
	CHECK_EQ(pl.size(), (size_t)2);
	CHECK_EQ(near(find(pl, 0).x, -250), true);
	CHECK_EQ(near(find(pl, 1).x, 150), true);
	CHECK_EQ(p.current(), 1);	// 150 of the 400 back: nearer page 1's centre than page 0's
	// a short slow release snaps back over 220 ms with an ease-out cubic
	p.release(200);
	CHECK_EQ(p.moving(), true);
	CHECK_EQ(p.step(310), true);	// half way: 0.875 of 150 px travelled
	CHECK_EQ(near(find(p.placements(), 1).x, 18.75), true);
	CHECK_EQ(p.step(420), false);
	CHECK_EQ(p.current(), 1);
	CHECK_EQ(near(find(p.placements(), 1).x, 0), true);
	// a slow drag is enough once the row is past the middle
	p.touchDown(500);
	p.drag(250, 0, 800);	// 150 along the row: page 0's centre is the nearer one
	p.release(1100);	// the finger rested: no velocity in the last 100 ms, and none is needed
	CHECK_EQ(p.moving(), true);
	CHECK_EQ(p.step(1320), false);
	CHECK_EQ(p.current(), 0);

	// a fast flick turns however short the drag, in each direction; 400 px/s is not over 400
	Paged f;
	f.setView(400, 600);
	f.setPages(5);
	f.goTo(2, 0, false);	// the row's middle 800 along, page 2's centre
	f.touchDown(0);
	f.drag(-80, 0, 20);	// a finger left: ltr forward, 800 px/s over the last 20 ms
	f.drag(-80, 0, 40);
	CHECK_EQ(near(find(f.placements(), 2).x, 0), false);	// the row is 160 px on, page 3 peeking in
	f.release(40);
	CHECK_EQ(f.moving(), true);
	CHECK_EQ(f.step(260), false);	// the snap's 220 ms
	CHECK_EQ(f.current(), 3);
	CHECK_EQ(near(find(f.placements(), 3).x, 0), true);
	f.touchDown(300);
	f.drag(80, 0, 320);	// and the way back
	f.drag(80, 0, 340);
	f.release(340);
	CHECK_EQ(f.moving(), true);
	CHECK_EQ(f.step(560), false);
	CHECK_EQ(f.current(), 2);
	f.touchDown(600);
	f.drag(8, 0, 620);	// exactly 400 px/s: no flick, and the drag is nowhere near half a page
	f.drag(8, 0, 640);
	f.release(640);
	CHECK_EQ(f.moving(), true);	// it still glides back to the nearest page
	CHECK_EQ(f.step(860), false);
	CHECK_EQ(f.current(), 2);
	// a row held at an end stops its samples moving there, so no flick turns past the first page or the last
	f.goTo(0, 900, false);
	f.touchDown(1000);
	f.drag(150, 0, 1020);	// ltr back at the chapter's start: the row cannot go before page 0
	f.drag(150, 0, 1040);
	f.release(1040);
	CHECK_EQ(f.moving(), false);
	CHECK_EQ(f.current(), 0);
	f.goTo(4, 1100, false);
	f.touchDown(1200);
	f.drag(-150, 0, 1220);	// ltr forward at the chapter's end
	f.drag(-150, 0, 1240);
	f.release(1240);
	CHECK_EQ(f.moving(), false);
	CHECK_EQ(f.current(), 4);
	CHECK_EQ(f.atEnd(), true);
	f.forward(1300);	// nothing left: at rest, no animation
	CHECK_EQ(f.moving(), false);

	// right to left: the row runs the other way about the centred page; a finger right is toward the next page
	Paged r;
	r.setView(400, 600);
	r.setPages(4);
	r.setRtl(true);
	r.goTo(1, 0, false);
	CHECK_EQ(r.placements().size(), (size_t)1);	// the shown page is centred either way
	CHECK_EQ(near(find(r.placements(), 1).x, 0), true);
	r.touchDown(0);
	r.drag(150, 0, 50);	// next page 2 enters from the left as the row slides right
	pl = r.placements();
	CHECK_EQ(pl.size(), (size_t)2);
	CHECK_EQ(near(find(pl, 1).x, 150), true);
	CHECK_EQ(near(find(pl, 2).x, -250), true);
	CHECK_EQ(r.current(), 1);
	r.release(200);	// short and slow: back to 1
	CHECK_EQ(r.moving(), true);
	CHECK_EQ(r.step(420), false);
	CHECK_EQ(r.current(), 1);
	r.touchDown(500);
	r.drag(80, 0, 520);	// a right flick: forward, one page
	r.drag(80, 0, 540);
	r.release(540);
	CHECK_EQ(r.moving(), true);
	CHECK_EQ(r.step(760), false);
	CHECK_EQ(r.current(), 2);
	CHECK_EQ(near(find(r.placements(), 2).x, 0), true);
	r.touchDown(800);
	r.drag(-80, 0, 820);	// and back with a left one
	r.drag(-80, 0, 840);
	r.release(840);
	CHECK_EQ(r.step(1060), false);
	CHECK_EQ(r.current(), 1);
	r.goTo(0, 1100, false);
	r.touchDown(1200);
	r.drag(-150, 0, 1220);	// rtl back at the chapter's start: nothing turns past it
	r.drag(-150, 0, 1240);
	r.release(1240);
	CHECK_EQ(r.moving(), false);
	CHECK_EQ(r.current(), 0);
	// a page that fits the view has nothing to pan: a vertical drag does nothing
	r.touchDown(1300);
	r.drag(0, -100, 1320);
	CHECK_EQ(r.vscroll(), 0.0);
	r.release(1340);
	CHECK_EQ(r.moving(), false);

	// a tall page at fit Width scrolls inside before it turns, and Right opens the next page at the top
	Paged t;
	t.setView(400, 600);
	t.setPages(3, 1.5);
	t.setFit(Paged::FitWidth);
	t.setPageSize(1, 400, 900);	// page 1: 300 px over the view's bottom
	t.goTo(1, 0, false);
	pl = t.placements();
	CHECK_EQ(pl.size(), (size_t)1);
	CHECK_EQ(near(find(pl, 1).h, 900), true);
	CHECK_EQ(near(find(pl, 1).y, 0), true);
	CHECK_EQ(t.vscroll(), 0.0);
	t.forward(0);	// one screenful (540), which here is the rest of the page
	CHECK_EQ(t.moving(), true);
	CHECK_EQ(t.step(180), false);	// the D-pad step slides for 180 ms
	CHECK_EQ(t.current(), 1);	// a screenful never turns the page
	CHECK_EQ(near(t.vscroll(), 300), true);
	CHECK_EQ(near(find(t.placements(), 1).y, -300), true);	// its bottom at the view's
	t.forward(200);	// at its end: the next page, opened at the top
	CHECK_EQ(t.step(380), false);
	CHECK_EQ(t.current(), 2);
	CHECK_EQ(t.vscroll(), 0.0);
	CHECK_EQ(t.atEnd(), true);	// the last page, all of it shown
	t.forward(400);
	CHECK_EQ(t.moving(), false);
	t.back(500);	// Left back over the turn opens the previous page at its end
	CHECK_EQ(t.step(680), false);
	CHECK_EQ(t.current(), 1);
	CHECK_EQ(near(t.vscroll(), 300), true);
	t.nextPage(700);	// R skips the rest of a tall page and goes straight on
	CHECK_EQ(t.step(880), false);
	CHECK_EQ(t.current(), 2);
	CHECK_EQ(t.vscroll(), 0.0);
	t.back(900);	// and Left lands there from the next page's top
	t.step(1080);
	CHECK_EQ(near(t.vscroll(), 300), true);
	t.back(1200);	// a screenful up inside it
	CHECK_EQ(t.step(1380), false);
	CHECK_EQ(near(t.vscroll(), 0), true);
	CHECK_EQ(t.current(), 1);
	t.back(1500);	// at its top: the previous page
	CHECK_EQ(t.step(1680), false);
	CHECK_EQ(t.current(), 0);
	t.prevPage(1700);	// L at the chapter's start: nothing
	CHECK_EQ(t.moving(), false);
	CHECK_EQ(t.current(), 0);

	// a vertical drag snaps the scroll to a screenful's bounds, a fast one goes one further
	Paged v2;
	v2.setView(400, 600);
	v2.setPages(3, 1.5);
	v2.setFit(Paged::FitWidth);
	v2.setPageSize(1, 400, 900);	// bounds: 0 and the bottom at 300
	v2.goTo(1, 0, false);
	v2.touchDown(0);
	v2.drag(0, -100, 20);	// a finger up scrolls down the page
	v2.drag(0, -100, 40);
	v2.release(40);	// 5000 px/s: one bound further, which is the page's bottom
	CHECK_EQ(v2.moving(), true);
	CHECK_EQ(v2.step(260), false);
	CHECK_EQ(near(v2.vscroll(), 300), true);
	CHECK_EQ(v2.current(), 1);
	CHECK_EQ(v2.atEnd(), false);	// only inside the page: page 2 is still to read
	v2.touchDown(300);
	v2.drag(0, 100, 320);	// 200 along: the bottom is the nearer bound
	v2.release(340);	// a lone sample: no velocity, and the axis is moot: the bounds say 300
	CHECK_EQ(v2.step(560), false);
	CHECK_EQ(near(v2.vscroll(), 300), true);
	v2.touchDown(600);
	v2.drag(0, 150, 620);	// 150: an equal share, which keeps the page's top (nearest never hops ahead)
	v2.drag(0, 0, 630);
	v2.release(630);
	CHECK_EQ(v2.step(850), false);
	CHECK_EQ(near(v2.vscroll(), 0), true);

	// Fit Fill on a portrait page in a landscape view: it covers the view and crops top and bottom, so Right
	// still walks down the page and there is nothing to pan across
	Paged fp;
	fp.setView(400, 300);
	fp.setPages(3, 1.5);	// Fill draws it 400 x 600: 300 cropped at the bottom
	fp.setFit(Paged::FitFill);
	pl = fp.placements();
	CHECK_EQ(pl.size(), (size_t)1);
	CHECK_EQ(near(find(pl, 0).w, 400), true);
	CHECK_EQ(near(find(pl, 0).h, 600), true);
	CHECK_EQ(near(find(pl, 0).x, 0), true);
	CHECK_EQ(near(find(pl, 0).y, 0), true);	// top aligned: it is the bottom that crops
	CHECK_EQ(fp.vscroll(), 0.0);
	CHECK_EQ(fp.hscroll(), 0.0);
	fp.forward(0);	// a screenful (270) of the page's 300 of overflow
	CHECK_EQ(fp.step(180), false);
	CHECK_EQ(near(fp.vscroll(), 270), true);
	CHECK_EQ(near(find(fp.placements(), 0).y, -270), true);
	fp.forward(200);	// the part screenful to the page's bottom
	CHECK_EQ(fp.step(380), false);
	CHECK_EQ(near(fp.vscroll(), 300), true);
	CHECK_EQ(fp.atEnd(), false);	// a whole page still to go before the chapter's
	fp.forward(400);	// at the page's end: the next one, opened at its top
	CHECK_EQ(fp.step(580), false);
	CHECK_EQ(fp.current(), 1);
	CHECK_EQ(near(fp.vscroll(), 0), true);

	// Fit Fill on a landscape spread in a portrait-ish view: it covers the view and crops the sides, and ltr
	// pans from the page's left edge after the vertical
	Paged wf;
	wf.setView(400, 600);
	wf.setPages(3, 0.6);	// Fill draws it 1000 x 600: a page full tall, 600 over sideways
	wf.setFit(Paged::FitFill);
	pl = wf.placements();
	CHECK_EQ(pl.size(), (size_t)1);
	CHECK_EQ(near(find(pl, 0).w, 1000), true);
	CHECK_EQ(near(find(pl, 0).h, 600), true);
	CHECK_EQ(near(find(pl, 0).y, 0), true);
	CHECK_EQ(near(find(pl, 0).x, 0), true);	// the pan starts at the page's left edge, not centred
	wf.forward(0);	// nothing below to scroll: Right crosses a screen width, 360
	CHECK_EQ(wf.current(), 0);	// a step across never turns the page
	CHECK_EQ(wf.step(180), false);
	CHECK_EQ(near(wf.vscroll(), 0), true);
	CHECK_EQ(near(wf.hscroll(), 360), true);
	CHECK_EQ(near(find(wf.placements(), 0).x, -360), true);
	wf.forward(200);	// the last column ends flush at the page's right edge
	CHECK_EQ(wf.step(380), false);
	CHECK_EQ(near(wf.hscroll(), 600), true);
	CHECK_EQ(near(find(wf.placements(), 0).x, -600), true);
	CHECK_EQ(wf.atEnd(), false);
	wf.forward(400);	// the page's end: the next one, opened at its top and start edge
	CHECK_EQ(wf.step(580), false);
	CHECK_EQ(wf.current(), 1);
	CHECK_EQ(wf.hscroll(), 0.0);
	CHECK_EQ(near(find(wf.placements(), 1).x, 0), true);
	wf.back(600);	// Left back over the turn opens the previous page at its last column
	CHECK_EQ(wf.step(780), false);
	CHECK_EQ(wf.current(), 0);
	CHECK_EQ(near(wf.hscroll(), 600), true);
	wf.back(800);	// and back reverses forward: column by column…
	CHECK_EQ(wf.step(980), false);
	CHECK_EQ(near(wf.hscroll(), 240), true);
	wf.back(1000);	// …to the start edge
	CHECK_EQ(wf.step(1180), false);
	CHECK_EQ(near(wf.hscroll(), 0), true);
	wf.back(1200);	// at its start on the first page: nothing
	CHECK_EQ(wf.moving(), false);
	CHECK_EQ(wf.current(), 0);

	// rtl reads the spread the other way: the pan starts at the right edge and steps leftwards
	Paged wr;
	wr.setView(400, 600);
	wr.setPages(3, 0.6);
	wr.setFit(Paged::FitFill);
	wr.setRtl(true);
	CHECK_EQ(near(find(wr.placements(), 0).x, -600), true);	// right edge flush: hscroll 0 is the reading start
	wr.forward(0);
	CHECK_EQ(wr.step(180), false);
	CHECK_EQ(near(wr.hscroll(), 360), true);	// forward along the reading order: the page slides right
	CHECK_EQ(near(find(wr.placements(), 0).x, -240), true);
	wr.forward(200);
	CHECK_EQ(wr.step(380), false);
	CHECK_EQ(near(wr.hscroll(), 600), true);	// the page's end: its left edge flush
	CHECK_EQ(near(find(wr.placements(), 0).x, 0), true);
	wr.forward(400);	// the next page, opened at its right edge again
	CHECK_EQ(wr.step(580), false);
	CHECK_EQ(wr.current(), 1);
	CHECK_EQ(near(wr.hscroll(), 0), true);
	CHECK_EQ(near(find(wr.placements(), 1).x, -600), true);
	wr.back(600);	// back over the turn: the previous page at its end, the left column in rtl
	CHECK_EQ(wr.step(780), false);
	CHECK_EQ(wr.current(), 0);
	CHECK_EQ(near(wr.hscroll(), 600), true);
	CHECK_EQ(near(find(wr.placements(), 0).x, 0), true);
	wr.back(800);
	CHECK_EQ(wr.step(980), false);
	CHECK_EQ(near(wr.hscroll(), 240), true);
	CHECK_EQ(near(find(wr.placements(), 0).x, -360), true);
	wr.back(1000);
	CHECK_EQ(wr.step(1180), false);
	CHECK_EQ(near(wr.hscroll(), 0), true);
	wr.back(1200);	// at the chapter's start: nothing
	CHECK_EQ(wr.moving(), false);
	CHECK_EQ(wr.current(), 0);

	// a drag pans inside a wide page first and only carries the row once the pan hits its edge, as photo
	// viewers do
	Paged wd;
	wd.setView(400, 600);
	wd.setPages(3, 0.6);	// the same 1000 x 600 spreads, their centres 1000 apart
	wd.setFit(Paged::FitFill);
	wd.goTo(1, 0, false);
	CHECK_EQ(near(find(wd.placements(), 1).x, 0), true);
	wd.touchDown(10);
	wd.drag(100, 0, 20);	// at its start edge the pan has nothing left to take: the drag pulls the row, at half rate
	CHECK_EQ(wd.hscroll(), 0.0);
	CHECK_EQ(near(find(wd.placements(), 1).x, 50), true);	// the page slides right…
	CHECK_EQ(near(find(wd.placements(), 0).x, -950), true);	// …and page 0 follows at its edge, seam kept
	CHECK_EQ(wd.current(), 1);
	wd.release(20);	// a lone sample: no flick, the row snaps back
	CHECK_EQ(wd.moving(), true);
	CHECK_EQ(wd.step(240), false);
	CHECK_EQ(near(find(wd.placements(), 1).x, 0), true);
	wd.touchDown(300);
	wd.drag(-500, 0, 320);	// panning inside the page: the row stays put
	CHECK_EQ(near(wd.hscroll(), 500), true);
	CHECK_EQ(near(find(wd.placements(), 1).x, -500), true);
	CHECK_EQ(wd.current(), 1);
	wd.drag(-300, 0, 420);	// 100 of this pans on to the far edge, the other 200 pull the row 100 (half rate)
	CHECK_EQ(near(wd.hscroll(), 600), true);
	CHECK_EQ(wd.current(), 1);	// nowhere near page 2's centre yet
	CHECK_EQ(near(find(wd.placements(), 1).x, -700), true);
	CHECK_EQ(near(find(wd.placements(), 2).x, 300), true);	// the next page waits at the panned one's edge
	wd.drag(0, 0, 430);	// the finger rests: no velocity to release
	wd.release(430);	// slow and only 200 carried over: page 1's centre is still nearer, its pan stays at the end
	CHECK_EQ(wd.step(650), false);
	CHECK_EQ(wd.current(), 1);
	CHECK_EQ(near(wd.hscroll(), 600), true);
	CHECK_EQ(near(find(wd.placements(), 1).x, -600), true);	// its right edge flush with the view

	// setFit cycles the three fits with the current page kept, and drops the offsets a new fit can't hold
	Paged sw;
	sw.setView(400, 600);
	sw.setPages(3, 1.5);
	sw.setPageSize(1, 800, 600);	// a spread, page 1: aspect 0.75
	sw.goTo(1, 0, false);
	sw.setFit(Paged::FitFill);	// covers the view: drawn 800 x 600, 400 to pan
	CHECK_EQ(sw.current(), 1);
	CHECK_EQ(near(find(sw.placements(), 1).w, 800), true);
	sw.forward(0);
	CHECK_EQ(sw.step(180), false);
	CHECK_EQ(near(sw.hscroll(), 360), true);
	sw.setFit(Paged::FitPage);	// the spread fits: its pan is gone, the page sits centred on its place
	CHECK_EQ(sw.current(), 1);
	CHECK_EQ(sw.hscroll(), 0.0);
	CHECK_EQ(near(find(sw.placements(), 1).w, 400), true);
	CHECK_EQ(near(find(sw.placements(), 1).x, 0), true);
	CHECK_EQ(near(find(sw.placements(), 1).y, 150), true);
	sw.setFit(Paged::FitWidth);	// 400 wide too: shorter than the view, so centred down as Page's is
	CHECK_EQ(sw.current(), 1);
	CHECK_EQ(near(find(sw.placements(), 1).w, 400), true);
	sw.setFit(Paged::FitFill);	// Fill again: the spread is back and page 1 opens at its start edge
	CHECK_EQ(sw.current(), 1);
	CHECK_EQ(near(find(sw.placements(), 1).w, 800), true);
	CHECK_EQ(sw.hscroll(), 0.0);
	CHECK_EQ(near(find(sw.placements(), 1).x, 0), true);

	// goTo: animated glides the row with current() crossing to the target, plain jumps
	Paged g;
	g.setView(400, 600);
	g.setPages(5);
	g.goTo(2, 0, true);
	CHECK_EQ(g.moving(), true);
	CHECK_EQ(g.step(60), true);	// 0.704 of the way: still nearer page 1
	CHECK_EQ(g.current(), 1);
	CHECK_EQ(g.step(90), true);	// past the middle of the row's travel
	CHECK_EQ(g.current(), 2);
	CHECK_EQ(g.step(180), false);
	CHECK_EQ(near(find(g.placements(), 2).x, 0), true);
	g.goTo(0, 300, false);	// no animation, straight to the page
	CHECK_EQ(g.moving(), false);
	CHECK_EQ(g.current(), 0);
	// a touch stops a slide where it is and the release snaps from there (as in Strip)
	g.goTo(3, 1000, true);
	CHECK_EQ(g.step(1090), true);	// 0.875 of the 0→1200 glide: 1050
	g.touchDown(1100);
	CHECK_EQ(g.moving(), false);
	CHECK_EQ(g.step(1200), false);
	CHECK_EQ(g.current(), 3);	// the middle is nearer page 3's centre already
	g.release(1250);
	CHECK_EQ(g.moving(), true);
	CHECK_EQ(g.step(1470), false);
	CHECK_EQ(near(find(g.placements(), 3).x, 0), true);

	// wanted: the current page, then up to ahead in reading order, then up to behind; rtl reads the same numbers
	CHECK_EQ(g.wanted(2, 1), std::vector<int>({3, 4, 2}));
	g.goTo(1, 1500, false);
	CHECK_EQ(g.wanted(3, 3), std::vector<int>({1, 2, 3, 4, 0}));
	g.setRtl(true);
	CHECK_EQ(g.wanted(2, 1), std::vector<int>({1, 2, 3, 0}));
	CHECK_EQ(g.wanted(-2, -2), std::vector<int>({1}));
	std::vector<int> all = g.wanted(99, 99);
	CHECK_EQ(all.size(), (size_t)5);
	std::set<int> once(all.begin(), all.end());
	CHECK_EQ(once.size(), (size_t)5);

	// a real page size redraws the row without moving the page being shown
	Paged q;
	q.setView(400, 600);
	q.setPages(4, 1.5);
	q.goTo(2, 0, false);
	q.setPageSize(2, 400, 1200);	// now 200 x 600 at fit Page: its own centre stays, its neighbours slide
	CHECK_EQ(q.current(), 2);
	CHECK_EQ(q.moving(), false);
	pl = q.placements();
	CHECK_EQ(near(find(pl, 2).x, 100), true);	// centred at the new, narrower width
	CHECK_EQ(near(find(pl, 2).w, 200), true);
	CHECK_EQ(near(find(pl, 2).h, 600), true);
	q.setPageSize(1, 400, 1200);	// a neighbour narrows too: the shown page does not budge
	CHECK_EQ(q.current(), 2);
	CHECK_EQ(near(find(q.placements(), 2).x, 100), true);
	q.setPageSize(2, 0, 500);	// bad sizes are ignored, as in Strip
	CHECK_EQ(near(find(q.placements(), 2).w, 200), true);
	// and a tall page that turns short mid-read keeps no scroll below the view
	Paged c2;
	c2.setView(400, 600);
	c2.setPages(3, 1.5);
	c2.setFit(Paged::FitWidth);
	c2.setPageSize(1, 400, 1200);	// 600 px over: one whole screenful and 60
	c2.goTo(1, 0, false);
	c2.forward(0);
	CHECK_EQ(c2.step(180), false);
	CHECK_EQ(near(c2.vscroll(), 540), true);	// a full screenful, a part one would not be enough to stop on
	c2.setPageSize(1, 400, 500);	// suddenly it fits
	CHECK_EQ(c2.current(), 1);
	CHECK_EQ(near(c2.vscroll(), 0), true);
	CHECK_EQ(near(find(c2.placements(), 1).h, 500), true);
	CHECK_EQ(near(find(c2.placements(), 1).y, 50), true);	// centred again

	// zoom: the page grows past the view both ways, the content under the focus keeps its place, Right walks
	// through it in reading order before it turns, and a drag pans inside before it carries the row
	Paged pz;
	pz.setView(400, 600);
	pz.setPages(3, 1.5);	// at the fit 400 x 600
	pz.goTo(1, 0, false);
	pz.setZoom(2, 200, 300);
	CHECK_EQ(near(pz.zoom(), 2), true);
	CHECK_EQ(pz.current(), 1);
	CHECK_EQ(near(pz.hscroll(), 200), true);	// (0 + 200) * 2 - 200
	CHECK_EQ(near(pz.vscroll(), 300), true);	// (0 + 300) * 2 - 300
	pl = pz.placements();
	CHECK_EQ(near(find(pl, 1).w, 800), true);
	CHECK_EQ(near(find(pl, 1).h, 1200), true);
	CHECK_EQ(near(find(pl, 1).x, -200), true);
	CHECK_EQ(near(find(pl, 1).y, -300), true);
	pz.forward(0);	// one screenful down the page first
	CHECK_EQ(pz.step(180), false);
	CHECK_EQ(near(pz.vscroll(), 600), true);	// its bottom: 540 would pass it
	CHECK_EQ(pz.current(), 1);
	pz.forward(200);	// then across, one screen width, back at the top
	CHECK_EQ(pz.step(380), false);
	CHECK_EQ(near(pz.hscroll(), 400), true);
	CHECK_EQ(pz.vscroll(), 0.0);
	CHECK_EQ(pz.current(), 1);
	pz.forward(400);	// the second column: a plain screenful again
	CHECK_EQ(pz.step(580), false);
	CHECK_EQ(near(pz.vscroll(), 540), true);
	pz.forward(600);	// the part screenful to the page's bottom
	CHECK_EQ(pz.step(780), false);
	CHECK_EQ(near(pz.vscroll(), 600), true);
	pz.forward(800);	// at the page's end: the next one, at its top and start edge
	CHECK_EQ(pz.step(980), false);
	CHECK_EQ(pz.current(), 2);
	CHECK_EQ(pz.vscroll(), 0.0);
	CHECK_EQ(pz.hscroll(), 0.0);
	pz.back(1000);	// Left back over the turn opens the previous page at its end
	CHECK_EQ(pz.step(1180), false);
	CHECK_EQ(pz.current(), 1);
	CHECK_EQ(near(pz.vscroll(), 600), true);
	CHECK_EQ(near(pz.hscroll(), 400), true);
	// a finger pans inside the zoomed page first, and only past its edge moves the row (as Fill does)
	Paged pd;
	pd.setView(400, 600);
	pd.setPages(3, 1.5);
	pd.goTo(1, 0, false);
	pd.setZoom(2, 200, 300);	// h 200, v 300
	pd.touchDown(0);
	pd.drag(-100, 0, 10);	// a finger left: across the page toward its end
	CHECK_EQ(near(pd.hscroll(), 300), true);
	CHECK_EQ(pd.current(), 1);
	pd.drag(-500, 0, 20);	// 100 to the page's edge, the other 400 carry the row on
	CHECK_EQ(near(pd.hscroll(), 400), true);
	CHECK_EQ(pd.current(), 1);	// not yet page 2's centre
	pd.drag(0, -100, 30);	// and down inside it: the row's vertical is the page's pan
	CHECK_EQ(near(pd.vscroll(), 400), true);
	pd.release(400);	// it rests: no flick
	CHECK_EQ(pd.step(620), false);
	CHECK_EQ(pd.current(), 1);
	// zoomed, a pan inside the page stays where the finger let it go: no snap to a screenful's bound (at the fit a
	// page bigger than the screen still steps a screenful at a time)
	{
		Paged pf;
		pf.setView(400, 600);
		pf.setPages(3, 1.5);
		pf.goTo(1, 0, false);
		pf.setZoom(2, 200, 300);	// h 200, v 300
		pf.touchDown(0);
		pf.drag(0, -130, 10);	// down the page by 130: v 430, between bounds
		pf.drag(-70, 0, 20);	// and across by 70: h 270
		pf.release(1000);	// rested: no flick
		pf.step(1300);
		CHECK_EQ(pf.current(), 1);
		CHECK_EQ(near(pf.vscroll(), 430), true);
		CHECK_EQ(near(pf.hscroll(), 270), true);
	}
	// zoomed, a quick swipe across glides on past where the finger left it (issue #8), and stays inside the page
	{
		Paged pg;
		pg.setView(400, 600);
		pg.setPages(3, 1.5);
		pg.goTo(1, 0, false);
		pg.setZoom(2, 200, 300);	// h 200 of 400
		pg.touchDown(0);
		pg.drag(-40, 0, 10);
		pg.drag(-40, 0, 20);	// 80 px in 20 ms: 4000 px/s across
		pg.release(25);
		CHECK_EQ(pg.moving(), true);
		pg.step(2000);
		CHECK_EQ(pg.current(), 1);
		CHECK_EQ(pg.hscroll() > 280 + 1, true);	// further than the finger took it
		CHECK_EQ(pg.hscroll() <= 400 + 0.5, true);	// and not past the page's end
	}
	// a quick nudge isn't a turn: a flick turns the page only once the row has moved 10 % of the screen
	{
		Paged pn;
		pn.setView(400, 600);
		pn.setPages(3, 1.5);
		pn.goTo(1, 0, false);
		pn.touchDown(0);
		pn.drag(-15, 0, 10);
		pn.drag(-15, 0, 20);	// 30 px (under 40) at 1500 px/s
		pn.release(25);
		pn.step(1000);
		CHECK_EQ(pn.current(), 1);
		pn.touchDown(2000);
		pn.drag(-25, 0, 2010);
		pn.drag(-25, 0, 2020);	// 50 px at 2500 px/s: a turn
		pn.release(2025);
		pn.step(3000);
		CHECK_EQ(pn.current(), 2);
	}
	// right to left zooms about the centre the same way: h still runs with the reading order, from the right edge
	Paged rz;
	rz.setView(400, 600);
	rz.setPages(3, 1.5);
	rz.setRtl(true);
	rz.goTo(1, 0, false);
	rz.setZoom(2, 200, 300);
	CHECK_EQ(rz.current(), 1);
	CHECK_EQ(near(rz.hscroll(), 200), true);
	pl = rz.placements();
	CHECK_EQ(near(find(pl, 1).x, -200), true);	// the same page position, mirrored about the row
	CHECK_EQ(near(find(pl, 1).w, 800), true);
	// and back to the fit: what was under the middle is again, the page whole
	Paged pz1;
	pz1.setView(400, 600);
	pz1.setPages(3, 1.5);
	pz1.goTo(1, 0, false);
	pz1.setZoom(2, 200, 300);
	pz1.setZoom(1, 200, 300);
	CHECK_EQ(near(pz1.zoom(), 1), true);
	CHECK_EQ(pz1.current(), 1);
	CHECK_EQ(pz1.hscroll(), 0.0);
	CHECK_EQ(pz1.vscroll(), 0.0);
	pl = pz1.placements();
	CHECK_EQ(near(find(pl, 1).w, 400), true);
	CHECK_EQ(near(find(pl, 1).h, 600), true);
	pz1.setZoom(0.2, 200, 300);	// under the fit is no zoom at all
	CHECK_EQ(near(pz1.zoom(), 1), true);

	// no pages: the whole surface is inert
	Paged e;
	e.setView(400, 600);
	CHECK_EQ(e.pageCount(), 0);
	CHECK_EQ(e.current(), -1);
	CHECK_EQ(e.placements().size(), (size_t)0);
	CHECK_EQ(e.atEnd(), false);
	CHECK_EQ(e.wanted(3, 2).size(), (size_t)0);
	e.goTo(0, 0, true);
	e.forward(0);
	e.back(0);
	e.touchDown(0);
	e.drag(50, 50, 10);
	e.release(20);
	CHECK_EQ(e.moving(), false);
	CHECK_EQ(e.step(100), false);
	e.setPages(2, 0);	// a nonsense aspect falls back to the comic's page
	CHECK_EQ(near(find(e.placements(), 0).h, 600), true);
	CHECK_EQ(e.current(), 0);
}

static void testMangaStore()
{
	// an extension's series found again under a new id: its entry, heart and chapters' places move over
	{
		MangaStore m;
		MangaSeries old;
		old.id = "src:java:9:1749";
		old.title = "Genius";
		old.added = 500;
		old.inLibrary = true;
		m.putSeries(old);
		ChapterProgress c1;
		c1.seriesId = old.id;
		c1.chapterId = "src:java:9:20";
		c1.chapterTitle = "Chapter 1";
		c1.page = 4;
		c1.pages = 10;
		c1.at = 900;
		m.setProgress(c1);
		MangaSeries fresh;
		fresh.id = "src:java:9:3";
		fresh.title = "Genius";
		fresh.added = 800;
		fresh.inLibrary = false;
		m.putSeries(fresh);
		m.moveSeries(old.id, fresh.id);
		CHECK_EQ(m.series(old.id) == nullptr, true);
		CHECK_EQ(m.series(fresh.id)->inLibrary, true);
		CHECK_EQ(m.series(fresh.id)->added, (int64_t)500);
		CHECK_EQ(m.progressOf(fresh.id).size(), (size_t)1);
		m.moveChapters({{"src:java:9:20", "src:java:9:7"}});
		CHECK_EQ(m.progress("src:java:9:20") == nullptr, true);
		CHECK_EQ(m.progress("src:java:9:7")->page, 4);
		CHECK_EQ(m.progress("src:java:9:7")->seriesId, std::string("src:java:9:3"));
	}
	// Incognito mode: the place is kept, the History doesn't move (a new chapter hidden, a known one keeps its time)
	{
		MangaStore a;
		MangaSeries sr;
		sr.id = "local:/anon";
		sr.title = "Anon";
		a.putSeries(sr);
		ChapterProgress seen;
		seen.seriesId = sr.id;
		seen.chapterId = "anon-1";
		seen.page = 2;
		seen.pages = 10;
		seen.at = 1000;
		a.setProgress(seen);
		ChapterProgress quiet = seen;
		quiet.page = 5;
		quiet.at = 2000;
		a.setProgress(quiet, false);
		CHECK_EQ(a.progress("anon-1")->page, 5);
		CHECK_EQ(a.progress("anon-1")->at, (int64_t)1000);
		CHECK_EQ(a.series(sr.id)->lastRead, (int64_t)1000);
		ChapterProgress fresh = seen;
		fresh.chapterId = "anon-2";
		fresh.page = 1;
		fresh.at = 3000;
		a.setProgress(fresh, false);
		CHECK_EQ(a.progress("anon-2")->page, 1);
		CHECK_EQ(a.progress("anon-2")->hidden, true);
		CHECK_EQ(a.history(10).size(), (size_t)1);
		CHECK_EQ(a.history(10)[0].chapterId, std::string("anon-1"));
	}
	char tmpl[] = "/tmp/bp-manga-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string root = made;
	std::string state = root + "/state";

	MangaStore st;
	CHECK_EQ(st.load(state), true);		// no files yet: an empty store
	CHECK_EQ(st.library().size(), size_t(0));
	CHECK_EQ(st.history(10).size(), size_t(0));
	CHECK_EQ(st.continueReading(10).size(), size_t(0));

	uint64_t serial = st.serial();
	MangaSeries a;
	a.id = "local:/data/Manga/Ember Lantern";
	a.title = "Ember Lantern";
	a.source = "local";
	a.cover = "/data/Manga/cover.jpg";
	a.added = 1000;
	st.putSeries(a);
	CHECK_EQ(st.serial() > serial, true);	// every change grows the serial

	MangaSeries b;
	b.id = "local:/data/Manga/Quiet Harbor";
	b.title = "Quiet Harbor";
	b.source = "local";
	b.added = 2000;
	b.readingMode = "strip";
	st.putSeries(b);

	// a title with the characters a TSV can't hold
	MangaSeries c;
	c.id = "local:/data/Manga/Tab\tName";
	c.title = "Tab\there\nNew line\\Back";
	c.source = "local";
	c.added = 3000;
	st.putSeries(c);

	// a chapter read to the middle: not read
	ChapterProgress p;
	p.seriesId = a.id;
	p.chapterId = "local:/data/Manga/Ember Lantern/Ch 01";
	p.chapterTitle = "Chapter 1";
	p.page = 4;
	p.pages = 20;
	p.at = 5000;
	st.setProgress(p);

	// a chapter read to its last page: read
	ChapterProgress last;
	last.seriesId = a.id;
	last.chapterId = "local:/data/Manga/Ember Lantern/Ch 02";
	last.chapterTitle = "Chapter 2";
	last.page = 19;
	last.pages = 20;
	last.at = 6000;
	st.setProgress(last);

	// a chapter of another series
	ChapterProgress q;
	q.seriesId = b.id;
	q.chapterId = "local:/data/Manga/Quiet Harbor/Ch 01";
	q.chapterTitle = "Chapter 1";
	q.page = 2;
	q.pages = 15;
	q.at = 7000;
	st.setProgress(q);

	const ChapterProgress* e = st.progress(p.chapterId);
	CHECK_EQ(e != nullptr, true);
	if (e)
	{
		CHECK_EQ(e->read, false);
		CHECK_EQ(e->page, 4);
		CHECK_EQ(e->pages, 20);
	}
	e = st.progress(last.chapterId);
	CHECK_EQ(e != nullptr, true);
	if (e) CHECK_EQ(e->read, true);
	// the series was read when its chapter was
	const MangaSeries* sa = st.series(a.id);
	CHECK_EQ(sa != nullptr, true);
	if (sa) CHECK_EQ(sa->lastRead, (int64_t)6000);

	// saved and read back into another store
	CHECK_EQ(st.save(), true);
	MangaStore st2;
	CHECK_EQ(st2.load(state), true);
	std::vector<MangaSeries> lib = st2.library();
	CHECK_EQ(lib.size(), size_t(3));
	if (lib.size() == 3)
	{
		CHECK_EQ(lib[0].id, b.id);		// the one read most recently first (7000)
		CHECK_EQ(lib[0].title, b.title);
		CHECK_EQ(lib[0].source, b.source);
		CHECK_EQ(lib[0].cover, std::string(""));
		CHECK_EQ(lib[0].added, (int64_t)2000);
		CHECK_EQ(lib[0].lastRead, (int64_t)7000);
		CHECK_EQ(lib[0].inLibrary, true);
		CHECK_EQ(lib[0].readingMode, std::string("strip"));
		CHECK_EQ(lib[1].id, a.id);		// then the one read before it (6000)
		CHECK_EQ(lib[1].title, a.title);
		CHECK_EQ(lib[1].source, a.source);
		CHECK_EQ(lib[1].cover, a.cover);
		CHECK_EQ(lib[1].added, (int64_t)1000);
		CHECK_EQ(lib[1].lastRead, (int64_t)6000);
		CHECK_EQ(lib[1].inLibrary, true);
		CHECK_EQ(lib[1].readingMode, std::string(""));
		CHECK_EQ(lib[2].id, c.id);		// then the one never read, by title
		CHECK_EQ(lib[2].title, c.title);	// the tabs, the newline and the backslash came back
		CHECK_EQ(lib[2].source, c.source);
		CHECK_EQ(lib[2].added, (int64_t)3000);
		CHECK_EQ(lib[2].lastRead, (int64_t)0);
		CHECK_EQ(lib[2].inLibrary, true);
	}
	e = st2.progress(p.chapterId);
	CHECK_EQ(e != nullptr, true);
	if (e)
	{
		CHECK_EQ(e->seriesId, p.seriesId);
		CHECK_EQ(e->chapterId, p.chapterId);
		CHECK_EQ(e->chapterTitle, p.chapterTitle);
		CHECK_EQ(e->page, 4);
		CHECK_EQ(e->pages, 20);
		CHECK_EQ(e->read, false);
		CHECK_EQ(e->at, (int64_t)5000);
		CHECK_EQ(e->hidden, false);
	}
	e = st2.progress(last.chapterId);
	CHECK_EQ(e != nullptr, true);
	if (e) CHECK_EQ(e->read, true);
	CHECK_EQ(st2.progress("local:/nothing") == nullptr, true);

	// the files say what they are
	{
		std::ifstream f(state + "/manga/library.tsv");
		std::string line;
		std::getline(f, line);
		CHECK_EQ(line, std::string("# banana-player manga library v1"));
		std::ifstream g(state + "/manga/progress.tsv");
		std::getline(g, line);
		CHECK_EQ(line, std::string("# banana-player manga progress v1"));
		std::getline(g, line);	// a chapter's line: the hidden column is the last one
		CHECK_EQ(line.size() > 2 && line.compare(line.size() - 2, 2, "\t0") == 0, true);
	}

	// continue reading: each series' latest unfinished chapter, newest first
	std::vector<ChapterProgress> cont = st.continueReading(10);
	CHECK_EQ(cont.size(), size_t(2));
	if (cont.size() == 2)
	{
		CHECK_EQ(cont[0].chapterId, q.chapterId);
		CHECK_EQ(cont[1].chapterId, p.chapterId);
	}
	CHECK_EQ(st.continueReading(1).size(), size_t(1));
	if (!st.continueReading(1).empty()) CHECK_EQ(st.continueReading(1)[0].chapterId, q.chapterId);
	CHECK_EQ(st.continueReading(0).size(), size_t(0));

	// the history: every chapter opened, newest first
	std::vector<ChapterProgress> hist = st.history(10);
	CHECK_EQ(hist.size(), size_t(3));
	if (hist.size() == 3)
	{
		CHECK_EQ(hist[0].chapterId, q.chapterId);
		CHECK_EQ(hist[1].chapterId, last.chapterId);
		CHECK_EQ(hist[2].chapterId, p.chapterId);
	}
	CHECK_EQ(st.history(2).size(), size_t(2));

	// the history as series rows: one per series, its newest chapter, newest first
	std::vector<ChapterProgress> bySeries = st.historyBySeries(10);
	CHECK_EQ(bySeries.size(), size_t(2));
	if (bySeries.size() == 2)
	{
		CHECK_EQ(bySeries[0].chapterId, q.chapterId);		// Quiet Harbor's one chapter, the newest (7000)
		CHECK_EQ(bySeries[1].chapterId, last.chapterId);	// Ember Lantern's newest (6000), not p (5000)
	}
	CHECK_EQ(st.historyBySeries(1).size(), size_t(1));
	if (!st.historyBySeries(1).empty()) CHECK_EQ(st.historyBySeries(1)[0].chapterId, q.chapterId);
	CHECK_EQ(st.historyBySeries(0).size(), size_t(0));

	// a newer chapter takes its series' row rather than adding one
	ChapterProgress newer;
	newer.seriesId = a.id;
	newer.chapterId = "local:/data/Manga/Ember Lantern/Ch 05";
	newer.chapterTitle = "Chapter 5";
	newer.page = 1;
	newer.pages = 10;
	newer.at = 9000;
	st.setProgress(newer);
	bySeries = st.historyBySeries(10);
	CHECK_EQ(bySeries.size(), size_t(2));
	if (bySeries.size() == 2)
	{
		CHECK_EQ(bySeries[0].chapterId, newer.chapterId);
		CHECK_EQ(bySeries[1].chapterId, q.chapterId);
	}
	// a hidden chapter leaves the row to the series' next newest one; all hidden: no series row
	st.clearHistorySince(9000);
	bySeries = st.historyBySeries(10);
	CHECK_EQ(bySeries.size(), size_t(2));
	if (bySeries.size() == 2) CHECK_EQ(bySeries[1].chapterId, last.chapterId);

	// marked read and unread
	std::vector<std::string> chapters = {"local:/data/Manga/Ember Lantern/Ch 01", "local:/data/Manga/Ember Lantern/Ch 03"};
	CHECK_EQ(st.unread(a.id, chapters), 2);
	st.markRead(a.id, chapters, true);
	CHECK_EQ(st.unread(a.id, chapters), 0);
	e = st.progress("local:/data/Manga/Ember Lantern/Ch 03");
	CHECK_EQ(e != nullptr, true);
	if (e) CHECK_EQ(e->read, true);
	st.markRead(a.id, chapters, false);
	CHECK_EQ(st.unread(a.id, chapters), 2);
	// a chapter of another series doesn't answer for this one
	st.markRead(a.id, {"local:/data/Manga/Ember Lantern/Ch 01"}, true);
	CHECK_EQ(st.unread(b.id, {"local:/data/Manga/Ember Lantern/Ch 01"}), 1);

	// taking a series out of the library and back
	st.setInLibrary(a.id, false);
	CHECK_EQ(st.library().size(), size_t(2));
	CHECK_EQ(st.series(a.id) != nullptr, true);
	st.setInLibrary(a.id, true);
	CHECK_EQ(st.library().size(), size_t(3));

	// a series goes with its chapters' progress
	st.removeSeries(b.id);
	CHECK_EQ(st.series(b.id) == nullptr, true);
	CHECK_EQ(st.progress(q.chapterId) == nullptr, true);
	CHECK_EQ(st.library().size(), size_t(2));

	// a chapter opened again: it is unfinished, so it is in the Continue reading strip
	ChapterProgress again;
	again.seriesId = a.id;
	again.chapterId = "local:/data/Manga/Ember Lantern/Ch 04";
	again.chapterTitle = "Chapter 4";
	again.page = 2;
	again.pages = 15;
	again.at = 8000;
	st.setProgress(again);

	// the history cleared: the History list goes, the reading progress stays
	st.clearHistory();
	CHECK_EQ(st.history(50).size(), size_t(0));
	CHECK_EQ(st.historyBySeries(50).size(), size_t(0));	// (its rows go with it)
	CHECK_EQ(st.library().size(), size_t(2));
	// the read mark and the page to resume are still there
	e = st.progress(p.chapterId);
	CHECK_EQ(e != nullptr, true);
	if (e)
	{
		CHECK_EQ(e->read, true);
		CHECK_EQ(e->page, 4);
		CHECK_EQ(e->pages, 20);
	}
	e = st.progress("local:/data/Manga/Ember Lantern/Ch 03");
	CHECK_EQ(e != nullptr, true);
	if (e) CHECK_EQ(e->read, false);
	// so the unread counts are the ones before the clear
	CHECK_EQ(st.unread(a.id, chapters), 1);
	// and the Continue reading strip goes with the History (the owner's call: a clear clears what was being read);
	// the place itself stays, so opening the chapter again resumes there
	std::vector<ChapterProgress> cont2 = st.continueReading(10);
	CHECK_EQ(cont2.size(), size_t(0));

	// a chapter read again is in the History again
	st.setProgress(again);
	std::vector<ChapterProgress> hist2 = st.history(50);
	CHECK_EQ(hist2.size(), size_t(1));	// the chapters cleared before are still out of it
	if (!hist2.empty()) CHECK_EQ(hist2[0].chapterId, again.chapterId);

	// a broken line in either file is skipped; the rest of the file loads
	std::string bad = root + "/bad";
	mkdir(bad.c_str(), 0755);
	mkdir((bad + "/manga").c_str(), 0755);
	{
		std::ofstream f(bad + "/manga/library.tsv");
		f << "# banana-player manga library v1\n";
		f << "local:/A\tAlpha\tlocal\t\t10\t20\t1\t\n";
		f << "this line has no fields\n";
		f << "local:/B\tBeta\tlocal\t\t30\t40\t0\tstrip\n";
		f << "local:/C\tToo\tfew\n";
		f << "local:/D\tDelta\tlocal\t\t50\t60\t1\tltr\tcolumns\ta\tlater\n";	// a later version's extra columns
	}
	{
		std::ofstream f(bad + "/manga/progress.tsv");
		f << "# banana-player manga progress v1\n";
		f << "local:/A\tlocal:/A/c1\tOne\t3\t10\t0\t700\n";		// an older file: no hidden column
		f << "rubbish\n";
		f << "local:/A\tlocal:/A/c2\tTwo\n";
		f << "local:/A\tlocal:/A/c3\tThree\t1\t5\t0\t800\tignored\n";	// a later version's extra column
		f << "local:/A\tlocal:/A/c4\tFour\t2\t8\t0\t900\t1\n";		// cleared from the History
		f << "local:/A\tlocal:/A/c5\tFive\t2\t8\t0\t950\t0\n";
	}
	MangaStore broken;
	CHECK_EQ(broken.load(bad), true);
	CHECK_EQ(broken.library().size(), size_t(2));	// B is out of the library
	CHECK_EQ(broken.series("local:/A") != nullptr, true);
	CHECK_EQ(broken.series("local:/B") != nullptr, true);
	CHECK_EQ(broken.series("local:/C") == nullptr, true);
	CHECK_EQ(broken.series("local:/D") != nullptr, true);
	if (const MangaSeries* d = broken.series("local:/D"))
	{
		CHECK_EQ(d->title, std::string("Delta"));
		CHECK_EQ(d->readingMode, std::string("ltr"));
		CHECK_EQ(d->lastRead, (int64_t)60);
	}
	CHECK_EQ(broken.progress("local:/A/c1") != nullptr, true);
	CHECK_EQ(broken.progress("local:/A/c2") == nullptr, true);
	CHECK_EQ(broken.progress("local:/A/c3") != nullptr, true);
	if (const ChapterProgress* bp1 = broken.progress("local:/A/c1"))
	{
		CHECK_EQ(bp1->page, 3);
		CHECK_EQ(bp1->pages, 10);
		CHECK_EQ(bp1->at, (int64_t)700);
		CHECK_EQ(bp1->hidden, false);	// a file without the column loads: its chapters are in the History
	}
	// the hidden column: a cleared chapter is out of the History, an unreadable value says nothing
	if (const ChapterProgress* bp3 = broken.progress("local:/A/c3")) CHECK_EQ(bp3->hidden, false);
	if (const ChapterProgress* bp4 = broken.progress("local:/A/c4"))
	{
		CHECK_EQ(bp4->hidden, true);
		CHECK_EQ(bp4->page, 2);
	}
	if (const ChapterProgress* bp5 = broken.progress("local:/A/c5")) CHECK_EQ(bp5->hidden, false);
	std::vector<ChapterProgress> bhist = broken.history(10);
	CHECK_EQ(bhist.size(), size_t(3));	// c5, c3 and c1: c4 was cleared

	// a progress.tsv written before the hidden column loads: its chapters are in the History
	std::string old = root + "/old";
	mkdir(old.c_str(), 0755);
	mkdir((old + "/manga").c_str(), 0755);
	{
		std::ofstream f(old + "/manga/progress.tsv");
		f << "# banana-player manga progress v1\n";
		f << "local:/A\tlocal:/A/c1\tOne\t3\t10\t0\t700\n";
	}
	MangaStore older;
	CHECK_EQ(older.load(old), true);
	CHECK_EQ(older.progress("local:/A/c1") != nullptr, true);
	if (const ChapterProgress* o1 = older.progress("local:/A/c1"))
	{
		CHECK_EQ(o1->page, 3);
		CHECK_EQ(o1->read, false);
		CHECK_EQ(o1->hidden, false);
	}
	CHECK_EQ(older.history(10).size(), size_t(1));

	// a folder that isn't there: an empty store, not a failure
	MangaStore none;
	CHECK_EQ(none.load(root + "/nothing"), true);
	CHECK_EQ(none.library().size(), size_t(0));
	CHECK_EQ(none.history(10).size(), size_t(0));
	CHECK_EQ(none.progress(p.chapterId) == nullptr, true);

	// the folders are gone again
	unlink((state + "/manga/library.tsv").c_str());
	unlink((state + "/manga/progress.tsv").c_str());
	rmdir((state + "/manga").c_str());
	rmdir(state.c_str());
	unlink((bad + "/manga/library.tsv").c_str());
	unlink((bad + "/manga/progress.tsv").c_str());
	rmdir((bad + "/manga").c_str());
	rmdir(bad.c_str());
	unlink((old + "/manga/progress.tsv").c_str());
	rmdir((old + "/manga").c_str());
	rmdir(old.c_str());
	rmdir(root.c_str());
}

// the scanner over a real temporary tree: folders and CBZs the same bytes as tests/pages, so the fixture archives
// carry their ComicInfo (Series "Test Series", Number 3, Volume 1) into the checks
static void testMangaLocal()
{
	const std::string P = std::string(BP_TESTS_DIR) + "/pages/";
	auto bytes = [&](const std::string& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};
	const std::vector<uint8_t> jpg = bytes(P + "page.jpg"), cbz = bytes(P + "stored.cbz");
	auto mk = [&](const std::string& p) { CHECK_EQ(mkdir(p.c_str(), 0755), 0); };
	auto img = [&](const std::string& p)
	{
		std::ofstream w(p, std::ios::binary);
		w.write((const char*)jpg.data(), (std::streamsize)jpg.size());
	};
	auto arc = [&](const std::string& p)
	{
		std::ofstream w(p, std::ios::binary);
		w.write((const char*)cbz.data(), (std::streamsize)cbz.size());
	};

	char tpl[] = "/tmp/bp-mangalocal-XXXXXX";
	char* made = mkdtemp(tpl);
	CHECK_EQ(made != nullptr, true);
	if (!made) return;
	const std::string D = made, M = D + "/M";	// M is the one Manga folder being scanned

	mk(M);
	// a series folder with numbered chapter folders: the order is 1.5, 2, 10, not by name
	mk(M + "/Chaptered");
	mk(M + "/Chaptered/Chapter 1.5");
	mk(M + "/Chaptered/Chapter 2");
	mk(M + "/Chaptered/Chapter 10");
	img(M + "/Chaptered/Chapter 1.5/001.jpg");
	img(M + "/Chaptered/Chapter 2/001.jpg");
	img(M + "/Chaptered/Chapter 2/002.jpg");
	img(M + "/Chaptered/Chapter 10/001.jpg");
	// a series of CBZ chapters: (volume, number) order, and the title every ComicInfo agrees on
	mk(M + "/Cbz Chapters");
	arc(M + "/Cbz Chapters/Vol.1 Ch.3.cbz");
	arc(M + "/Cbz Chapters/Vol.1 Ch.1.cbz");
	// volume folders: the chapters are one level deeper, the volume comes from the folder that holds them
	mk(M + "/Volumes");
	mk(M + "/Volumes/Vol 2");
	mk(M + "/Volumes/Vol 2/Ch 5");
	mk(M + "/Volumes/Vol 1");
	mk(M + "/Volumes/Vol 1/Ch 9");
	img(M + "/Volumes/Vol 2/Ch 5/001.jpg");
	img(M + "/Volumes/Vol 1/Ch 9/001.jpg");
	// a loose folder of images: a one-chapter series named after the folder
	mk(M + "/Loose Images");
	img(M + "/Loose Images/001.jpg");
	img(M + "/Loose Images/002.jpg");
	img(M + "/Loose Images/003.jpg");
	// a loose CBZ in root: a one-chapter series; the name's number wins over ComicInfo's
	arc(M + "/Loose 7.cbz");
	std::ofstream(M + "/notes.txt") << "x";	// not manga, not a chapter: nothing to see
	// chapters without numbers read in natural order
	mk(M + "/NoNumbers");
	mk(M + "/NoNumbers/Prologue");
	mk(M + "/NoNumbers/Epilogue");
	img(M + "/NoNumbers/Prologue/001.jpg");
	img(M + "/NoNumbers/Epilogue/001.jpg");
	// an archive the name says nothing about: ComicInfo gives number, volume and the series title
	mk(M + "/Meta");
	arc(M + "/Meta/whatever.cbz");
	// what the scan must leave alone
	mk(M + "/Empty");
	mk(M + "/Junk");
	std::ofstream(M + "/Junk/notes.txt") << "x";
	mk(M + "/.Hidden");
	img(M + "/.Hidden/001.jpg");
	mk(M + "/__MACOSX");
	img(M + "/__MACOSX/001.jpg");

	// series sorted naturally by title; the three "Test Series" tie on their paths
	std::vector<LocalSeries> out = scanMangaFolder(M);
	CHECK_EQ(out.size(), (size_t)7);
	auto byPath = [&](const std::string& p) -> const LocalSeries*
	{
		for (const LocalSeries& s : out)
			if (s.path == p) return &s;
		return nullptr;
	};
	if (out.size() == 7)
	{
		CHECK_EQ(out[0].title, std::string("Chaptered"));
		CHECK_EQ(out[1].title, std::string("Loose Images"));
		CHECK_EQ(out[2].title, std::string("NoNumbers"));
		CHECK_EQ(out[3].title, std::string("Test Series"));
		CHECK_EQ(out[3].path, M + "/Cbz Chapters");
		CHECK_EQ(out[4].path, M + "/Loose 7.cbz");
		CHECK_EQ(out[5].path, M + "/Meta");
		CHECK_EQ(out[6].title, std::string("Volumes"));
	}

	if (const LocalSeries* s = byPath(M + "/Chaptered"))
	{
		CHECK_EQ(s->title, std::string("Chaptered"));
		CHECK_EQ(s->cover, M + "/Chaptered/Chapter 1.5/001.jpg");
		CHECK_EQ(s->chapters.size(), (size_t)3);
		if (s->chapters.size() == 3)
		{
			CHECK_EQ(s->chapters[0].title, std::string("Chapter 1.5"));
			CHECK_EQ(s->chapters[0].number, 1.5);
			CHECK_EQ(s->chapters[0].pages, 1);
			CHECK_EQ(s->chapters[0].archive, false);
			CHECK_EQ(s->chapters[1].title, std::string("Chapter 2"));
			CHECK_EQ(s->chapters[1].number, 2.0);
			CHECK_EQ(s->chapters[1].pages, 2);
			CHECK_EQ(s->chapters[2].title, std::string("Chapter 10"));
			CHECK_EQ(s->chapters[2].number, 10.0);
			CHECK_EQ(s->chapters[2].path, M + "/Chaptered/Chapter 10");
		}
	}
	if (const LocalSeries* s = byPath(M + "/Cbz Chapters"))
	{
		CHECK_EQ(s->title, std::string("Test Series"));	// both chapters' ComicInfo say it
		CHECK_EQ(s->chapters.size(), (size_t)2);
		if (s->chapters.size() == 2)
		{
			CHECK_EQ(s->chapters[0].title, std::string("Vol.1 Ch.1"));
			CHECK_EQ(s->chapters[0].number, 1.0);
			CHECK_EQ(s->chapters[0].volume, 1.0);
			CHECK_EQ(s->chapters[0].archive, true);
			CHECK_EQ(s->chapters[0].pages, 2);	// the fixture's two images, not its ComicInfo.xml
			CHECK_EQ(s->chapters[1].title, std::string("Vol.1 Ch.3"));
			CHECK_EQ(s->chapters[1].number, 3.0);
		}
		CHECK_EQ(s->cover, M + "/Cbz Chapters/Vol.1 Ch.1.cbz#001.jpg");
	}
	if (const LocalSeries* s = byPath(M + "/Volumes"))
	{
		CHECK_EQ(s->title, std::string("Volumes"));
		CHECK_EQ(s->chapters.size(), (size_t)2);
		if (s->chapters.size() == 2)
		{
			CHECK_EQ(s->chapters[0].title, std::string("Ch 9"));	// Vol 1 first, whatever the numbers say
			CHECK_EQ(s->chapters[0].volume, 1.0);
			CHECK_EQ(s->chapters[0].number, 9.0);
			CHECK_EQ(s->chapters[0].path, M + "/Volumes/Vol 1/Ch 9");
			CHECK_EQ(s->chapters[1].title, std::string("Ch 5"));
			CHECK_EQ(s->chapters[1].volume, 2.0);
			CHECK_EQ(s->chapters[1].number, 5.0);
		}
		CHECK_EQ(s->cover, M + "/Volumes/Vol 1/Ch 9/001.jpg");
	}
	if (const LocalSeries* s = byPath(M + "/Loose Images"))
	{
		CHECK_EQ(s->title, std::string("Loose Images"));
		CHECK_EQ(s->chapters.size(), (size_t)1);
		if (s->chapters.size() == 1)
		{
			CHECK_EQ(s->chapters[0].title, std::string("Loose Images"));
			CHECK_EQ(s->chapters[0].path, M + "/Loose Images");
			CHECK_EQ(s->chapters[0].number, -1.0);
			CHECK_EQ(s->chapters[0].pages, 3);
		}
		CHECK_EQ(s->cover, M + "/Loose Images/001.jpg");
	}
	if (const LocalSeries* s = byPath(M + "/NoNumbers"))
	{
		CHECK_EQ(s->chapters.size(), (size_t)2);
		if (s->chapters.size() == 2)
		{
			CHECK_EQ(s->chapters[0].title, std::string("Epilogue"));	// natural order, no numbers to go on
			CHECK_EQ(s->chapters[0].number, -1.0);
			CHECK_EQ(s->chapters[1].title, std::string("Prologue"));
		}
	}
	if (const LocalSeries* s = byPath(M + "/Meta"))
	{
		CHECK_EQ(s->title, std::string("Test Series"));	// the ComicInfo of the only chapter
		CHECK_EQ(s->chapters.size(), (size_t)1);
		if (s->chapters.size() == 1)
		{
			CHECK_EQ(s->chapters[0].title, std::string("whatever"));	// the name without the extension
			CHECK_EQ(s->chapters[0].number, 3.0);	// from ComicInfo: the name carries none
			CHECK_EQ(s->chapters[0].volume, 1.0);
			CHECK_EQ(s->chapters[0].pages, 2);
			CHECK_EQ(s->chapters[0].path, M + "/Meta/whatever.cbz");
		}
		CHECK_EQ(s->cover, M + "/Meta/whatever.cbz#001.jpg");
	}
	if (const LocalSeries* s = byPath(M + "/Loose 7.cbz"))
	{
		CHECK_EQ(s->title, std::string("Test Series"));
		CHECK_EQ(s->chapters.size(), (size_t)1);
		if (s->chapters.size() == 1)
		{
			CHECK_EQ(s->chapters[0].title, std::string("Loose 7"));
			CHECK_EQ(s->chapters[0].number, 7.0);	// the name's number wins over ComicInfo's 3
			CHECK_EQ(s->chapters[0].archive, true);
			CHECK_EQ(s->chapters[0].pages, 2);
		}
	}
	CHECK_EQ(byPath(M + "/Empty"), nullptr);
	CHECK_EQ(byPath(M + "/Junk"), nullptr);
	CHECK_EQ(byPath(M + "/.Hidden"), nullptr);
	CHECK_EQ(byPath(M + "/__MACOSX"), nullptr);
	CHECK_EQ(scanMangaFolder(M + "/gone").size(), (size_t)0);
	CHECK_EQ(scanMangaFolder(M + "/Chaptered").size(), (size_t)3);	// a chapter folder scanned as a root: the chapters are one level up

	// the places' Manga folders, each existing child folder once, nothing for a place without one
	mk(D + "/p1");
	mk(D + "/p1/Manga");
	img(D + "/p1/Manga/001.jpg");
	mk(D + "/p2");
	mk(D + "/p2/manga");
	mk(D + "/p3");
	mk(D + "/p4");
	std::ofstream(D + "/p4/Manga") << "not a folder";	// named Manga, but not a child folder
	CHECK_EQ(mangaRoots({D + "/p1", D + "/p2", D + "/p3", D + "/p4", D + "/gone"}),
		(std::vector<std::string>{D + "/p1/Manga", D + "/p2/manga"}));
	CHECK_EQ(mangaRoots({D + "/p1", D + "/p1/"}), (std::vector<std::string>{D + "/p1/Manga"}));	// no duplicates
	const std::vector<std::string> none;
	CHECK_EQ(mangaRoots(none), none);

	for (const std::string& f : {M + "/Loose 7.cbz", M + "/notes.txt", M + "/Chaptered/Chapter 1.5/001.jpg",
		M + "/Chaptered/Chapter 2/001.jpg", M + "/Chaptered/Chapter 2/002.jpg", M + "/Chaptered/Chapter 10/001.jpg",
		M + "/Cbz Chapters/Vol.1 Ch.3.cbz", M + "/Cbz Chapters/Vol.1 Ch.1.cbz",
		M + "/Volumes/Vol 2/Ch 5/001.jpg", M + "/Volumes/Vol 1/Ch 9/001.jpg",
		M + "/Loose Images/001.jpg", M + "/Loose Images/002.jpg", M + "/Loose Images/003.jpg",
		M + "/NoNumbers/Prologue/001.jpg", M + "/NoNumbers/Epilogue/001.jpg", M + "/Meta/whatever.cbz",
		M + "/Junk/notes.txt", M + "/.Hidden/001.jpg", M + "/__MACOSX/001.jpg",
		D + "/p1/Manga/001.jpg", D + "/p4/Manga"})
		unlink(f.c_str());
	for (const std::string& f : {M + "/Chaptered/Chapter 1.5", M + "/Chaptered/Chapter 2", M + "/Chaptered/Chapter 10",
		M + "/Chaptered", M + "/Cbz Chapters", M + "/Volumes/Vol 2/Ch 5", M + "/Volumes/Vol 2",
		M + "/Volumes/Vol 1/Ch 9", M + "/Volumes/Vol 1", M + "/Volumes", M + "/Loose Images", M + "/NoNumbers/Prologue",
		M + "/NoNumbers/Epilogue", M + "/NoNumbers", M + "/Meta", M + "/Empty", M + "/Junk", M + "/.Hidden",
		M + "/__MACOSX", M, D + "/p1/Manga", D + "/p1", D + "/p2/manga", D + "/p2", D + "/p3", D + "/p4", D})
		rmdir(f.c_str());
}

static void testMangaNetFolders()
{
	using Entry = NetEntry;
	auto dir = [](const std::string& name) { Entry e; e.name = name; e.dir = true; return e; };
	auto file = [](const std::string& name) { Entry e; e.name = name; e.dir = false; return e; };
	const std::string S = "bpnet://abcd/Share/Manga/Series";
	const std::string E = "bpnet://abcd/Share/Manga/Empty";

	// a series folder: chapter folders, archives, and its own pictures (ignored while there are chapters)
	std::vector<NetEntry> own = {
		dir("Chapter 10"), dir("Chapter 2"), dir("Chapter 1"), dir("Empty"),
		file("Vol.1 Ch.3.cbz"), file("whatever.zip"), file("notes.txt"), file(".hidden.jpg"), file("__MACOSX"),
		file("cover.jpg"),
	};
	NetMangaListing listing = netMangaListing(own);
	CHECK_EQ(listing.dirs, (std::vector<std::string>{"Chapter 1", "Chapter 2", "Chapter 10", "Empty"}));
	CHECK_EQ(listing.archives, (std::vector<std::string>{"Vol.1 Ch.3.cbz", "whatever.zip"}));
	CHECK_EQ(listing.images, (std::vector<std::string>{"cover.jpg"}));

	std::map<std::string, std::vector<NetEntry>> sub;
	sub[netJoin(S, "Chapter 1")] = {file("001.jpg")};
	sub[netJoin(S, "Chapter 2")] = {file("002.jpg"), file("001.jpg")};
	sub[netJoin(S, "Chapter 10")] = {file("010.jpg")};
	sub[netJoin(S, "Empty")] = {};
	NetMangaSeries series = netMangaSeriesFrom(S, "Series", own, sub);
	CHECK_EQ(series.title, std::string("Series"));
	CHECK_EQ(series.path, S);
	CHECK_EQ(series.chapters.size(), (size_t)5);
	if (series.chapters.size() == 5)
	{
		// no number on "whatever", so the order is natural, as the card's scan orders it
		CHECK_EQ(series.chapters[0].title, std::string("Chapter 1"));
		CHECK_EQ(series.chapters[0].path, netJoin(S, "Chapter 1"));
		CHECK_EQ(series.chapters[0].archive, false);
		CHECK_EQ(series.chapters[0].pages, 1);
		CHECK_EQ(series.chapters[1].title, std::string("Chapter 2"));
		CHECK_EQ(series.chapters[1].pages, 2);
		CHECK_EQ(series.chapters[2].title, std::string("Chapter 10"));
		CHECK_EQ(series.chapters[3].title, std::string("Vol.1 Ch.3"));
		CHECK_EQ(series.chapters[3].archive, true);
		CHECK_EQ(series.chapters[3].pages, -1);
		CHECK_EQ(series.chapters[4].title, std::string("whatever"));
	}

	// a folder of pictures alone: a one-chapter series named after the folder
	NetMangaSeries one = netMangaSeriesFrom(E, "Loose", {file("003.jpg"), file("001.jpg"), file("002.jpg")}, {});
	CHECK_EQ(one.chapters.size(), (size_t)1);
	if (one.chapters.size() == 1)
	{
		CHECK_EQ(one.chapters[0].title, std::string("Loose"));
		CHECK_EQ(one.chapters[0].path, E);
		CHECK_EQ(one.chapters[0].archive, false);
		CHECK_EQ(one.chapters[0].pages, 3);
	}

	// nothing to read: no pictures, no archive, and a child folder we couldn't list or that holds no pictures
	CHECK_EQ(netMangaSeriesFrom(E, "Empty", {file("notes.txt")}, {}).chapters.size(), (size_t)0);
	CHECK_EQ(netMangaSeriesFrom(E, "Empty", {dir("Volume 1")}, {}).chapters.size(), (size_t)0);
	std::map<std::string, std::vector<NetEntry>> deep;
	deep[netJoin(E, "Volume 1")] = {dir("Chapter 1")};
	// the design's network series is direct: a volume folder's children are not taken as chapters here
	CHECK_EQ(netMangaSeriesFrom(E, "Empty", {dir("Volume 1")}, deep).chapters.size(), (size_t)0);

	// an archive folder: its chapters by name, the archive's pages unknown until it is fetched
	NetMangaSeries arcs = netMangaSeriesFrom(E, "Archives", {file("Ch 2.cbz"), file("Ch 10.zip"), file("Ch 1.cbz")}, {});
	CHECK_EQ(arcs.chapters.size(), (size_t)3);
	if (arcs.chapters.size() == 3)
	{
		CHECK_EQ(arcs.chapters[0].title, std::string("Ch 1"));
		CHECK_EQ(arcs.chapters[1].title, std::string("Ch 2"));
		CHECK_EQ(arcs.chapters[2].title, std::string("Ch 10"));
		CHECK_EQ(arcs.chapters[2].archive, true);
	}

	// a numbered order (all chapters carry a number) goes by volume then number, as the card's scan does
	std::vector<NetEntry> numbered = {dir("Chapter 10"), dir("Chapter 2"), file("Vol.1 Ch.3.cbz"), file("Vol.2 Ch.1.cbz")};
	std::map<std::string, std::vector<NetEntry>> nsub;
	for (const std::string& d : {"Chapter 10", "Chapter 2"}) nsub[netJoin(E, d)] = {file("001.jpg")};
	NetMangaSeries ns = netMangaSeriesFrom(E, "Numbered", numbered, nsub);
	CHECK_EQ(ns.chapters.size(), (size_t)4);
	if (ns.chapters.size() == 4)
	{
		CHECK_EQ(ns.chapters[0].title, std::string("Chapter 2"));
		CHECK_EQ(ns.chapters[1].title, std::string("Chapter 10"));
		CHECK_EQ(ns.chapters[2].title, std::string("Vol.1 Ch.3"));
		CHECK_EQ(ns.chapters[3].title, std::string("Vol.2 Ch.1"));
	}
}

static void testComicInfo()
{
	ComicInfo c;
	// a typical file, as the downloads write it
	CHECK_EQ(parseComicInfo(R"(<?xml version="1.0"?>
<ComicInfo>
  <Series>Yotsuba</Series>
  <Number>3</Number>
  <Volume>1</Volume>
  <Title>Chapter Three</Title>
  <PageCount>2</PageCount>
  <Manga>YesAndRightToLeft</Manga>
</ComicInfo>
)", c), true);
	CHECK_EQ(c.series, std::string("Yotsuba"));
	CHECK_EQ(c.number, std::string("3"));
	CHECK_EQ(c.volume, 1);
	CHECK_EQ(c.title, std::string("Chapter Three"));
	CHECK_EQ(c.pageCount, 2);
	CHECK_EQ(c.rightToLeft, true);
	CHECK_EQ(c.summary, std::string(""));
	CHECK_EQ(c.sourceId, std::string(""));

	// entities, decimal and hex references (a multi-byte one, and an astral one), and the bad references
	CHECK_EQ(parseComicInfo(R"(<ComicInfo><Title>Tom &amp; Jerry &lt;3 &gt; &quot;q&quot; &apos;a&apos; &#65;&#x3042;&#x1F600;</Title>)"
	                        R"(<Summary>a&#0;b&#xD800;c&bogus;d&#;e</Summary><Writer>AT&T</Writer></ComicInfo>)", c), true);
	CHECK_EQ(c.title, std::string("Tom & Jerry <3 > \"q\" 'a' A\xE3\x81\x82\xF0\x9F\x98\x80"));
	CHECK_EQ(c.summary, std::string("abc&bogus;d&#;e"));	// NUL and a surrogate dropped; not references stay as written
	CHECK_EQ(c.writer, std::string("AT&T"));

	// CDATA is literal, and trimmed like text
	CHECK_EQ(parseComicInfo(R"(<ComicInfo><Summary>
  <![CDATA[a <b> & "c" &amp;]]>
</Summary></ComicInfo>)", c), true);
	CHECK_EQ(c.summary, std::string("a <b> & \"c\" &amp;"));

	// self-closing elements are empty; unknown elements are skipped with their children, which can't override a field
	CHECK_EQ(parseComicInfo(R"(<ComicInfo><Series>S</Series><Summary/><Title>top</Title>)"
	                        R"(<Pages><Page Image="0"/><Title>nested</Title></Pages><Writer>W</Writer></ComicInfo>)", c), true);
	CHECK_EQ(c.series, std::string("S"));
	CHECK_EQ(c.summary, std::string(""));
	CHECK_EQ(c.title, std::string("top"));
	CHECK_EQ(c.writer, std::string("W"));

	// the declaration, comments and attributes (a '>' inside a quoted value) are not content
	CHECK_EQ(parseComicInfo(R"(<?xml version="1.0" encoding="utf-8"?>
<!-- made by a tool, <Series>not this</Series> -->
<ComicInfo xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:note='x > y'>
<Series lang="en">Real</Series></ComicInfo>)", c), true);
	CHECK_EQ(c.series, std::string("Real"));

	// Manga: only YesAndRightToLeft turns the reading direction
	for (const char* v : {"Yes", "No", "Unknown"}) {
		CHECK_EQ(parseComicInfo(std::string("<ComicInfo><Manga>") + v + "</Manga></ComicInfo>", c), true);
		CHECK_EQ(c.rightToLeft, false);
	}
	CHECK_EQ(parseComicInfo("<ComicInfo><Manga> YesAndRightToLeft </Manga></ComicInfo>", c), true);
	CHECK_EQ(c.rightToLeft, true);

	// numbers that aren't whole stay unset
	CHECK_EQ(parseComicInfo("<ComicInfo><Volume>two</Volume><PageCount>1.5</PageCount></ComicInfo>", c), true);
	CHECK_EQ(c.volume, -1);
	CHECK_EQ(c.pageCount, -1);
	CHECK_EQ(parseComicInfo("<ComicInfo><Volume> 03 </Volume><PageCount/></ComicInfo>", c), true);
	CHECK_EQ(c.volume, 3);
	CHECK_EQ(c.pageCount, -1);

	// the Banana Player fields
	CHECK_EQ(parseComicInfo("<ComicInfo><BPSource>mangadex</BPSource><BPSeriesKey>abc-123</BPSeriesKey></ComicInfo>", c), true);
	CHECK_EQ(c.sourceId, std::string("mangadex"));
	CHECK_EQ(c.seriesKey, std::string("abc-123"));

	// no ComicInfo element: false, and the struct is left as it was
	c = ComicInfo();
	c.series = "keep";
	CHECK_EQ(parseComicInfo("<Foo><Series>x</Series></Foo>", c), false);
	CHECK_EQ(parseComicInfo("", c), false);
	CHECK_EQ(c.series, std::string("keep"));

	// malformed and truncated input: false, and no crash or hang
	for (const char* bad : {"<ComicInfo><Series>x</Series>", "<ComicInfo><Series>x", "<ComicInfo", "<", "<ComicInfo>a < b</ComicInfo>",
	                        "<ComicInfo><!-- open", "<ComicInfo><![CDATA[ open", "<ComicInfo><?pi", "<ComicInfo></ComicInfo",
	                        "<ComicInfo><Series>x</Other></ComicInfo>", "<ComicInfo><Series a=></Series></ComicInfo>",
	                        "<ComicInfo><Series a=\"x></Series></ComicInfo>", "<ComicInfo/ >"}) {
		CHECK_EQ(parseComicInfo(bad, c), false);
	}
	std::string deep;
	for (int i = 0; i < 300; i++) deep += "<x>";
	CHECK_EQ(parseComicInfo(deep + "<ComicInfo></ComicInfo>", c), false);
	// the 1 MB cap refuses rather than reads
	CHECK_EQ(parseComicInfo("<ComicInfo>" + std::string((1 << 20) + 1, ' ') + "</ComicInfo>", c), false);

	// the round trip: a filled struct comes back as it was, an empty one as a default
	ComicInfo f;
	f.series = "Yotsuba & friends";
	f.title = "Tom \"T\" <x>";
	f.number = "10.5";
	f.summary = "line one\nline two";
	f.writer = "A. B";
	f.web = "https://example.com/?a=1&b=2";
	f.volume = 2;
	f.pageCount = 20;
	f.rightToLeft = true;
	f.sourceId = "mangadex";
	f.seriesKey = "abc-123";
	ComicInfo g;
	CHECK_EQ(parseComicInfo(writeComicInfo(f), g), true);
	CHECK_EQ(g.series, f.series);
	CHECK_EQ(g.title, f.title);
	CHECK_EQ(g.number, f.number);
	CHECK_EQ(g.summary, f.summary);
	CHECK_EQ(g.writer, f.writer);
	CHECK_EQ(g.web, f.web);
	CHECK_EQ(g.volume, f.volume);
	CHECK_EQ(g.pageCount, f.pageCount);
	CHECK_EQ(g.rightToLeft, f.rightToLeft);
	CHECK_EQ(g.sourceId, f.sourceId);
	CHECK_EQ(g.seriesKey, f.seriesKey);

	ComicInfo e;
	std::string ex = writeComicInfo(e);
	CHECK_EQ(ex.find("<Series>"), std::string::npos);
	CHECK_EQ(ex.find("<Manga>"), std::string::npos);
	CHECK_EQ(parseComicInfo(ex, g), true);
	CHECK_EQ(g.series, std::string(""));
	CHECK_EQ(g.volume, -1);
	CHECK_EQ(g.pageCount, -1);
	CHECK_EQ(g.rightToLeft, false);

	// escaping: & < > " in a title; control characters dropped, tab kept
	ComicInfo t;
	t.title = "A & B < C > D \"E\"";
	CHECK_EQ(writeComicInfo(t).find("<Title>A &amp; B &lt; C &gt; D &quot;E&quot;</Title>") != std::string::npos, true);
	t.title = "x\x01y\tz";
	CHECK_EQ(writeComicInfo(t).find("<Title>xy\tz</Title>") != std::string::npos, true);
}

static void testBands()
{
	// the layout: bands of the image's own rows, a short tail joined to the last
	BandLayout l{720, 20000};
	CHECK_EQ(l.count(), 10);
	CHECK_EQ(l.top(3), 3 * 2048);
	CHECK_EQ(l.rows(0), 2048);
	CHECK_EQ(l.rows(9), 20000 - 9 * 2048);
	int sum = 0;
	for (int b = 0; b < l.count(); b++) sum += l.rows(b);
	CHECK_EQ(sum, 20000);
	CHECK_EQ((BandLayout{720, 4096 + 100}.count()), 2);	// 100 rows are no band of their own
	CHECK_EQ((BandLayout{720, 4096 + 100}.rows(1)), 2148);
	CHECK_EQ((BandLayout{720, 4096 + 1024}.count()), 3);
	CHECK_EQ((BandLayout{720, 1000}.count()), 1);
	// which pages: over twice as tall as wide, and two bands at least
	CHECK_EQ(bandedPage(720, 20000), true);
	CHECK_EQ(bandedPage(720, 1440), false);
	CHECK_EQ(bandedPage(720, 3000), false);	// tall, but one band: the whole page is as cheap
	CHECK_EQ(bandedPage(720, 3072), true);
	CHECK_EQ(bandedPage(2000, 3072), false);
	CHECK_EQ(bandedPage(0, 0), false);
	// a band's decoded size: scaled to the decode width, never over the image's own
	int w = 0, h = 0;
	bandSize(l, 0, 640, w, h);
	CHECK_EQ(w, 640);
	CHECK_EQ(h, 1820);
	bandSize(l, 0, 2048, w, h);
	CHECK_EQ(w, 720);
	CHECK_EQ(h, 2048);
	// the bands a view needs: the page drawn 640 wide is 17778 tall, a band 1820.4; the view 480 tall
	double ph = 20000 * 640.0 / 720;
	// at the page's top: band 0 in view, band 1 not within two screens below
	CHECK_EQ(bandsNeeded(l, 0, ph, 480, 480, 960) == (std::vector<int>{0}), true);
	// 1000 px down: band 0 still in view; band 1 starts 820 px under the view's top, within the margin
	CHECK_EQ(bandsNeeded(l, -1000, ph, 480, 480, 960) == (std::vector<int>{0, 1}), true);
	// across a seam: both in view, top to bottom, and nothing else
	CHECK_EQ(bandsNeeded(l, -1700, ph, 480, 0, 0) == (std::vector<int>{0, 1}), true);
	// just past it: the band above is within a screen, after the one in view
	CHECK_EQ(bandsNeeded(l, -1900, ph, 480, 480, 960) == (std::vector<int>{1, 0}), true);
	CHECK_EQ(bandsNeeded(l, -1900, ph, 480, 0, 0) == (std::vector<int>{1}), true);
	// in view, then under (nearest first), then over
	CHECK_EQ(bandsNeeded(l, -3600, ph, 480, 5000, 5000) == (std::vector<int>{1, 2, 3, 4, 0}), true);
	// a page far under or over the view needs none
	CHECK_EQ(bandsNeeded(l, 5000, ph, 480, 480, 960).empty(), true);
	CHECK_EQ(bandsNeeded(l, -ph - 600, ph, 480, 480, 960).empty(), true);
	// the page after, its top a screen under the view: its first band is ready before it is reached
	CHECK_EQ(bandsNeeded(l, 900, ph, 480, 480, 960) == (std::vector<int>{0}), true);
	// keys: a whole page is its index; a band's key gives both back
	CHECK_EQ(bandKey(7, -1), 7);
	CHECK_EQ(keyPage(7), 7);
	CHECK_EQ(keyBand(7), -1);
	int k = bandKey(1234, 31);
	CHECK_EQ(k != 1234, true);
	CHECK_EQ(keyPage(k), 1234);
	CHECK_EQ(keyBand(k), 31);

	// a band decoded alone is the whole decode's rows: a 64 x 5000 PNG, each row its own colour
	{
		const int W = 64, H = 5000;
		std::vector<unsigned char> px((size_t)W * H * 4);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				unsigned char* p = &px[((size_t)y * W + x) * 4];
				p[0] = (unsigned char)(y & 255); p[1] = (unsigned char)(y >> 8); p[2] = (unsigned char)x; p[3] = 255;
			}
		std::string path = "/tmp/bp-test-band.png";
		CHECK_EQ(savePng(path, W, H, px.data()), true);
		std::vector<uint8_t> bytes;
		std::string err;
		CHECK_EQ(readPageFile(path, bytes, err), true);
		BandLayout pl{W, H};
		CHECK_EQ(pl.count(), 2);
		PageImage b1 = decodeBand(bytes.data(), bytes.size(), 0, pl.top(1), pl.rows(1));
		CHECK_EQ(b1.ok(), true);
		CHECK_EQ(b1.srcW, W);
		CHECK_EQ(b1.srcH, H);
		CHECK_EQ(b1.w, W);
		CHECK_EQ(b1.h, H - 2048);
		CHECK_EQ(b1.ok() && std::memcmp(b1.rgba.data(), &px[(size_t)2048 * W * 4], b1.rgba.size()) == 0, true);
		// rows past the end are cut; a band outside the picture is an error
		PageImage tail = decodeBand(bytes.data(), bytes.size(), 0, 4990, 2048);
		CHECK_EQ(tail.ok(), true);
		CHECK_EQ(tail.h, 10);
		CHECK_EQ(decodeBand(bytes.data(), bytes.size(), 0, 5000, 10).ok(), false);
		// scaled to the decode width
		PageImage half = decodeBand(bytes.data(), bytes.size(), 32, 0, 2048);
		CHECK_EQ(half.ok(), true);
		CHECK_EQ(half.w, 32);
		CHECK_EQ(half.h, 1024);
		// a cut file fails (the reader then tries the page whole)
		CHECK_EQ(decodeBand(bytes.data(), bytes.size() / 2, 0, 2048, 2048).ok(), false);
		remove(path.c_str());
	}
	// a JPEG's band against the whole decode (two decoders: equal within a few levels)
	{
		const std::string path = std::string(BP_TESTS_DIR) + "/pages/page.jpg";
		std::vector<uint8_t> bytes;
		std::string err;
		CHECK_EQ(readPageFile(path, bytes, err), true);
		PageImage whole = decodePage(bytes.data(), bytes.size(), 0);
		PageImage band = decodeBand(bytes.data(), bytes.size(), 0, 50, 70);
		CHECK_EQ(band.ok(), true);
		CHECK_EQ(band.w, 120);
		CHECK_EQ(band.h, 70);
		CHECK_EQ(band.srcH, 180);
		long long diff = 0;
		if (band.ok() && whole.ok())
			for (size_t i = 0; i < band.rgba.size(); i++)
				diff += std::abs((int)band.rgba[i] - (int)whole.rgba[(size_t)50 * 120 * 4 + i]);
		CHECK_EQ(band.ok() && diff / (long long)band.rgba.size() < 4, true);
		// no band decoder for the other formats, nor for a damaged file
		std::vector<uint8_t> webp;
		CHECK_EQ(readPageFile(std::string(BP_TESTS_DIR) + "/pages/page.webp", webp, err), true);
		CHECK_EQ(decodeBand(webp.data(), webp.size(), 0, 0, 100).ok(), false);
		std::vector<uint8_t> cut;
		CHECK_EQ(readPageFile(std::string(BP_TESTS_DIR) + "/pages/truncated.jpg", cut, err), true);
		CHECK_EQ(decodeBand(cut.data(), cut.size(), 0, 0, 100).ok(), false);
	}
}

static void testPageDecode()
{
	const std::string pages = std::string(BP_TESTS_DIR) + "/pages/";
	auto readBytes = [](const std::string& path) {
		std::ifstream in(path, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};

	// a page's own size, the panel's width and the 2x cap, for each still format the fixtures carry
	for (const char* name : {"page.jpg", "page.png", "page.webp"})
	{
		const std::string path = pages + name;
		PageImage at60 = decodePageFile(path, 60);
		CHECK_EQ(at60.ok(), true);
		CHECK_EQ(at60.srcW, 120);
		CHECK_EQ(at60.srcH, 180);
		CHECK_EQ(at60.w, 60);
		CHECK_EQ(at60.h, 90);
		CHECK_EQ(at60.rgba.size(), (size_t)60 * 90 * 4);

		PageImage own = decodePageFile(path, 0);
		CHECK_EQ(own.ok(), true);
		CHECK_EQ(own.w, 120);
		CHECK_EQ(own.h, 180);

		PageImage big = decodePageFile(path, 1000);
		CHECK_EQ(big.ok(), true);
		CHECK_EQ(big.w, 240);	// never over 2x the image's own width
		CHECK_EQ(big.h, 360);

		// the format comes from the bytes, not a name: a page out of an archive or a server has none
		std::vector<uint8_t> bytes = readBytes(path);
		PageImage fromBytes = decodePage(bytes.data(), bytes.size(), 60);
		CHECK_EQ(fromBytes.ok(), true);
		CHECK_EQ(fromBytes.w, 60);
		CHECK_EQ(fromBytes.h, 90);
	}

	// a cut PNG has no IEND: the decoder must refuse it rather than hand back half a page
	std::vector<uint8_t> pngBytes = readBytes(pages + "page.png");
	PageImage cutPng = decodePage(pngBytes.data(), pngBytes.size() / 2, 0);
	CHECK_EQ(cutPng.ok(), false);
	CHECK_EQ(cutPng.error.empty(), false);

	// solid 0x336699 RGB: no alpha in the file, so the decoder gives opaque pixels
	PageImage wide = decodePageFile(pages + "wide.png", 0);
	CHECK_EQ(wide.ok(), true);
	CHECK_EQ(wide.srcW, 64);
	CHECK_EQ(wide.srcH, 48);
	CHECK_EQ(wide.w, 64);
	CHECK_EQ(wide.h, 48);
	const uint8_t* wp = &wide.rgba[(10 * wide.w + 10) * 4];
	CHECK_EQ(std::abs((int)wp[0] - 0x33) <= 4, true);
	CHECK_EQ(std::abs((int)wp[1] - 0x66) <= 4, true);
	CHECK_EQ(std::abs((int)wp[2] - 0x99) <= 4, true);
	CHECK_EQ(wp[3], 255);

	// red at half alpha: a paletted PNG's transparency reaches the RGBA buffer
	PageImage alpha = decodePageFile(pages + "alpha.png", 0);
	CHECK_EQ(alpha.ok(), true);
	CHECK_EQ(alpha.w, 40);
	CHECK_EQ(alpha.h, 60);
	const uint8_t* ap = &alpha.rgba[(30 * alpha.w + 20) * 4];
	CHECK_EQ(std::abs((int)ap[0] - 255) <= 4, true);
	CHECK_EQ(std::abs((int)ap[3] - 128) <= 4, true);

	// maxH caps the height and shrinks the width with the aspect
	PageImage capped = decodePageFile(pages + "page.jpg", 120, 90);
	CHECK_EQ(capped.ok(), true);
	CHECK_EQ(capped.w, 60);
	CHECK_EQ(capped.h, 90);
	PageImage cappedOwn = decodePageFile(pages + "page.jpg", 0, 60);
	CHECK_EQ(cappedOwn.ok(), true);
	CHECK_EQ(cappedOwn.w, 40);
	CHECK_EQ(cappedOwn.h, 60);

	// an odd scaled size is rounded to at least one row and still has no stride padding
	PageImage oddSize = decodePageFile(pages + "wide.png", 33);
	CHECK_EQ(oddSize.ok(), true);
	CHECK_EQ(oddSize.w, 33);
	CHECK_EQ(oddSize.h, 25);
	CHECK_EQ(oddSize.rgba.size(), (size_t)33 * 25 * 4);

	// no input, no format, a cut file and a file that isn't there: an error, never a crash
	PageImage empty = decodePage(nullptr, 0, 0);
	CHECK_EQ(empty.ok(), false);
	CHECK_EQ(empty.error.empty(), false);
	const uint8_t junk[] = {0x00, 0x01, 0x02, 0x03};
	PageImage unknown = decodePage(junk, sizeof junk, 0);
	CHECK_EQ(unknown.ok(), false);
	CHECK_EQ(unknown.error.empty(), false);
	// an AVIF box with no picture in it exercises the container path's clean refusal
	const uint8_t fakeAvif[] = {0, 0, 0, 20, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f', 0, 0, 0, 0};
	PageImage avif = decodePage(fakeAvif, sizeof fakeAvif, 0);
	CHECK_EQ(avif.ok(), false);
	CHECK_EQ(avif.error.empty(), false);
	PageImage broken = decodePageFile(pages + "broken.jpg", 0);
	CHECK_EQ(broken.ok(), false);
	CHECK_EQ(broken.error.empty(), false);
	PageImage truncated = decodePageFile(pages + "truncated.jpg", 0);
	CHECK_EQ(truncated.ok(), false);
	CHECK_EQ(truncated.error.empty(), false);
	PageImage missing = decodePageFile(pages + "missing.jpg", 0);
	CHECK_EQ(missing.ok(), false);
	CHECK_EQ(missing.error.empty(), false);
}

static void testCbz()
{
	const std::string P = std::string(BP_TESTS_DIR) + "/pages/";
	auto file = [](const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};
	const std::vector<uint8_t> jpg = file(P + "page.jpg"), png = file(P + "page.png");
	std::vector<uint8_t> out;

	// the same three files, stored and deflated: pages() carries the two images in order, not the xml
	for (const char* a : {"stored.cbz", "deflated.cbz"})
	{
		Cbz z;
		CHECK_EQ(z.open(P + a), true);
		CHECK_EQ(z.isOpen(), true);
		CHECK_EQ(z.entries().size(), (size_t)3);
		CHECK_EQ(z.pages(), (std::vector<std::string>{"001.jpg", "002.png"}));
		CHECK_EQ(z.read("001.jpg", out) && out == jpg, true);
		CHECK_EQ(z.read("002.png", out) && out == png, true);
		CHECK_EQ(z.read("ComicInfo.xml", out)
			&& std::string(out.begin(), out.end()).find("<Series>Test Series</Series>") != std::string::npos, true);
		CHECK_EQ(z.read("003.jpg", out), false);	// a name that isn't in the archive
		CHECK_EQ(z.error().empty(), false);
		z.close();
		CHECK_EQ(z.isOpen(), false);
		CHECK_EQ(z.read("001.jpg", out), false);	// closed: nothing to read
	}

	// what the directory promises about each entry
	Cbz s;
	CHECK_EQ(s.open(P + "stored.cbz"), true);
	CHECK_EQ(s.entries()[0].name, std::string("001.jpg"));
	CHECK_EQ(s.entries()[0].method, (uint32_t)0);
	CHECK_EQ(s.entries()[0].packed, s.entries()[0].size);	// stored: nothing was compressed away
	CHECK_EQ(s.entries()[0].size, (uint64_t)jpg.size());
	Cbz d;
	CHECK_EQ(d.open(P + "deflated.cbz"), true);
	CHECK_EQ(d.entries()[0].method, (uint32_t)8);
	CHECK_EQ(d.entries()[0].size, (uint64_t)jpg.size());
	CHECK_EQ(d.entries()[0].packed < d.entries()[0].size, true);	// deflated: it really is smaller

	// the folder archive: its directory entry isn't listed, and the pages under it read in order
	Cbz n;
	CHECK_EQ(n.open(P + "nested.cbz"), true);
	CHECK_EQ(n.entries().size(), (size_t)3);
	CHECK_EQ(n.pages(), (std::vector<std::string>{"sub/002.png", "sub/010.jpg"}));
	CHECK_EQ(n.read("sub/010.jpg", out) && out == jpg, true);
	CHECK_EQ(n.read("sub/002.png", out) && out == png, true);
	CHECK_EQ(n.read("sub/", out), false);

	// not a zip at all, and a file that isn't there: open() says no with a reason
	Cbz z;
	CHECK_EQ(z.open(P + "page.jpg"), false);
	CHECK_EQ(z.error().empty(), false);
	CHECK_EQ(z.open(P + "gone.cbz"), false);
	CHECK_EQ(z.error().empty(), false);
	CHECK_EQ(z.isOpen(), false);

	// damaged copies, written into one temporary file the test removes again
	char tmpl[] = "/tmp/bp-cbz-test-XXXXXX";
	int tf = mkstemp(tmpl);
	CHECK_EQ(tf >= 0, true);
	::close(tf);
	const std::string tmp = tmpl;
	auto write = [&](const std::vector<uint8_t>& v)
	{
		std::ofstream w(tmp, std::ios::binary | std::ios::trunc);
		w.write((const char*)v.data(), (std::streamsize)v.size());
		CHECK_EQ(w.good(), true);
	};
	write({});	// empty: too short for even the end record
	CHECK_EQ(z.open(tmp), false);
	CHECK_EQ(z.error().empty(), false);
	const std::vector<uint8_t> deflated = file(P + "deflated.cbz");
	write(std::vector<uint8_t>(deflated.begin(), deflated.begin() + deflated.size() / 2));
	CHECK_EQ(z.open(tmp), false);	// cut in half: its central directory is gone

	// flip bytes in the middle of 001.jpg's compressed data: the archive still opens (the directory is intact),
	// reading that page fails on the CRC or the stream, and the rest of the archive still reads
	std::vector<uint8_t> broken = deflated;
	const CbzEntry& e0 = d.entries()[0];
	size_t lo = (size_t)e0.localOffset;	// the local header's own name and extra lengths, not the record's
	auto le16 = [&](size_t at) { return (size_t)broken[at] | ((size_t)broken[at + 1] << 8); };
	size_t data = lo + 30 + le16(lo + 26) + le16(lo + 28);
	for (size_t k = 0; k < 8; k++) broken[data + (size_t)(e0.packed / 2) + k] ^= 0xff;
	write(broken);
	d.close();
	CHECK_EQ(d.open(tmp), true);
	CHECK_EQ(d.read("001.jpg", out), false);
	CHECK_EQ(d.error().empty(), false);
	CHECK_EQ(d.read("002.png", out) && out == png, true);
	std::remove(tmp.c_str());
}

static bool near(double a, double b, double eps = 1e-6)
{
	return std::fabs(a - b) <= eps;
}

static void testStrip()
{
	// zoomed, a quick swipe across glides on as one down does (issue #8): the pan keeps going after the finger
	// lets go, within the column
	{
		Strip z;
		z.setView(400, 600);
		z.setPages(3);
		z.setZoom(2, 200, 300);
		double p0 = z.pan();
		z.touchDown(0);
		z.drag(0, 10, -40);	// a finger left: the column pans right (toward its right edge)
		z.drag(0, 20, -40);	// 4000 px/s across, nothing down
		double p1 = z.pan();
		CHECK_EQ(near(p1, p0 + 80), true);
		z.release(25);
		CHECK_EQ(z.moving(), true);
		z.step(3000);
		CHECK_EQ(z.moving(), false);
		CHECK_EQ(z.pan() > p1 + 1, true);
		CHECK_EQ(z.pan() <= 400 + 0.5, true);	// a column 800 wide in a view of 400 pans 400 at most
	}
	// layout: pages at the view's width, no gap, placeholders 1.5 x the width
	Strip s;
	s.setView(400, 600);
	s.setPages(3);
	CHECK_EQ(s.pageCount(), 3);
	CHECK_EQ(near(s.pageHeight(0), 600), true);
	CHECK_EQ(near(s.pageTop(0), 0), true);
	CHECK_EQ(near(s.pageTop(1), 600), true);
	CHECK_EQ(near(s.pageTop(2), 1200), true);
	CHECK_EQ(near(s.totalHeight(), 1800), true);
	CHECK_EQ(near(s.offset(), 0), true);
	CHECK_EQ(near(s.screenful(), 540), true);

	// a real size below the view changes nothing on screen
	s.setPageSize(2, 400, 800);	// page 2: 800 high, +200
	CHECK_EQ(near(s.pageHeight(2), 800), true);
	CHECK_EQ(near(s.offset(), 0), true);
	CHECK_EQ(near(s.totalHeight(), 2000), true);
	// a page at the view's top isn't above it: the top stays put as well
	s.setPageSize(0, 400, 400);	// page 0: 400 high, -200
	CHECK_EQ(near(s.pageHeight(0), 400), true);
	CHECK_EQ(near(s.offset(), 0), true);
	CHECK_EQ(near(s.pageTop(1), 400), true);
	CHECK_EQ(near(s.totalHeight(), 1800), true);

	// a page above the view moves the offset by its height change, so what is on screen keeps its place
	Strip above;
	above.setView(400, 600);
	above.setPages(3);	// 600 each: tops 0, 600, 1200
	above.scrollTo(900);	// 300 px into page 1
	CHECK_EQ(near(above.offset(), 900), true);
	above.setPageSize(0, 400, 400);	// page 0 shrinks by 200
	CHECK_EQ(near(above.offset(), 700), true);
	CHECK_EQ(near(above.pageTop(1), 400), true);
	CHECK_EQ(near(above.offset() - above.pageTop(1), 300), true);	// still 300 px into page 1
	above.setPageSize(0, 400, 800);	// grows back by 400
	CHECK_EQ(near(above.offset(), 1100), true);
	CHECK_EQ(near(above.offset() - above.pageTop(1), 300), true);

	// bad input is ignored: an index out of range, a zero width or height
	above.setPageSize(-1, 400, 400);
	above.setPageSize(3, 400, 400);
	above.setPageSize(1, 0, 400);
	above.setPageSize(1, 400, 0);
	CHECK_EQ(near(above.pageHeight(1), 600), true);
	CHECK_EQ(near(above.offset(), 1100), true);

	// clamping at both ends, and a column no taller than the view
	s.scrollTo(100000);
	CHECK_EQ(near(s.offset(), s.totalHeight() - 600), true);
	s.scrollBy(500);
	CHECK_EQ(near(s.offset(), 1200), true);
	s.scrollTo(-50);
	CHECK_EQ(near(s.offset(), 0), true);
	s.scrollBy(-500);
	CHECK_EQ(near(s.offset(), 0), true);

	Strip shortOne;
	shortOne.setView(400, 600);
	shortOne.setPages(1);	// 600: exactly the view, so it can't scroll
	shortOne.scrollTo(300);
	CHECK_EQ(near(shortOne.offset(), 0), true);
	CHECK_EQ(near(shortOne.totalHeight(), 600), true);
	shortOne.setPages(0);
	CHECK_EQ(shortOne.pageCount(), 0);
	CHECK_EQ(shortOne.offset(), 0.0);

	// visible and currentPage at the boundaries: a page occupying exactly one view
	Strip v;
	v.setView(400, 400);
	v.setPages(4, 1.0);	// 400 each: tops 0, 400, 800, 1200; total 1600
	CHECK_EQ(v.visible().first, 0);
	CHECK_EQ(v.visible().second, 0);
	CHECK_EQ(v.currentPage(), 0);
	v.scrollTo(400);
	CHECK_EQ(v.visible().first, 1);
	CHECK_EQ(v.visible().second, 1);
	CHECK_EQ(v.currentPage(), 1);
	// the middle exactly on a page's top belongs to the page below it
	v.scrollTo(200);	// view [200, 600): pages 0 and 1, middle 400
	CHECK_EQ(v.visible().first, 0);
	CHECK_EQ(v.visible().second, 1);
	CHECK_EQ(v.currentPage(), 1);
	v.scrollTo(1200);	// the end: page 2's bottom is the view's top
	CHECK_EQ(v.visible().first, 3);
	CHECK_EQ(v.visible().second, 3);
	CHECK_EQ(v.currentPage(), 3);
	v.setPages(1, 1.0);
	CHECK_EQ(v.visible().first, 0);
	CHECK_EQ(v.visible().second, 0);
	CHECK_EQ(v.currentPage(), 0);
	v.setPages(0);
	CHECK_EQ(v.visible().first, -1);
	CHECK_EQ(v.visible().second, -1);
	CHECK_EQ(v.currentPage(), -1);
	CHECK_EQ(v.wanted(2, 2).size(), (size_t)0);

	// wanted: visible first (top to bottom), then ahead in the direction of motion, then behind
	Strip w;
	w.setView(400, 400);
	w.setPages(10, 1.0);	// 400 each: total 4000, max offset 3600
	std::vector<int> ids = w.wanted(3, 2);	// no motion yet: ahead is down, and the list's start runs out
	CHECK_EQ(ids.size(), (size_t)4);
	for (int i = 0; i < 4; i++) CHECK_EQ(ids[i], i);
	w.scrollTo(2000);	// page 5, alone in the view, moving down
	CHECK_EQ(w.currentPage(), 5);
	ids = w.wanted(2, 2);
	CHECK_EQ(ids.size(), (size_t)5);
	CHECK_EQ(ids[0], 5);
	CHECK_EQ(ids[1], 6);
	CHECK_EQ(ids[2], 7);
	CHECK_EQ(ids[3], 4);
	CHECK_EQ(ids[4], 3);

	// moving up: ahead is the pages before the visible ones, behind the pages after
	w.scrollBy(-10);	// 1990: view [1990, 2390) holds pages 4 and 5
	CHECK_EQ(w.visible().first, 4);
	CHECK_EQ(w.visible().second, 5);
	ids = w.wanted(2, 2);
	CHECK_EQ(ids.size(), (size_t)6);
	CHECK_EQ(ids[0], 4);
	CHECK_EQ(ids[1], 5);
	CHECK_EQ(ids[2], 3);
	CHECK_EQ(ids[3], 2);
	CHECK_EQ(ids[4], 6);
	CHECK_EQ(ids[5], 7);

	// at the end and at the start there is nothing ahead on that side
	w.scrollTo(3600);
	ids = w.wanted(3, 2);
	CHECK_EQ(ids.size(), (size_t)3);
	CHECK_EQ(ids[0], 9);
	CHECK_EQ(ids[1], 8);
	CHECK_EQ(ids[2], 7);
	w.scrollTo(0);	// moving up, so ahead is up the column
	ids = w.wanted(3, 2);
	CHECK_EQ(ids.size(), (size_t)3);
	CHECK_EQ(ids[0], 0);
	CHECK_EQ(ids[1], 1);
	CHECK_EQ(ids[2], 2);
	// bounds far past both ends still list each page once
	w.scrollTo(2000);
	ids = w.wanted(99, 99);
	CHECK_EQ(ids.size(), (size_t)10);
	std::set<int> once(ids.begin(), ids.end());
	CHECK_EQ(once.size(), (size_t)10);

	// drag follows the finger 1:1 (dy > 0 is a finger moving down, so the offset falls), clamped
	Strip d;
	d.setView(400, 400);
	d.setPages(10, 1.0);
	d.scrollTo(1000);
	d.touchDown(0);
	d.drag(25, 10);
	CHECK_EQ(near(d.offset(), 975), true);
	d.drag(-25, 20);
	CHECK_EQ(near(d.offset(), 1000), true);
	d.release(30);	// the last drag went the other way, so that is the way it flings
	CHECK_EQ(d.moving(), true);
	CHECK_EQ(d.step(40), true);
	CHECK_EQ(d.offset() > 1000, true);

	// a drag past an end is clamped and leaves nothing to fling
	d.scrollTo(0);
	d.touchDown(0);
	d.drag(300, 10);
	d.drag(300, 20);
	CHECK_EQ(near(d.offset(), 0), true);
	d.release(30);
	CHECK_EQ(d.moving(), false);
	d.scrollTo(3600);	// the far end
	d.touchDown(0);
	d.drag(-300, 10);
	d.drag(-300, 20);
	CHECK_EQ(near(d.offset(), 3600), true);
	d.release(30);
	CHECK_EQ(d.moving(), false);

	// the fling's velocity comes from the last 100 ms: 100 px in 40 ms is 2500 px/s
	Strip f;
	f.setView(400, 1000);
	f.setPages(20, 2.5);	// 1000 high in a 400 wide view: total 20000, max offset 19000
	f.scrollTo(5000);
	f.touchDown(0);
	f.drag(-50, 20);
	f.drag(-50, 40);
	CHECK_EQ(near(f.offset(), 5100), true);
	f.release(50);
	CHECK_EQ(f.moving(), true);
	CHECK_EQ(f.step(375), true);	// 325 ms on: v0 * tau * (1 - e^-1)
	CHECK_EQ(near(f.offset(), 5100 + 2500 * 0.325 * (1 - std::exp(-1.0)), 1e-3), true);
	f.step(10000);
	CHECK_EQ(f.moving(), false);

	// a release after the finger rested for more than 100 ms has no velocity to fling with
	Strip hold;
	hold.setView(400, 400);
	hold.setPages(10, 1.0);
	hold.scrollTo(1000);
	hold.touchDown(0);
	hold.drag(-30, 20);
	hold.drag(-30, 40);
	hold.release(300);
	CHECK_EQ(hold.moving(), false);
	CHECK_EQ(near(hold.offset(), 1060), true);
	hold.step(400);
	CHECK_EQ(near(hold.offset(), 1060), true);

	// the velocity is capped at 6000 px/s (this flick would be 15000)
	Strip capStrip;
	capStrip.setView(400, 1000);
	capStrip.setPages(40, 2.5);
	capStrip.scrollTo(10000);
	capStrip.touchDown(0);
	capStrip.drag(-150, 10);
	capStrip.drag(-150, 20);
	capStrip.release(30);
	capStrip.step(355);	// 325 ms on
	CHECK_EQ(near(capStrip.offset(), 10300 + 6000 * 0.325 * (1 - std::exp(-1.0)), 1e-3), true);

	// the same fling stepped every 5 ms and every 50 ms ends on the same pixel
	Strip fast, slow;
	for (Strip* p : {&fast, &slow})
	{
		p->setView(400, 1000);
		p->setPages(20, 2.5);
		p->scrollTo(5000);
		p->touchDown(0);
		p->drag(-50, 20);
		p->drag(-50, 40);
		p->release(50);
	}
	int64_t t = 50;
	while (fast.step(t += 5)) {}
	int64_t u = 50;
	while (slow.step(u += 50)) {}
	CHECK_EQ(fast.moving(), false);
	CHECK_EQ(slow.moving(), false);
	CHECK_EQ(near(fast.offset(), slow.offset(), 1.0), true);
	// its whole travel: v0 * tau * (1 - 20 / v0), the speed it stops at
	CHECK_EQ(near(fast.offset(), 5100 + 2500 * 0.325 * (1 - 20.0 / 2500), 1e-3), true);

	// a fling into an end stops exactly on it, with its velocity gone
	Strip edge;
	edge.setView(400, 1000);
	edge.setPages(20, 2.5);
	edge.scrollTo(18500);
	edge.touchDown(0);
	edge.drag(-150, 10);	// 18650
	edge.drag(-150, 20);	// 18800, 15000 px/s capped to 6000
	edge.release(30);
	CHECK_EQ(edge.moving(), true);
	CHECK_EQ(edge.step(130), false);	// 100 ms on: ~516 px would pass the end
	CHECK_EQ(edge.moving(), false);
	CHECK_EQ(near(edge.offset(), 19000), true);
	CHECK_EQ(edge.step(500), false);	// nothing left to advance
	CHECK_EQ(near(edge.offset(), 19000), true);

	// animateTo: an ease-out cubic over the duration, exactly on target at its end
	Strip a;
	a.setView(400, 400);
	a.setPages(10, 1.0);
	a.scrollTo(1000);
	a.animateTo(1400, 0);
	CHECK_EQ(a.moving(), true);
	CHECK_EQ(a.step(0), true);
	CHECK_EQ(near(a.offset(), 1000), true);
	CHECK_EQ(a.step(90), true);	// half way: 1 - (1 - 0.5)^3 = 0.875
	CHECK_EQ(near(a.offset(), 1350), true);
	CHECK_EQ(a.step(180), false);
	CHECK_EQ(near(a.offset(), 1400), true);
	CHECK_EQ(a.moving(), false);
	CHECK_EQ(a.step(200), false);
	CHECK_EQ(near(a.offset(), 1400), true);

	// touchDown stops an animation where it is
	a.scrollTo(1000);
	a.animateTo(1800, 500);
	CHECK_EQ(a.step(590), true);
	double stoppedAt = a.offset();
	a.touchDown(600);
	CHECK_EQ(a.moving(), false);
	CHECK_EQ(a.step(700), false);
	CHECK_EQ(near(a.offset(), stoppedAt), true);

	// a zero duration and an out-of-range target: straight to the clamped end, not moving
	a.scrollTo(1000);
	a.animateTo(99999, 0, 0);
	CHECK_EQ(near(a.offset(), 3600), true);
	CHECK_EQ(a.moving(), false);

	// zoom: the column widens past the view, the content under the focus keeps its place, vertical drags still
	// scroll it and horizontal ones pan it across
	Strip z;
	z.setView(400, 600);
	z.setPages(3);	// 600 each: tops 0, 600, 1200
	CHECK_EQ(near(z.pageWidth(), 400), true);
	CHECK_EQ(z.pan(), 0.0);
	z.scrollTo(600);
	z.setZoom(2, 200, 300);
	CHECK_EQ(near(z.zoom(), 2), true);
	CHECK_EQ(near(z.pageWidth(), 800), true);
	CHECK_EQ(near(z.pageHeight(0), 1200), true);
	CHECK_EQ(near(z.offset(), 1500), true);	// (600 + 300) * 2 - 300
	CHECK_EQ(near(z.pan(), 200), true);		// (0 + 200) * 2 - 200
	z.drag(-100, 10);	// a finger up still walks down the column
	CHECK_EQ(near(z.offset(), 1600), true);
	z.drag(0, 20, 100);	// a finger right reveals what is left of it
	CHECK_EQ(near(z.pan(), 100), true);
	z.drag(0, 30, 1000);	// clamped at the view's left edge…
	CHECK_EQ(near(z.pan(), 0), true);
	z.drag(0, 40, -1000);	// …and at the page's far edge
	CHECK_EQ(near(z.pan(), 400), true);
	z.drag(0, 50, 400);	// back to its left edge
	CHECK_EQ(z.pan(), 0.0);
	// back to the fit: the column's middle is where it was, and there is nothing left to pan
	z.scrollTo(1500);
	z.setZoom(1, 200, 300);
	CHECK_EQ(near(z.zoom(), 1), true);
	CHECK_EQ(near(z.pageWidth(), 400), true);
	CHECK_EQ(near(z.offset(), 600), true);	// (1500 + 300) / 2 - 300
	CHECK_EQ(z.pan(), 0.0);
	z.setZoom(20, 200, 300);	// 800 % is the furthest the reader goes
	CHECK_EQ(near(z.zoom(), 8), true);
}

static void testLabels()
{
	Track a;
	a.type = TrackType::Audio;
	a.lang = "eng";
	a.codec = "ac3";
	a.channels = 6;
	CHECK_EQ(trackLabel(a, 1), std::string("English · AC3 5.1"));

	a.title = "Director's commentary";
	a.codec = "aac";
	a.channels = 2;
	CHECK_EQ(trackLabel(a, 2), std::string("English · AAC Stereo · Director's commentary"));

	Track s;
	s.type = TrackType::Sub;
	s.codec = "subrip";
	s.external = true;
	CHECK_EQ(trackLabel(s, 3), std::string("Track 3 · SRT · File"));

	s.lang = "jpn";
	s.title = "Japanese";	// says nothing the language doesn't
	s.codec = "hdmv_pgs_subtitle";
	s.external = false;
	s.forced = true;
	CHECK_EQ(trackLabel(s, 1), std::string("Japanese · PGS · Forced"));

	CHECK_EQ(languageName("ZH"), std::string("Chinese"));
	CHECK_EQ(languageName("und"), std::string(""));
	CHECK_EQ(languageName("tlh"), std::string("tlh"));
	CHECK_EQ(codecName("pcm_s16le"), std::string("PCM"));
	CHECK_EQ(codecName("h264"), std::string("H264"));
}

// mpv_node trees as mpv hands them out
struct NodeMap
{
	std::vector<std::string> keyStore;
	std::vector<char*> keys;
	std::vector<mpv_node> values;
	mpv_node_list list{};
	mpv_node node{};

	void str(const char* k, const char* v) { add(k).u.string = const_cast<char*>(v), values.back().format = MPV_FORMAT_STRING; }
	void i64(const char* k, int64_t v) { add(k).u.int64 = v, values.back().format = MPV_FORMAT_INT64; }
	void dbl(const char* k, double v) { add(k).u.double_ = v, values.back().format = MPV_FORMAT_DOUBLE; }
	void flag(const char* k, bool v) { add(k).u.flag = v, values.back().format = MPV_FORMAT_FLAG; }
	mpv_node& add(const char* k)
	{
		keyStore.push_back(k);
		values.push_back({});
		return values.back();
	}
	mpv_node* done()
	{
		keys.clear();
		for (auto& k : keyStore) keys.push_back(const_cast<char*>(k.c_str()));
		list.num = (int)values.size();
		list.values = values.data();
		list.keys = keys.data();
		node.format = MPV_FORMAT_NODE_MAP;
		node.u.list = &list;
		return &node;
	}
};

static void testTrackList()
{
	NodeMap v, a1, a2, s1;
	v.i64("id", 1); v.str("type", "video"); v.str("codec", "h264"); v.flag("selected", true);
	a1.i64("id", 1); a1.str("type", "audio"); a1.str("lang", "jpn"); a1.str("codec", "opus");
	a1.i64("demux-channel-count", 2); a1.flag("selected", true); a1.flag("default", true);
	a2.i64("id", 2); a2.str("type", "audio"); a2.str("lang", "eng"); a2.str("codec", "eac3"); a2.i64("demux-channel-count", 6);
	s1.i64("id", 1); s1.str("type", "sub"); s1.str("lang", "eng"); s1.str("codec", "ass"); s1.str("title", "Signs & Songs");
	s1.flag("forced", true);

	std::vector<mpv_node> items = {*v.done(), *a1.done(), *a2.done(), *s1.done()};
	mpv_node_list l{};
	l.num = (int)items.size();
	l.values = items.data();
	mpv_node arr{};
	arr.format = MPV_FORMAT_NODE_ARRAY;
	arr.u.list = &l;

	auto t = parseTrackList(&arr);
	CHECK_EQ(t.size(), (size_t)4);
	CHECK_EQ(t[0].type, TrackType::Video);
	CHECK_EQ(t[1].type, TrackType::Audio);
	CHECK_EQ(t[1].selected, true);
	CHECK_EQ(t[1].isDefault, true);
	CHECK_EQ(t[2].id, (int64_t)2);
	CHECK_EQ(t[2].channels, 6);
	CHECK_EQ(trackLabel(t[2], 2), std::string("English · E-AC3 5.1"));
	CHECK_EQ(t[3].type, TrackType::Sub);
	CHECK_EQ(trackLabel(t[3], 1), std::string("English · ASS · Signs & Songs · Forced"));

	CHECK_EQ(parseTrackList(nullptr).size(), (size_t)0);
}

static void testChapters()
{
	NodeMap c1, c2, c3;
	c1.dbl("time", 0); c1.str("title", "Opening");
	c2.dbl("time", 15.5); c2.str("title", "00:00:15.500");
	c3.i64("time", 30);	// whole seconds, no title
	std::vector<mpv_node> items = {*c1.done(), *c2.done(), *c3.done()};
	mpv_node_list l{};
	l.num = (int)items.size();
	l.values = items.data();
	mpv_node arr{};
	arr.format = MPV_FORMAT_NODE_ARRAY;
	arr.u.list = &l;

	auto ch = parseChapterList(&arr);
	CHECK_EQ(ch.size(), (size_t)3);
	CHECK_EQ(ch[0].title, std::string("Opening"));
	CHECK_EQ(ch[1].time, 15.5);
	CHECK_EQ(ch[2].time, 30.0);
	CHECK_EQ(ch[2].title, std::string(""));
	CHECK_EQ(parseChapterList(nullptr).size(), (size_t)0);

	CHECK_EQ(chapterUntitled(""), true);
	CHECK_EQ(chapterUntitled("  \t"), true);
	CHECK_EQ(chapterUntitled("00:12:30.000"), true);
	CHECK_EQ(chapterUntitled("12:30"), true);
	CHECK_EQ(chapterUntitled("(unnamed)"), true);
	CHECK_EQ(chapterUntitled("Chapter 03"), false);
	CHECK_EQ(chapterUntitled("1984"), false);
	CHECK_EQ(chapterUntitled("Intro 0:30"), false);
	CHECK_EQ(chapterLabel(ch[0], 1), std::string("Opening"));
	CHECK_EQ(chapterLabel(ch[1], 2), std::string("Chapter 2"));
	CHECK_EQ(chapterLabel(ch[2], 3), std::string("Chapter 3"));

	// the target table: next
	CHECK_EQ(chapterTarget({}, -1, 3, 1), CHAPTER_NONE);
	CHECK_EQ(chapterTarget({}, -1, 3, -1), CHAPTER_NONE);
	CHECK_EQ(chapterTarget(ch, -1, 0, 1), 0);
	CHECK_EQ(chapterTarget(ch, 0, 3, 1), 1);
	CHECK_EQ(chapterTarget(ch, 1, 20, 1), 2);
	CHECK_EQ(chapterTarget(ch, 2, 40, 1), CHAPTER_NONE);
	// previous: past 5 s into a chapter its start, else the one before
	CHECK_EQ(chapterTarget(ch, 1, 21, -1), 1);
	CHECK_EQ(chapterTarget(ch, 1, 20.6, -1), 1);
	CHECK_EQ(chapterTarget(ch, 1, 20.5, -1), 0);	// exactly 5 s in: still the one before
	CHECK_EQ(chapterTarget(ch, 1, 15.5, -1), 0);
	CHECK_EQ(chapterTarget(ch, 2, 36, -1), 2);
	// the first chapter: its start (0) is the file's, so nothing before it; a later start goes to 0
	CHECK_EQ(chapterTarget(ch, 0, 2, -1), CHAPTER_NONE);
	CHECK_EQ(chapterTarget(ch, 0, 9, -1), 0);
	std::vector<Chapter> late = {{10, "A"}, {20, "B"}};
	CHECK_EQ(chapterTarget(late, -1, 4, -1), CHAPTER_FILE_START);
	CHECK_EQ(chapterTarget(late, 0, 12, -1), CHAPTER_FILE_START);
	CHECK_EQ(chapterTarget(late, 0, 16, -1), 0);
	CHECK_EQ(chapterTarget(late, -1, 4, 1), 0);
	std::vector<Chapter> nearZero = {{0.3, "A"}, {20, "B"}};
	CHECK_EQ(chapterTarget(nearZero, 0, 1, -1), CHAPTER_NONE);
}

static void testSpeed()
{
	CHECK_EQ(stepSpeed(1.0, 1), 1.5);
	CHECK_EQ(stepSpeed(2.5, 1), 2.5);		// stops at the top
	CHECK_EQ(stepSpeed(2.5, 1, true), 0.5);	// a tap goes round
	CHECK_EQ(stepSpeed(0.5, -1), 0.5);
	CHECK_EQ(stepSpeed(1.25, -1), 1.0);	// from a speed set elsewhere: the next one down
	CHECK_EQ(speedLabel(1.5), std::string("1.5x"));
	CHECK_EQ(speedLabel(1.0), std::string("1x"));
	CHECK_EQ(formatBytes(512), std::string("512 B"));
	CHECK_EQ(formatBytes(1536), std::string("1.5 KB"));
	CHECK_EQ(formatBytes(734003200), std::string("700 MB"));
	CHECK_EQ(formatBitrate(128000), std::string("128 kb/s"));
	CHECK_EQ(formatBitrate(4500000), std::string("4.5 Mb/s"));
	CHECK_EQ(timeLeft(400, 1000, 1), 600.0);
	CHECK_EQ(timeLeft(400, 1000, 2), 300.0);	// at 2x it's over in half the time
	CHECK_EQ(timeLeft(1200, 1000, 1), 0.0);
	CHECK_EQ(timeLeft(-1, 1000, 1), -1.0);	// not known yet
	CHECK_EQ(timeLeft(10, -1, 1), -1.0);
}

static void testSubStyle()
{
	SubStyle def;
	CHECK_EQ(styleValueLabel(def, FONT), std::string("Inter"));
	CHECK_EQ(styleValueLabel(def, SIZE), std::string("Medium"));
	CHECK_EQ(styleValueLabel(def, BORDER), std::string("Normal"));

	SubStyle s;
	stepStyle(s, COLOR, 1);
	stepStyle(s, FONT, -1);	// wraps to the last
	CHECK_EQ(styleValueLabel(s, COLOR), std::string("Yellow"));
	CHECK_EQ(styleValueLabel(s, FONT), std::string("Fusion Pixel"));

	std::map<std::string, std::string> props;
	for (auto& p : subStyleProperties(s)) props[p.first] = p.second;
	CHECK_EQ(props["sub-color"], std::string("#FFE45C"));
	CHECK_EQ(props["sub-font"], std::string("Fusion Pixel 12px Prop zh-Hans"));
	CHECK_EQ(props["sub-border-size"], std::string("3"));
	CHECK_EQ(props.size(), (size_t)6);

	stepStyle(s, FONT, -2);	// Inter Bold: the same family, bold
	for (auto& p : subStyleProperties(s)) props[p.first] = p.second;
	CHECK_EQ(props["sub-font"], std::string("Inter"));
	CHECK_EQ(props["sub-bold"], std::string("yes"));
	CHECK_EQ(subStyleLook(s).fontFile, std::string("Inter-Bold.ttf"));
	CHECK_EQ(subStyleLook(s).color, 0xFFE45Cu);

	CHECK_EQ(parseSubStyle(serializeSubStyle(s)) == s, true);
	// unknown keys and values are left out; what's missing keeps its default
	SubStyle odd = parseSubStyle("size = huge\ncolor=cyan\nnonsense\n# comment = x\n");
	CHECK_EQ(styleValueLabel(odd, SIZE), std::string("Medium"));
	CHECK_EQ(styleValueLabel(odd, COLOR), std::string("Cyan"));
}

static void testSettings()
{
	Settings d = parseSettings("");
	CHECK_EQ(d.rememberPosition && d.rememberAudio && d.rememberSubs, true);	// all on by default
	Settings s;
	s.rememberAudio = false;
	stepStyle(s.style, SIZE, 1);
	Settings back = parseSettings(serializeSettings(s));
	CHECK_EQ(back.rememberPosition, true);
	CHECK_EQ(back.rememberAudio, false);
	CHECK_EQ(back.rememberSubs, true);
	CHECK_EQ(back.style == s.style, true);
	CHECK_EQ(parseSettings("remember-subtitles = maybe\n").rememberSubs, true);
	CHECK_EQ(d.rememberFolder, false);	// the browser's folder: off by default
	CHECK_EQ(d.keepHistory, true);		// the play history: on by default
	CHECK_EQ(d.buttonHotkeys, true);	// the buttons over a playing video: on by default
	CHECK_EQ(parseSettings("button-hotkeys = no\n").buttonHotkeys, false);
	CHECK_EQ(d.showHiddenFolders, false);
	CHECK_EQ(parseSettings("show-hidden-folders = yes\n").showHiddenFolders, true);
	CHECK_EQ(d.mangaOn, false);	// the manga reader: off by default
	Settings mg;
	mg.mangaOn = true;
	CHECK_EQ(parseSettings(serializeSettings(mg)).mangaOn, true);
	CHECK_EQ(d.mangaSecureDns, false);	// Secure DNS: the system's lookup until it's turned on
	Settings dns;
	dns.mangaSecureDns = true;
	CHECK_EQ(parseSettings(serializeSettings(dns)).mangaSecureDns, true);
	CHECK_EQ(parseSettings("manga-secure-dns = maybe\n").mangaSecureDns, false);
	CHECK_EQ(d.musicShow, (int)SHOW_ART);
	CHECK_EQ(d.visualizer, (int)VIS_PEAKS);
	CHECK_EQ(d.loop, (int)LOOP_OFF);
	Settings m;
	m.musicShow = SHOW_BOTH;
	m.visualizer = VIS_CIRCLE;
	m.loop = LOOP_ONE;
	Settings mb = parseSettings(serializeSettings(m));
	CHECK_EQ(mb.musicShow == SHOW_BOTH && mb.visualizer == VIS_CIRCLE && mb.loop == LOOP_ONE, true);
	CHECK_EQ(visualizerGraph(VIS_WAVES, false).rfind("[aid1] asplit [ao] [v]; [v] aformat=sample_rates=44100|48000,showwaves", 0), (size_t)0);
	CHECK_EQ(visualizerGraph(VIS_SPECTRUM, true).find("[vid1]") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_SPECTRUM, true).find("showspectrum=s=640x200") != std::string::npos, true);
	// art + visualizer: the art fitted once, only the 640x200 strip drawn each frame over the art's slice (overlay's
	// main, held), showfreqs timed by itself, nothing keyed nor converted per frame
	CHECK_EQ(visualizerGraph(VIS_BARS, true),
		std::string("[aid1] asplit [ao] [a]; [vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=rgba,settb=AVTB,split [art1] [art2]; [art1] crop=640:280:0:0 [top]; [art2] crop=640:200:0:280 [under]; [a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=640x200:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=0xC08A22|0xC08A22 [s]; [under] [s] overlay=format=rgb:alpha=premultiplied [strip]; [top] [strip] vstack [vo]"));
	// Spectrum over the art: its loudness as alpha, laid on the art in YUV (no frame converted, no colorkey)
	CHECK_EQ(visualizerGraph(VIS_SPECTRUM, true).find("showspectrum=s=640x200:slide=scroll:mode=combined:color=fire:scale=log:fps=30:opacity=4,format=yuva444p [s]") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_SPECTRUM, true).find("overlay=format=yuv444 [strip]") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_WAVES, true).find("showwaves=s=640x200:mode=cline:split_channels=0:rate=30:colors=0xC08A22|0xC08A22 [s]") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_BARS, true).find("loop="), std::string::npos);
	// the visualizer alone: in the filter's own format (a per-frame conversion to yuv420p cost more than the drawing)
	for (int v : {VIS_BARS, VIS_WAVES, VIS_SPECTRUM})
		for (bool art : {false, true})
		{
			std::string g = visualizerGraph(v, art);
			CHECK_EQ(g.find("yuv420p") == std::string::npos && g.find("colorkey") == std::string::npos, true);
			// the sound drawn at 44.1/48 kHz, whatever the file's (a 192 kHz file cost up to twice as much)
			CHECK_EQ(g.find("aformat=sample_rates=44100|48000") != std::string::npos, true);
		}
	CHECK_EQ(visualizerGraph(VIS_BARS, false).find("fps="), std::string::npos);
	// alone, opaque (mpv would show a clear background as a checkerboard)
	CHECK_EQ(visualizerGraph(VIS_BARS, false).find(":colors=0xC08A22|0xC08A22,format=rgb0 [vo]") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_WAVES, false).find(":colors=0xC08A22|0xC08A22,format=rgb0 [vo]") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_SPECTRUM, false).find(",fps="), std::string::npos);	// its own fps= only
	// Circle: drawn from its tables; without them, as Bars
	CHECK_EQ(visualizerGraph(VIS_CIRCLE, false).find("showfreqs") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_CIRCLE, false).find("remap"), std::string::npos);
	char cdir[] = "/tmp/bp-circle-XXXXXX";
	if (mkdtemp(cdir))
	{
		CHECK_EQ(writeCircleMaps(cdir), true);
		CHECK_EQ(writeCircleMaps(cdir), true);	// kept: not made again
		std::string g = visualizerGraph(VIS_CIRCLE, true, cdir);
		CHECK_EQ(g.find("remap") != std::string::npos && g.find("[vid1]") != std::string::npos, true);
		// no source that never ends: past the song's end it would flood mpv (its clock stopped, gigabytes taken)
		CHECK_EQ(g.find("loop="), std::string::npos);
		CHECK_EQ(visualizerGraph(VIS_CIRCLE, false, cdir).find("loop="), std::string::npos);
		CHECK_EQ(g.find(std::string("movie='") + cdir + "/circle3-x.pgm'") != std::string::npos, true);
		// the middle is black (the palette's last column), straight up from it a bar's lowest step
		std::ifstream in(std::string(cdir) + "/circle3-x.pgm", std::ios::binary);
		std::string pgm((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		size_t head = pgm.size() - (size_t)CIRCLE_BOX * CIRCLE_BOX * 2;
		auto at = [&](int x, int y) { size_t i = head + ((size_t)(y - CIRCLE_Y) * CIRCLE_BOX + x - CIRCLE_X) * 2; return (unsigned char)pgm[i] << 8 | (unsigned char)pgm[i + 1]; };
		CHECK_EQ(at(CIRCLE_W / 2, 274), 256 + 15);			// the middle: black
		CHECK_EQ(at(CIRCLE_W / 2 - 1, 274 - 90) < 256, true);	// straight up: a bar
		CHECK_EQ(at(CIRCLE_X + 5, CIRCLE_Y + 5), 256 + 15);	// the square's corner: black
		for (const char* f : {"/circle3-x.pgm", "/circle3-y.pgm"}) unlink((std::string(cdir) + f).c_str());
		rmdir(cdir);
	}
	CHECK_EQ(parseSettings("visualizer = circle\n").visualizer, (int)VIS_CIRCLE);
	// Peaks: read and written back by its id; Timeline (its first form) reads as Peaks
	CHECK_EQ(parseSettings("visualizer = peaks\n").visualizer, (int)VIS_PEAKS);
	CHECK_EQ(parseSettings("visualizer = timeline\n").visualizer, (int)VIS_PEAKS);
	{
		Settings t;
		t.visualizer = VIS_PEAKS;
		CHECK_EQ(serializeSettings(t).find("visualizer = peaks\n") != std::string::npos, true);
		CHECK_EQ(parseSettings(serializeSettings(t)).visualizer, (int)VIS_PEAKS);
		CHECK_EQ(std::string(visualizerLabel(VIS_PEAKS)), std::string("Peaks"));
		std::string v = visualizerGraph(VIS_PEAKS, false), a = visualizerGraph(VIS_PEAKS, true);
		for (const std::string& g : {v, a})
		{
			CHECK_EQ(g.find("[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=72x256:") != std::string::npos, true);
			CHECK_EQ(g.find("colors=white,crop=64:256:0:0,scale=64:1") != std::string::npos, true);	// the top 8 (16-24 kHz) cut off
			CHECK_EQ(g.find("lagfun=decay=") != std::string::npos, true);	// the caps' fall
			CHECK_EQ(g.find("loop=") == std::string::npos && g.find("colorkey") == std::string::npos, true);
			CHECK_EQ(g.find("rgb"), std::string::npos);	// YUV throughout: no picture converted from RGB each frame
		}
		// drawn at half size in YUV (the chroma's size), only the luma scaled up, nearest; the grid's lines after
		CHECK_EQ(v.find("[pky] scale=640:480:flags=neighbor [pkY]") != std::string::npos, true);
		CHECK_EQ(v.find("[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p,drawbox=x=0:y=96:") != std::string::npos, true);
		CHECK_EQ(v.rfind(":c=white@0.1:t=fill [vo]") == v.size() - std::string(":c=white@0.1:t=fill [vo]").size(), true);
		// over the art: no alpha nor overlay, the art's planes kept where nothing's drawn; no grid
		CHECK_EQ(a.find("crop=640:200:0:280,extractplanes=y+u+v [pkay] [pkau] [pkav]") != std::string::npos, true);
		CHECK_EQ(a.find("[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]") != std::string::npos, true);
		CHECK_EQ(a.find("overlay") == std::string::npos && a.find("drawbox") == std::string::npos, true);
	}
	// Bars, Waves and Spectrum: retired (graphs kept); a setting naming one reads, and is written back, as Peaks
	for (const char* id : {"bars", "waves", "spectrum"})
	{
		CHECK_EQ(parseSettings(std::string("visualizer = ") + id + "\n").visualizer, (int)VIS_PEAKS);
		CHECK_EQ(parseSettings(std::string("audio-display = ") + id + "\n").visualizer, (int)VIS_PEAKS);
		CHECK_EQ(serializeSettings(parseSettings(std::string("visualizer = ") + id + "\n")).find("visualizer = peaks\n") != std::string::npos, true);
	}
	CHECK_EQ(Settings().visualizer, (int)VIS_PEAKS);
	// Peaks Banana: the same graph in other colours
	CHECK_EQ(parseSettings("visualizer = peaks-yellow\n").visualizer, (int)VIS_PEAKS_BANANA);
	CHECK_EQ(parseSettings(serializeSettings(parseSettings("visualizer = peaks-yellow\n"))).visualizer, (int)VIS_PEAKS_BANANA);
	CHECK_EQ(std::string(visualizerLabel(VIS_PEAKS_BANANA)), std::string("Peaks Banana"));
	CHECK_EQ(visualizerGraph(VIS_PEAKS_BANANA, false) != visualizerGraph(VIS_PEAKS, false), true);
	CHECK_EQ(visualizerGraph(VIS_PEAKS_BANANA, true) != visualizerGraph(VIS_PEAKS, true), true);
	CHECK_EQ(visualizerGraph(VIS_PEAKS_BANANA, false).find("lagfun=decay=") != std::string::npos, true);
	// the cycle: Peaks, Peaks Banana, Circle; from a hidden one as from Peaks
	CHECK_EQ(stepVisualizer(VIS_PEAKS, 1), (int)VIS_PEAKS_BANANA);
	CHECK_EQ(stepVisualizer(VIS_PEAKS_BANANA, 1), (int)VIS_CIRCLE);
	CHECK_EQ(stepVisualizer(VIS_CIRCLE, 1), (int)VIS_PEAKS);
	CHECK_EQ(stepVisualizer(VIS_PEAKS, -1), (int)VIS_CIRCLE);
	CHECK_EQ(stepVisualizer(VIS_SPECTRUM, 1), (int)VIS_PEAKS_BANANA);
	{
		int seen = 0;
		for (int i = 0, v = VIS_PEAKS; i < VIS_SHOWN_COUNT; i++, v = stepVisualizer(v, 1)) seen |= 1 << v;
		CHECK_EQ(seen, (1 << VIS_PEAKS) | (1 << VIS_PEAKS_BANANA) | (1 << VIS_CIRCLE));
	}
	CHECK_EQ(std::string(loopLabel(LOOP_ALL)), std::string("Loop all"));
	// the setting before 0.1.1, carried over
	Settings old = parseSettings("audio-display = cover-bars\n");
	CHECK_EQ(old.musicShow == SHOW_BOTH && old.visualizer == VIS_PEAKS, true);
	old = parseSettings("audio-display = waves\n");
	CHECK_EQ(old.musicShow == SHOW_VIS && old.visualizer == VIS_PEAKS, true);
	CHECK_EQ(parseSettings("audio-display = auto\n").musicShow, (int)SHOW_ART);
	CHECK_EQ(parseSettings(serializeSettings(d)).keepHistory, true);
	CHECK_EQ(parseSettings("keep-history = no\n").keepHistory, false);
	CHECK_EQ(d.keepNetHistory, true);	// network files in Recent: on by default
	CHECK_EQ(parseSettings("keep-network-history = no\n").keepNetHistory, false);
	CHECK_EQ(d.mangaMode, MANGA_STRIP);	// the manga reader: a strip read down (the owner's default)
	CHECK_EQ(parseSettings("manga-mode = strip\n").mangaMode, MANGA_STRIP);
	CHECK_EQ(d.mangaHinge, 36);
	CHECK_EQ(parseSettings("manga-hinge = 20\n").mangaHinge, 20);
	CHECK_EQ(parseSettings("manga-hinge = 999\n").mangaHinge, 36);
	CHECK_EQ(parseSettings(serializeSettings(parseSettings("manga-hinge = 12\n"))).mangaHinge, 12);
	// fullscreen: the first open's tip seen, and a chapter opening on both panels
	CHECK_EQ(d.mangaFullSeen, false);
	CHECK_EQ(parseSettings("manga-full-seen = yes\n").mangaFullSeen, true);
	CHECK_EQ(d.mangaOpenFull, true);
	CHECK_EQ(parseSettings("manga-open-full = no\n").mangaOpenFull, false);
	Settings fs;
	fs.mangaFullSeen = true;
	fs.mangaOpenFull = true;
	CHECK_EQ(serializeSettings(fs).find("manga-full-seen = yes\n") != std::string::npos, true);
	CHECK_EQ(serializeSettings(fs).find("manga-open-full = yes\n") != std::string::npos, true);
	CHECK_EQ(parseSettings(serializeSettings(fs)).mangaFullSeen, true);
	CHECK_EQ(parseSettings(serializeSettings(fs)).mangaOpenFull, true);
	CHECK_EQ(parseSettings("manga-mode = sideways\n").mangaMode, MANGA_STRIP);
	// the Manga group's other rows: fit, swap, preload, cache, adult sources, history, Recent
	CHECK_EQ(d.mangaFit, MANGA_FIT_PAGE);
	CHECK_EQ(parseSettings("manga-fit = width\n").mangaFit, MANGA_FIT_WIDTH);
	CHECK_EQ(parseSettings("manga-fit = fill\n").mangaFit, MANGA_FIT_FILL);
	CHECK_EQ(parseSettings("manga-fit = sideways\n").mangaFit, MANGA_FIT_PAGE);
	Settings mf;
	mf.mangaFit = MANGA_FIT_FILL;
	CHECK_EQ(parseSettings(serializeSettings(mf)).mangaFit, MANGA_FIT_FILL);
	CHECK_EQ(d.mangaSwapLR, false);
	CHECK_EQ(parseSettings("manga-swap-lr = yes\n").mangaSwapLR, true);
	Settings sw;
	sw.mangaSwapLR = true;
	CHECK_EQ(parseSettings(serializeSettings(sw)).mangaSwapLR, true);
	CHECK_EQ(d.mangaPreload, MANGA_PRELOAD_DEFAULT);
	CHECK_EQ(d.mangaPreload, 3);
	CHECK_EQ(parseSettings("manga-preload = 2\n").mangaPreload, 2);
	CHECK_EQ(parseSettings("manga-preload = 6\n").mangaPreload, 6);
	CHECK_EQ(parseSettings("manga-preload = 5\n").mangaPreload, MANGA_PRELOAD_DEFAULT);
	Settings pl6;
	pl6.mangaPreload = 6;
	CHECK_EQ(parseSettings(serializeSettings(pl6)).mangaPreload, 6);
	CHECK_EQ(stepMangaPreload(2, 1), 3);
	CHECK_EQ(stepMangaPreload(3, 1), 6);
	CHECK_EQ(stepMangaPreload(6, 1), 2);
	CHECK_EQ(stepMangaPreload(2, -1), 6);
	CHECK_EQ(d.mangaCacheSize, MANGA_CACHE_DEFAULT);
	CHECK_EQ(parseSettings("manga-cache = 100m\n").mangaCacheSize, 0);
	CHECK_EQ(parseSettings("manga-cache = 1g\n").mangaCacheSize, 2);
	CHECK_EQ(parseSettings("manga-cache = 2g\n").mangaCacheSize, MANGA_CACHE_DEFAULT);
	Settings mc;
	mc.mangaCacheSize = 2;
	CHECK_EQ(parseSettings(serializeSettings(mc)).mangaCacheSize, 2);
	CHECK_EQ(mangaCacheBytes(0), (uint64_t)100 * 1024 * 1024);
	CHECK_EQ(mangaCacheBytes(1), (uint64_t)300 * 1024 * 1024);
	CHECK_EQ(mangaCacheBytes(2), (uint64_t)1024 * 1024 * 1024);
	CHECK_EQ(d.mangaAdult, false);
	CHECK_EQ(parseSettings("manga-adult = yes\n").mangaAdult, true);
	CHECK_EQ(d.mangaHistory, true);
	CHECK_EQ(parseSettings("manga-history = no\n").mangaHistory, false);
	Settings mh;
	mh.mangaHistory = false;
	CHECK_EQ(parseSettings(serializeSettings(mh)).mangaHistory, false);
	CHECK_EQ(d.mangaRecent, true);
	CHECK_EQ(parseSettings("manga-recent = no\n").mangaRecent, false);
	Settings mr;
	mr.mangaRecent = false;
	CHECK_EQ(parseSettings(serializeSettings(mr)).mangaRecent, false);
	CHECK_EQ(parseSettings(serializeSettings(d)).keepNetHistory, true);
	CHECK_EQ(parseSettings("remember-folder = yes\n").rememberFolder, true);
	// the clock: 24-hour unless set; its three values kept; anything else the default
	CHECK_EQ(d.clock, (int)CLOCK_24);
	CHECK_EQ(parseSettings("clock = 12h\n").clock, (int)CLOCK_12);
	CHECK_EQ(parseSettings("clock = off\n").clock, (int)CLOCK_OFF);
	CHECK_EQ(parseSettings("clock = sundial\n").clock, (int)CLOCK_24);
	Settings c12;
	c12.clock = CLOCK_12;
	CHECK_EQ(parseSettings(serializeSettings(c12)).clock, (int)CLOCK_12);
	// Screen off after: 30 s unless said; never = 0 seconds; a value it doesn't know keeps the default
	CHECK_EQ(d.screenOffAfter, (int)SOFF_30);
	CHECK_EQ(parseSettings("screen-off = never\n").screenOffAfter, (int)SOFF_NEVER);
	CHECK_EQ(parseSettings("screen-off = 2m\n").screenOffAfter, (int)SOFF_120);
	// a video's own timer (issue #5): Never by default, kept apart from a song's
	CHECK_EQ(d.screenOffVideo, (int)SOFF_30);
	CHECK_EQ(parseSettings("screen-off-video = 1m\n").screenOffVideo, (int)SOFF_60);
	CHECK_EQ(parseSettings("screen-off-video = 1m\n").screenOffAfter, (int)SOFF_30);
	{
		Settings v;
		v.screenOffVideo = SOFF_300;
		CHECK_EQ(parseSettings(serializeSettings(v)).screenOffVideo, (int)SOFF_300);
	}
	CHECK_EQ(parseSettings("screen-off = 3h\n").screenOffAfter, (int)SOFF_30);
	CHECK_EQ(screenOffSeconds(SOFF_NEVER), 0);
	CHECK_EQ(screenOffSeconds(SOFF_300), 300);
	Settings s5;
	s5.screenOffAfter = SOFF_300;
	CHECK_EQ(parseSettings(serializeSettings(s5)).screenOffAfter, (int)SOFF_300);
}

static void testClock()
{
	CHECK_EQ(formatClock(0, 5, false), std::string("0:05"));
	CHECK_EQ(formatClock(12, 0, false), std::string("12:00"));
	CHECK_EQ(formatClock(23, 59, false), std::string("23:59"));
	CHECK_EQ(formatClock(0, 5, true), std::string("12:05 AM"));
	CHECK_EQ(formatClock(12, 0, true), std::string("12:00 PM"));
	CHECK_EQ(formatClock(23, 59, true), std::string("11:59 PM"));
	CHECK_EQ(formatClock(14, 32, true), std::string("2:32 PM"));
}

static void testResume()
{
	ResumeStore r;
	ResumeEntry a;
	a.path = "/data/roms/videos/Show/ep 1.mkv";
	a.position = 754.5;
	a.aid = 2;
	a.sid = 0;
	r.put(a);
	ResumeEntry b;
	b.path = "/data/roms/videos/tab\there.mp4";	// a tab in the name survives: the path is last
	b.sid = 3;
	b.duration = 52.25;
	r.put(b);
	r.lastAudioLang = "jpn";
	r.lastFolder = "/data/roms/videos/Show";
	r.lastSubOff = true;

	ResumeStore back;
	back.parse(r.serialize());
	const ResumeEntry* e = back.find(a.path);
	CHECK_EQ(e != nullptr, true);
	if (e)
	{
		CHECK_EQ(e->position, 754.5);
		CHECK_EQ(e->aid, (int64_t)2);
		CHECK_EQ(e->sid, (int64_t)0);
	}
	e = back.find(b.path);
	CHECK_EQ(e != nullptr && e->position < 0 && e->aid == -1 && e->sid == 3, true);
	CHECK_EQ(e ? e->duration : 0, 52.25);	// its length: a #duration line, after its own
	e = back.find(a.path);
	CHECK_EQ(e ? e->duration : 0, -1.0);	// none known: none written
	CHECK_EQ(back.serialize(), r.serialize());
	{
		// a length with no entry before it is left out (and a broken line); after its entry, further down, it counts
		ResumeStore old;
		old.parse("#duration\t30.000\t/v/x.mkv\n12.000\t-1\t-1\t/v/x.mkv\n#duration\tbad\n");
		CHECK_EQ(old.all().size(), (size_t)1);
		CHECK_EQ(old.find("/v/x.mkv") ? old.find("/v/x.mkv")->duration : 0, -1.0);
		old.parse("12.000\t-1\t-1\t/v/x.mkv\n5.000\t-1\t-1\t/v/y.mkv\n#duration\t30.000\t/v/x.mkv\n");
		CHECK_EQ(old.find("/v/x.mkv") ? old.find("/v/x.mkv")->duration : 0, 30.0);
		CHECK_EQ(old.find("/v/y.mkv") ? old.find("/v/y.mkv")->duration : 0, -1.0);
	}
	CHECK_EQ(back.lastAudioLang, std::string("jpn"));
	CHECK_EQ(back.lastSubOff, true);
	CHECK_EQ(back.lastFolder, std::string("/data/roms/videos/Show"));
	CHECK_EQ(back.all().size(), (size_t)2);

	// put again: replaced, not added
	a.position = 10;
	back.put(a);
	CHECK_EQ(back.find(a.path)->position, 10.0);
	for (int i = 0; i < 400; i++)
	{
		ResumeEntry x;
		x.path = "/v/" + std::to_string(i);
		back.put(x);
	}
	CHECK_EQ(back.find("/v/399") != nullptr, true);
	CHECK_EQ(back.find(a.path) == nullptr, true);	// the oldest went

	CHECK_EQ(worthResuming(2, 1000), false);	// the first seconds
	CHECK_EQ(worthResuming(500, 1000), true);
	CHECK_EQ(worthResuming(990, 1000), false);	// the credits
	{
		// Recent's progress: a kept place, none (played), a song (always played), unknown length
		ResumeEntry e;
		e.position = 250; e.duration = 1000;
		RecentProgress r = recentProgress(&e, false);
		CHECK_EQ(r.played, false); CHECK_EQ(r.position, 250.0); CHECK_EQ(r.frac, 0.25);
		r = recentProgress(&e, true);
		CHECK_EQ(r.played, true); CHECK_EQ(r.position, -1.0); CHECK_EQ(r.frac, 1.0); CHECK_EQ(r.duration, 1000.0);
		e.position = -1;
		CHECK_EQ(recentProgress(&e, false).played, true);
		e.position = 30; e.duration = -1;
		r = recentProgress(&e, false);
		CHECK_EQ(r.played, false); CHECK_EQ(r.frac, 0.0); CHECK_EQ(r.duration, -1.0);
		CHECK_EQ(recentProgress(nullptr, false).played, true);
	}
	CHECK_EQ(worthResuming(120, -1), true);		// unknown length
}

static void testBrowser()
{
	CHECK_EQ(parentFolder("/data/roms/videos/Show"), std::string("/data/roms/videos"));
	CHECK_EQ(parentFolder("/data/roms/videos/Show/"), std::string("/data/roms/videos"));
	CHECK_EQ(parentFolder("/data"), std::string("/"));
	CHECK_EQ(parentFolder("/"), std::string("/"));

	char dir[] = "/tmp/bp-browser-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	mkdir((d + "/Season 10").c_str(), 0755);
	mkdir((d + "/Season 2").c_str(), 0755);
	mkdir((d + "/.dotted").c_str(), 0755);
	for (const char* n : {"b.mkv", "A.mp4", "a.srt", ".c.mkv"}) std::ofstream(d + "/" + n) << "x";
	bool ok = false;
	auto e = listFolder(d, ok);
	CHECK_EQ(ok, true);
	CHECK_EQ(e.size(), (size_t)4);	// two folders, two videos; no subtitles, nothing hidden
	if (e.size() == 4)
	{
		CHECK_EQ(e[0].name, std::string("Season 2"));
		CHECK_EQ(e[0].dir, true);
		CHECK_EQ(e[1].name, std::string("Season 10"));
		CHECK_EQ(e[2].name, std::string("A.mp4"));
		CHECK_EQ(e[3].path, d + "/b.mkv");
		CHECK_EQ(e[3].dir, false);
	}
	// a folder with a ".hidden" file: left out unless asked for
	std::ofstream(d + "/Season 2/.hidden") << "";
	std::ofstream(d + "/Season 2/x.mkv") << "x";
	CHECK_EQ(listFolder(d, ok).size(), (size_t)3);
	CHECK_EQ(listFolder(d, ok, true).size(), (size_t)4);
	CHECK_EQ(isHiddenFolder(d + "/Season 2"), true);
	CHECK_EQ(insideHiddenFolder(d + "/Season 2/x.mkv"), true);
	CHECK_EQ(insideHiddenFolder(d + "/A.mp4"), false);
	unlink((d + "/Season 2/.hidden").c_str());
	unlink((d + "/Season 2/x.mkv").c_str());
	listFolder(d + "/missing", ok);
	CHECK_EQ(ok, false);
	for (const char* n : {"b.mkv", "A.mp4", "a.srt", ".c.mkv"}) unlink((d + "/" + n).c_str());
	for (const char* n : {"Season 10", "Season 2", ".dotted"}) rmdir((d + "/" + n).c_str());
	rmdir(dir);
}

static void testHistory()
{
	PlayHistory h;
	h.add("/v/a.mkv");
	h.add("/v/b.mkv");
	h.add("/v/a.mkv");	// again: first, once
	CHECK_EQ(h.items().size(), (size_t)2);
	CHECK_EQ(h.items()[0], std::string("/v/a.mkv"));
	for (int i = 0; i < 40; i++) h.add("/v/" + std::to_string(i));
	CHECK_EQ(h.items().size(), PlayHistory::MAX);	// 30 at most: the oldest go
	CHECK_EQ(h.items()[0], std::string("/v/39"));
	CHECK_EQ(h.items().back(), std::string("/v/10"));

	{
		// the network's taken out, the card's kept in their order
		PlayHistory m;
		m.add("/card/a.mkv");
		m.add("bpnet://ab12cd34/Media/b.mkv");
		m.add("/card/c.mkv");
		m.removeIf([](const std::string& p) { return isNetPath(p); });
		CHECK_EQ(m.items().size(), (size_t)2);
		CHECK_EQ(m.items()[0], std::string("/card/c.mkv"));
		CHECK_EQ(m.items()[1], std::string("/card/a.mkv"));
	}
	PlayHistory back;
	back.parse(h.serialize() + "/v/39\n");	// a repeat in the file is read once
	CHECK_EQ(back.items() == h.items(), true);
	// Remove from Recent: that one, the rest in their order
	CHECK_EQ(back.remove("/v/38"), true);
	CHECK_EQ(back.remove("/v/38"), false);
	CHECK_EQ(back.items().size(), PlayHistory::MAX - 1);
	CHECK_EQ(back.items()[0], std::string("/v/39"));
	CHECK_EQ(back.items()[1], std::string("/v/37"));
	back.clear();
	CHECK_EQ(back.items().empty(), true);
}

static void testBattery()
{
	char dir[] = "/tmp/bp-power-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	CHECK_EQ(readBattery(d + "/none").percent, -1);	// no supplies: no battery
	mkdir((d + "/battery").c_str(), 0755);
	mkdir((d + "/usb").c_str(), 0755);
	std::ofstream(d + "/battery/type") << "Battery\n";
	std::ofstream(d + "/battery/capacity") << "57\n";
	std::ofstream(d + "/battery/status") << "Discharging\n";
	std::ofstream(d + "/usb/type") << "USB\n";
	std::ofstream(d + "/usb/online") << "0\n";
	BatteryState b = readBattery(d);
	CHECK_EQ(b.percent, 57);
	CHECK_EQ(b.charging, false);
	// the rk817's gauge says Discharging on the cable: the charger online says charging
	std::ofstream(d + "/usb/online") << "1\n";
	CHECK_EQ(readBattery(d).charging, true);
	std::ofstream(d + "/usb/online") << "0\n";
	std::ofstream(d + "/battery/status") << "Full\n";
	CHECK_EQ(readBattery(d).charging, true);
	CHECK_EQ(parseSettings("").batteryPercent, false);	// the percent: off by default
	CHECK_EQ(parseSettings("").keepPhotoZoom, true);
	CHECK_EQ(parseSettings("").panRight, false);	// the pan area: left by default
	Settings ph;
	ph.keepPhotoZoom = false;
	ph.panRight = true;
	Settings phb = parseSettings(serializeSettings(ph));
	CHECK_EQ(phb.keepPhotoZoom, false);
	CHECK_EQ(phb.panRight, true);
	CHECK_EQ(parseSettings("").checkUpdates, true);
	CHECK_EQ(parseSettings("check-updates = no\n").checkUpdates, false);
	Settings later;
	later.updateRemindAfter = 1794100000;
	CHECK_EQ(parseSettings(serializeSettings(later)).updateRemindAfter, 1794100000LL);
	CHECK_EQ(parseSettings("").dragBox, true);	// a drag moves the box over the small photo
	CHECK_EQ(parseSettings("pan-drag = photo\n").dragBox, false);
	ph.dragBox = false;
	CHECK_EQ(parseSettings(serializeSettings(ph)).dragBox, false);
	for (const char* f : {"/battery/type", "/battery/capacity", "/battery/status", "/usb/type", "/usb/online"}) unlink((d + f).c_str());
	rmdir((d + "/battery").c_str());
	rmdir((d + "/usb").c_str());
	rmdir(dir);
}

static void testPlaylist()
{
	CHECK_EQ(isVideoFile("a.MKV"), true);
	CHECK_EQ(isVideoFile("a.srt"), false);
	CHECK_EQ(isVideoFile(".hidden.mp4"), false);
	for (const char* a : {"a.mp3", "b.FLAC", "c.ogg", "d.wav", "e.m4a", "f.opus", "g.m4b"})
	{
		CHECK_EQ(isAudioFile(a), true);
		CHECK_EQ(isMediaFile(a), true);
	}
	CHECK_EQ(isAudioFile("a.mkv"), false);
	CHECK_EQ(isAviFile("My Film.AVI"), true);
	CHECK_EQ(isAviFile("bpnet://3a3cff8c/Media/a.divx"), true);
	CHECK_EQ(isAviFile("a.mkv"), false);
	CHECK_EQ(isMediaFile("a.txt"), false);
	CHECK_EQ(naturalLess("ep2.mkv", "ep10.mkv"), true);
	CHECK_EQ(naturalLess("ep10.mkv", "ep2.mkv"), false);
	CHECK_EQ(naturalLess("Ep 02.mkv", "ep 3.mkv"), true);
	CHECK_EQ(naturalLess("a.mkv", "B.mkv"), true);

	char dir[] = "/tmp/bp-playlist-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	for (const char* n : {"ep10.mkv", "ep2.mkv", "ep1.mp4", "ep2.srt", "notes.txt"}) std::ofstream(d + "/" + n) << "x";
	int index = -1;
	auto list = siblingVideos(d + "/ep2.mkv", index);
	CHECK_EQ(list.size(), (size_t)3);
	CHECK_EQ(index, 1);
	if (list.size() == 3)
	{
		CHECK_EQ(list[0], d + "/ep1.mp4");
		CHECK_EQ(list[2], d + "/ep10.mkv");
	}
	auto alone = siblingVideos(d + "/missing.mkv", index);
	CHECK_EQ(alone.size(), (size_t)1);
	CHECK_EQ(index, 0);
	for (const char* n : {"ep10.mkv", "ep2.mkv", "ep1.mp4", "ep2.srt", "notes.txt"}) unlink((d + "/" + n).c_str());

	// photos step through photos, videos and songs through videos and songs
	for (const char* n : {"a.jpg", "b.mkv", "c.PNG", "d.mp3"}) std::ofstream(d + "/" + n) << "x";
	auto photos = siblingVideos(d + "/c.PNG", index);
	CHECK_EQ(photos.size(), (size_t)2);
	CHECK_EQ(index, 1);
	auto media = siblingVideos(d + "/d.mp3", index);
	CHECK_EQ(media.size(), (size_t)2);
	CHECK_EQ(index, 1);
	if (media.size() == 2) CHECK_EQ(media[0], d + "/b.mkv");
	CHECK_EQ(folderHasCover(d + "/d.mp3"), false);	// a.jpg is no cover's name
	std::ofstream(d + "/Folder.JPG") << "x";
	CHECK_EQ(folderHasCover(d + "/d.mp3"), true);
	unlink((d + "/Folder.JPG").c_str());
	for (const char* n : {"a.jpg", "b.mkv", "c.PNG", "d.mp3"}) unlink((d + "/" + n).c_str());
	rmdir(dir);
}

static void testPhoto()
{
	CHECK_EQ(isPhotoFile("IMG_0001.JPG"), true);
	CHECK_EQ(isPhotoFile("a.webp"), true);
	CHECK_EQ(yearFromDate("2021-05-03"), "2021");
	CHECK_EQ(yearFromDate("20210503"), "2021");
	CHECK_EQ(yearFromDate("May 1999"), "1999");
	CHECK_EQ(yearFromDate("12"), "");
	CHECK_EQ(yearFromDate("0000"), "");
	CHECK_EQ(trackText(" 3/12 "), "3/12");
	CHECK_EQ(trackText("x"), "");
	CHECK_EQ(isPhotoFile("a.mkv"), false);
	CHECK_EQ(isMediaFile("cover.jpg"), true);

	CHECK_EQ(stepZoom(1, 1), 1.5);
	CHECK_EQ(stepZoom(1.5, 1), 2.0);
	CHECK_EQ(stepZoom(8, 1), 8.0);
	CHECK_EQ(stepZoom(1, -1), 1.0);
	CHECK_EQ(stepZoom(2.5, -1), 2.0);	// from between steps: the one below
	CHECK_EQ(stepZoom(2.5, 1), 3.0);

	// a 4:3 photo on the 4:3 panel fits exactly; 2x: twice the panel, half a panel each way to move
	PhotoScale s = photoScale(4.0 / 3, 4.0 / 3, false, 1);
	CHECK_EQ(s.x, 1.0);
	CHECK_EQ(s.y, 1.0);
	CHECK_EQ(maxPan(1), 0.0);
	CHECK_EQ(maxPan(2), 0.25);
	// a 2:3 portrait fits by its height: half the panel's width
	s = photoScale(2.0 / 3, 4.0 / 3, false, 1);
	CHECK_EQ(s.x, 0.5);
	CHECK_EQ(s.y, 1.0);
	// filled: as wide as the panel, twice as tall
	s = photoScale(2.0 / 3, 4.0 / 3, true, 1);
	CHECK_EQ(s.x, 1.0);
	CHECK_EQ(s.y, 2.0);

	// an EXIF block: little-endian, IFD0 with Make and Orientation 6, an Exif IFD with DateTimeOriginal
	auto le16 = [](std::string& b, unsigned v) { b += (char)(v & 0xFF); b += (char)(v >> 8); };
	auto le32 = [&](std::string& b, unsigned v) { le16(b, v & 0xFFFF); le16(b, v >> 16); };
	std::string tiff = "II";
	le16(tiff, 42);
	le32(tiff, 8);
	// IFD0 at 8: 3 entries (12 bytes each), next-IFD 0 -> ends at 8 + 2 + 36 + 4 = 50
	le16(tiff, 3);
	le16(tiff, 0x010F); le16(tiff, 2); le32(tiff, 6); le32(tiff, 50);	// Make "Canon\0" at 50
	le16(tiff, 0x0112); le16(tiff, 3); le32(tiff, 1); le32(tiff, 6);
	le16(tiff, 0x8769); le16(tiff, 4); le32(tiff, 1); le32(tiff, 56);	// Exif IFD at 56
	le32(tiff, 0);
	tiff += std::string("Canon\0", 6);
	le16(tiff, 1);	// at 56: one entry, its text at 56 + 2 + 12 + 4 = 74
	le16(tiff, 0x9003); le16(tiff, 2); le32(tiff, 20); le32(tiff, 74);
	le32(tiff, 0);
	tiff += std::string("2024:05:01 13:45:10\0", 20);
	std::string app1 = std::string("Exif\0\0", 6) + tiff;
	std::string jpeg = "\xFF\xD8\xFF\xE1";
	jpeg += (char)((app1.size() + 2) >> 8);
	jpeg += (char)((app1.size() + 2) & 0xFF);
	jpeg += app1 + "\xFF\xDA";
	ExifInfo e = parseExif(jpeg);
	CHECK_EQ(e.orientation, 6);
	CHECK_EQ(e.make, std::string("Canon"));
	CHECK_EQ(e.taken, std::string("2024:05:01 13:45:10"));
	CHECK_EQ(exifRotation(e.orientation), 90);
	CHECK_EQ(exifDate(e.taken), std::string("2024-05-01 13:45"));
	CHECK_EQ(parseExif("not a jpeg").orientation, 1);
	// padded with 0xFF before the marker: still found
	std::string padded = "\xFF\xD8\xFF" + jpeg.substr(2);
	CHECK_EQ(parseExif(padded).orientation, 6);
	// an IFD offset out of the block: nothing read from the header's bytes
	std::string badOff = jpeg;
	badOff[2 + 2 + 2 + 6 + 4] = (char)0xF0;	// IFD0's offset (in the TIFF header, after "II" 42)
	CHECK_EQ(parseExif(badOff).make, std::string());
	CHECK_EQ(exifDate("garbage that is long"), std::string());
	CHECK_EQ(lavfiSafePath("/data/home/.config/banana-player"), true);
	CHECK_EQ(lavfiSafePath("/a/co:lon"), false);
	CHECK_EQ(lavfiSafePath("/a/eq=x"), false);
	CHECK_EQ(visualizerGraph(VIS_CIRCLE, false, "/a/co:lon").find("remap"), std::string::npos);	// as Bars
	CHECK_EQ(exifRotation(8), 270);
}

static void testUpdate()
{
	CHECK_EQ(compareVersions("0.2.10", "0.2.9") > 0, true);
	CHECK_EQ(compareVersions("v0.2.2", "0.2.2"), 0);
	CHECK_EQ(compareVersions("0.2", "0.2.1") < 0, true);
	CHECK_EQ(compareVersions("1.0.0", "0.9.9") > 0, true);
	CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	// two blocks (56 bytes: the padding spills over)
	CHECK_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
	CHECK_EQ(parseSha256File("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD  x.zip\n"),
		std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(parseSha256File("not a digest"), std::string());

	// GitHub's answer, in its order: the zip's name, size, then its URL (an uploader's object between, no size)
	std::string json = "{\"tag_name\": \"v0.3.0\", \"name\": \"Banana Player 0.3.0\", \"assets\": ["
		"{\"name\": \"BananaPlayer-0.3.0-aarch64.zip\", \"uploader\": {\"login\": \"x\"}, \"size\": 14344040, "
		"\"browser_download_url\": \"https:\\/\\/github.com\\/a\\/BananaPlayer-0.3.0-aarch64.zip\"},"
		"{\"name\": \"BananaPlayer-0.3.0-aarch64.zip.sha256\", \"size\": 97, "
		"\"browser_download_url\": \"https://github.com/a/BananaPlayer-0.3.0-aarch64.zip.sha256\"}]}";
	Release r;
	CHECK_EQ(parseRelease(json, r), true);
	CHECK_EQ(r.version, std::string("0.3.0"));
	CHECK_EQ(r.zipUrl, std::string("https://github.com/a/BananaPlayer-0.3.0-aarch64.zip"));
	CHECK_EQ(r.shaUrl, std::string("https://github.com/a/BananaPlayer-0.3.0-aarch64.zip.sha256"));
	CHECK_EQ(r.zipSize, (int64_t)14344040);
	CHECK_EQ(parseRelease("{\"message\": \"API rate limit exceeded\"}", r), false);

	// tomorrow: after now, within a day, at a midnight
	time_t now = time(nullptr), m = nextMidnight(now);
	struct tm t;
	localtime_r(&m, &t);
	CHECK_EQ(m > now && m - now <= 25 * 3600, true);
	CHECK_EQ(t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec == 0, true);
}

static void testBacklight()
{
	CHECK_EQ(stepBrightness(740, 1), 800);	// between steps (bananad's keys): to the next ten
	CHECK_EQ(stepBrightness(740, -1), 700);
	CHECK_EQ(stepBrightness(700, 1), 800);
	CHECK_EQ(stepBrightness(700, -1), 600);
	CHECK_EQ(stepBrightness(100, -1), 100);	// 10% at least: never off
	CHECK_EQ(stepBrightness(1000, 1), 1000);

	char dir[] = "/tmp/bp-backlight-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	Backlight bl;
	bl.sysDir = d + "/sys";
	bl.store = d + "/backlight";
	for (const char* n : {"backlight0", "backlight1"})
	{
		mkdir((d + "/sys").c_str(), 0755);
		mkdir((d + "/sys/" + n).c_str(), 0755);
		std::ofstream(d + "/sys/" + n + "/max_brightness") << "1666\n";
		std::ofstream(d + "/sys/" + n + "/brightness") << "833\n";
	}
	CHECK_EQ(bl.present(Backlight::TOP), true);
	CHECK_EQ(bl.permille(Backlight::BOTTOM), 500);
	// the top (backlight1) to 70%: sysfs as bananad rounds it, the store "<top> <bottom>"
	CHECK_EQ(bl.set(Backlight::TOP, 700), true);
	std::ifstream b1(d + "/sys/backlight1/brightness");
	int v = 0;
	b1 >> v;
	CHECK_EQ(v, (1666 * 700 + 500) / 1000);
	CHECK_EQ(bl.permille(Backlight::TOP), 700);
	std::ifstream st(d + "/backlight");
	int t = 0, b = 0;
	st >> t >> b;
	CHECK_EQ(t, 700);
	CHECK_EQ(b, 500);	// the bottom as it was
	// the bottom next: the top kept from the store
	bl.set(Backlight::BOTTOM, 300);
	std::ifstream st2(d + "/backlight");
	st2 >> t >> b;
	CHECK_EQ(t == 700 && b == 300, true);
	Backlight none;
	none.sysDir = d + "/none";
	CHECK_EQ(bl.present(Backlight::TOP) && !none.present(Backlight::TOP), true);
	system(("rm -rf " + d).c_str());
}

static void testFtpList()
{
	auto m = parseMlsd("type=cdir;modify=20261107; .\r\ntype=pdir; ..\r\ntype=dir;modify=20261107100000; Yellow Album\r\n"
		"type=file;size=1234;modify=20261107100000; Sintel (trailer).mkv\r\ntype=OS.unix=slink:/x;size=3; link\r\nbad line\r\n");
	CHECK_EQ(m.size(), (size_t)3);
	CHECK_EQ(m[0].name, std::string("Yellow Album"));
	CHECK_EQ(m[0].dir, true);
	CHECK_EQ(m[0].size, (int64_t)-1);
	CHECK_EQ(m[1].name, std::string("Sintel (trailer).mkv"));
	CHECK_EQ(m[1].dir, false);
	CHECK_EQ(m[1].size, (int64_t)1234);
	CHECK_EQ(m[2].name, std::string("link"));
	CHECK_EQ(parseMlsd("").size(), (size_t)0);
	CHECK_EQ(parseMlsd("Type=DIR;Size=5; Up\n")[0].dir, true);

	auto u = parseFtpList("total 12\r\ndrwxr-xr-x    2 1000     1000         4096 Nov  7 10:00 .\r\ndrwxr-xr-x    4 1000     1000         4096 Nov  7 10:00 ..\r\n"
		"drwxr-xr-x    2 1000     1000         4096 Nov  7 10:00 My Movies\r\n"
		"-rw-r--r--    1 1000     1000      6049358 Nov  7  2025 Sintel  (trailer).mkv\r\n"
		"lrwxrwxrwx    1 root     root            9 Jan  1 00:00 latest -> Movies/\r\n"
		"lrwxrwxrwx    1 root     root            9 Jan  1 00:00 a.mp3 -> b.mp3\r\n"
		"-rw-r--r--   1 owner 12 Dec 31 23:59 nogroup.txt\r\n");
	CHECK_EQ(u.size(), (size_t)5);
	CHECK_EQ(u[0].name, std::string("My Movies"));
	CHECK_EQ(u[0].dir, true);
	CHECK_EQ(u[1].name, std::string("Sintel  (trailer).mkv"));
	CHECK_EQ(u[1].size, (int64_t)6049358);
	CHECK_EQ(u[2].name, std::string("latest"));
	CHECK_EQ(u[2].dir, true);
	CHECK_EQ(u[3].name, std::string("a.mp3"));
	CHECK_EQ(u[3].dir, false);
	CHECK_EQ(u[4].name, std::string("nogroup.txt"));
	CHECK_EQ(u[4].size, (int64_t)12);

	auto d = parseFtpList("11-07-26  10:00AM       <DIR>          Photos 2026\r\n11-07-2026  02:15PM            6049358 trailer one.mkv\r\n");
	CHECK_EQ(d.size(), (size_t)2);
	CHECK_EQ(d[0].name, std::string("Photos 2026"));
	CHECK_EQ(d[0].dir, true);
	CHECK_EQ(d[1].name, std::string("trailer one.mkv"));
	CHECK_EQ(d[1].size, (int64_t)6049358);
	CHECK_EQ(parseFtpList("garbage\r\n").size(), (size_t)0);
}

static void testNet()
{
	{
		NetServer a;
		a.host = "TONY-PC";
		a.port = 445;
		CHECK_EQ(automaticServerName(a), std::string("TONY-PC"));
		CHECK_EQ(serverName(a), std::string("TONY-PC"));
		a.share = "Media";
		CHECK_EQ(serverName(a), std::string("Media on TONY-PC"));
		a.port = 1445;
		CHECK_EQ(serverName(a), std::string("Media on TONY-PC:1445"));
		a.name = "  ";
		CHECK_EQ(serverName(a), std::string("Media on TONY-PC:1445"));
		a.name = "Living room";
		CHECK_EQ(serverName(a), std::string("Living room"));
		NetServer f;
		f.type = NetType::Ftp;
		f.port = 21;
		CHECK_EQ(serverName(f), std::string("FTP server"));
		f.host = "192.168.1.9";
		CHECK_EQ(serverName(f), std::string("192.168.1.9"));
	}

	CHECK_EQ(isNetPath("bpnet://ab12/x"), true);
	CHECK_EQ(isNetPath("/home/x"), false);
	NetPath n;
	CHECK_EQ(parseNetPath("bpnet://ab12cd34", n), true);
	CHECK_EQ(n.server, std::string("ab12cd34"));
	CHECK_EQ(n.path, std::string("/"));
	CHECK_EQ(parseNetPath("bpnet:///x", n), false);
	CHECK_EQ(parseNetPath("file:///x", n), false);
	CHECK_EQ(makeNetPath("s", "//A/./B/../C//"), std::string("bpnet://s/A/C"));
	CHECK_EQ(makeNetPath("s", "/../.."), std::string("bpnet://s/"));
	CHECK_EQ(makeNetPath("s", ""), std::string("bpnet://s/"));
	CHECK_EQ(netParent("bpnet://s/A/B"), std::string("bpnet://s/A"));
	CHECK_EQ(netParent("bpnet://s/A"), std::string("bpnet://s/"));
	CHECK_EQ(netParent("bpnet://s/"), std::string("bpnet://s/"));
	CHECK_EQ(netBaseName("bpnet://s/A/b.mkv"), std::string("b.mkv"));
	CHECK_EQ(netBaseName("bpnet://s/"), std::string(""));
	CHECK_EQ(netJoin("bpnet://s/", "A"), std::string("bpnet://s/A"));
	CHECK_EQ(netJoin("bpnet://s/A", "b c"), std::string("bpnet://s/A/b c"));
	const char* names[] = {"Sintel (trailer) #1 ?.mkv", "100%.mp4", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4.mkv", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e a.mp4"};
	for (auto nm : names)
	{
		std::string u = netJoin("bpnet://s/Share", nm);
		CHECK_EQ(u.find('?'), std::string::npos);
		CHECK_EQ(u.find('#'), std::string::npos);
		CHECK_EQ(parseNetPath(u, n), true);
		CHECK_EQ(n.path, std::string("/Share/") + nm);
		CHECK_EQ(netBaseName(u), std::string(nm));
		CHECK_EQ(netDecode(netEncode(nm)), std::string(nm));
	}
	CHECK_EQ(netEncode("a%b?c#d\n"), std::string("a%25b%3Fc%23d%0A"));
	CHECK_EQ(netDecode("100%"), std::string("100%"));
	std::string id = newServerId();
	CHECK_EQ(id.size(), (size_t)8);
	CHECK_EQ(id.find_first_not_of("0123456789abcdef"), std::string::npos);

	// servers.conf
	CHECK_EQ(defaultPort(NetType::Smb), 445);
	CHECK_EQ(defaultPort(NetType::Ftp), 21);
	CHECK_EQ(std::string(netTypeLabel(NetType::Ftp)), std::string("FTP"));
	NetServer a;
	a.id = "aaaa1111"; a.name = " NAS = [home]\nline2\\ \xed\x95\x9c "; a.host = "nas.local"; a.user = "tab\tuser";
	a.secret = "v1:abc+/="; a.share = "Media"; a.folder = "/Films"; a.lastUsed = 1700000000123LL;
	NetServer b;
	b.id = "bbbb2222"; b.name = "Cam"; b.host = "10.0.0.2"; b.port = 2121; b.type = NetType::Ftp; b.guest = true; b.ftps = true;
	std::string text = serializeServers({a, b}) + "[other x]\nname = no\n[server cccc3333]\nfuture = 1\nname = C\n";
	auto got = parseServers(text);
	CHECK_EQ(got.size(), (size_t)3);
	if (got.size() == 3)
	{
		CHECK_EQ(got[0].name, a.name);
		CHECK_EQ(got[0].user, a.user);
		CHECK_EQ(got[0].secret, a.secret);
		CHECK_EQ(got[0].folder, a.folder);
		CHECK_EQ(got[0].lastUsed, a.lastUsed);
		CHECK_EQ(got[0].port, 0);
		CHECK_EQ(got[1].port, 2121);
		CHECK_EQ(got[1].type == NetType::Ftp, true);
		CHECK_EQ(got[1].guest, true);
		CHECK_EQ(got[1].ftps, true);
		CHECK_EQ(got[2].name, std::string("C"));
	}
	CHECK_EQ(parseServers("").size(), (size_t)0);

	// secret
	std::string raw;
	for (int i = 0; i < 256; i++) raw += (char)i;
	for (size_t len : {0, 1, 2, 3, 255, 256})
	{
		std::string e = base64Encode(raw.substr(0, len)), d;
		CHECK_EQ(base64Decode(e, d), true);
		CHECK_EQ(d, raw.substr(0, len));
	}
	CHECK_EQ(base64Encode("Man"), std::string("TWFu"));
	CHECK_EQ(base64Encode("Ma"), std::string("TWE="));
	std::string tmp;
	CHECK_EQ(base64Decode("TW=u", tmp), false);
	CHECK_EQ(base64Decode("TWF", tmp), false);
	std::string k1 = deriveKey("file-key", "serial1"), k2 = deriveKey("file-key", "serial2");
	CHECK_EQ(k1.size(), (size_t)32);
	CHECK_EQ(k1 == k2, false);
	CHECK_EQ(deriveKey("file-key", "serial1") == k1, true);
	CHECK_EQ(deriveKey("file-key", "").size(), (size_t)32);
	for (std::string pw : {std::string("hunter2 \xed\x95\x9c"), std::string(""), raw})
	{
		std::string s = sealSecret(pw, k1), out = "x";
		CHECK_EQ(s.compare(0, 3, "v1:"), 0);
		CHECK_EQ(openSecret(s, k1, out), true);
		CHECK_EQ(out, pw);
		CHECK_EQ(openSecret(s, k2, out), false);
		CHECK_EQ(sealSecret(pw, k1) == s, false);	// a fresh nonce each time
	}
	std::string s = sealSecret("pw", k1), out;
	for (size_t i = 3; i < s.size(); i += 5)
	{
		std::string bad = s;
		bad[i] = bad[i] == 'A' ? 'B' : 'A';
		CHECK_EQ(openSecret(bad, k1, out), false);
	}
	CHECK_EQ(openSecret("v2:" + s.substr(3), k1, out), false);
	CHECK_EQ(openSecret("v1:AAAA", k1, out), false);
	CHECK_EQ(openSecret(s, "short", out), false);
	std::string kf = "/tmp/bp-test-key-" + std::to_string(getpid()), key, again;
	unlink(kf.c_str());
	CHECK_EQ(loadOrCreateKey(kf, key), true);
	CHECK_EQ(key.size(), (size_t)32);
	CHECK_EQ(loadOrCreateKey(kf, again), true);
	CHECK_EQ(again == key, true);
	struct stat st;
	CHECK_EQ(stat(kf.c_str(), &st), 0);
	CHECK_EQ((int)(st.st_mode & 0777), 0600);
	unlink(kf.c_str());

	// sidecar
	std::vector<std::string> dir = {"movie.mkv", "movie.srt", "Movie.eng.forced.ASS", "movie - English.srt", "other.srt", "movie.nfo", "movie.idx", "movie.sub"};
	auto subs = subtitleFilesFor("movie.mkv", dir);
	CHECK_EQ(subs.size(), (size_t)5);
	CHECK_EQ(subs[0], std::string("movie.srt"));
	CHECK_EQ(subs[1], std::string("Movie.eng.forced.ASS"));
	CHECK_EQ(subtitleFilesFor("none.mkv", dir).size(), (size_t)0);
	CHECK_EQ(subtitleFilesFor("", dir).size(), (size_t)0);
	CHECK_EQ(coverFileFor({"a.jpg", "Folder.JPG", "cover.png"}), std::string("cover.png"));
	CHECK_EQ(coverFileFor({"x.mp3", ".folder.jpg"}), std::string(".folder.jpg"));
	CHECK_EQ(coverFileFor({"cover.txt", "photo.jpg"}), std::string(""));
}

static void testKeyboard()
{
	auto keys = keyboardLayout(false, ShiftState::Off, 0, 200, 640, 260);
	CHECK_EQ(keys.size(), (size_t)48);
	// the rows tile the width exactly
	for (int r = 0; r < 5; r++)
	{
		int left = 1 << 30, right = 0;
		for (auto& k : keys)
			if (k.row == r) { left = std::min(left, k.x); right = std::max(right, k.x + k.w); }
		CHECK_EQ(left, 0);
		CHECK_EQ(right, 640);
	}
	int q = keyboardHome(keys);
	CHECK_EQ(keys[q].label, std::string("q"));
	// right from q: w; left from q: round to the row's end (-)
	CHECK_EQ(keys[keyboardMove(keys, q, 1, 0)].label, std::string("w"));
	CHECK_EQ(keys[keyboardMove(keys, q, -1, 0)].label, std::string("-"));
	// up from q: 1; up from 1: round to the bottom row's nearest (#+=, under it)
	int one = keyboardMove(keys, q, 0, -1);
	CHECK_EQ(keys[one].label, std::string("1"));
	CHECK_EQ(keys[keyboardMove(keys, one, 0, -1)].label, std::string("#+="));
	// down from b (row 3): Space
	int b = -1;
	for (int i = 0; i < (int)keys.size(); i++)
		if (keys[i].label == "b") b = i;
	CHECK_EQ(keys[keyboardMove(keys, b, 0, 1)].action == KeyAction::Space, true);
	CHECK_EQ(keys[keyboardHit(keys, keys[b].x + 2, keys[b].y + 2)].label, std::string("b"));
	CHECK_EQ(keyboardHit(keys, 5, 100), -1);
	// shifted: capitals, typed as drawn
	auto up = keyboardLayout(false, ShiftState::Once, 0, 200, 640, 260);
	CHECK_EQ(up[keyboardHome(up)].text, std::string("Q"));
	auto sym = keyboardLayout(true, ShiftState::Off, 0, 200, 640, 260);
	CHECK_EQ(sym[11].text, std::string("!"));

	TextEdit t;
	t.set("ab");
	t.insert("c");
	CHECK_EQ(t.text, std::string("abc"));
	t.left();
	t.left();
	t.insert("\xc3\xa9");	// é: two bytes
	CHECK_EQ(t.text, std::string("a\xc3\xa9" "bc"));
	t.backspace();	// the whole é
	CHECK_EQ(t.text, std::string("abc"));
	t.set("x\xc3\xa9");
	t.left();	// before é, not inside it
	CHECK_EQ(t.caret, (size_t)1);
	t.right();
	CHECK_EQ(t.caret, (size_t)3);
	t.backspace();
	t.backspace();
	t.backspace();	// nothing left: nothing happens
	CHECK_EQ(t.text, std::string(""));
}

static void testEq()
{
	// gains round-trip
	const EqGains& bass = builtinEqPresets()[1].gains;
	CHECK_EQ(builtinEqPresets()[1].name, std::string("Bass Boost"));
	CHECK_EQ(builtinEqPresets().size(), (size_t)12);
	CHECK_EQ(formatEqGains(bass), std::string("3,5,5,3,1,0,0,0,0,0"));
	EqGains g;
	CHECK_EQ(parseEqGains("3,5,5,3,1,0,0,0,0,0", g), true);
	CHECK_EQ(g == bass, true);
	EqGains frac{-3.5, 0.5, 12, -12, 0.1, 0, 0, 0, 0, 7.3};
	EqGains back;
	CHECK_EQ(parseEqGains(formatEqGains(frac), back), true);
	CHECK_EQ(back == frac, true);
	// bad strings leave the gains alone
	EqGains keep = bass, bad = bass;
	const char* BAD[] = {"1,2,3,4,5,6,7,8,9", "1,2,3,4,5,6,7,8,9,10,11", "abc", "1,,2,3,4,5,6,7,8,9", "nan,0,0,0,0,0,0,0,0,0",
		"1,2,3,4,5,6,7,8,9,x", "1,2,3,4,5,6,7,8,9,10,", "", "inf,0,0,0,0,0,0,0,0,0"};
	for (const char* b : BAD)
	{
		CHECK_EQ(parseEqGains(b, bad), false);
		CHECK_EQ(bad == keep, true);
	}
	// clamping
	CHECK_EQ(clampEqGain(20), 12.0);
	CHECK_EQ(clampEqGain(-20), -12.0);
	CHECK_EQ(clampEqGain(1.26), 1.3);
	CHECK_EQ(clampEqGain(0.04), 0.0);
	CHECK_EQ(parseEqGains("99,-99,0,0,0,0,0,0,0,0", g), true);
	CHECK_EQ(g[0] == 12 && g[1] == -12, true);
	// the filter
	CHECK_EQ(eqFlat(EqGains{}), true);
	CHECK_EQ(eqFilterString(EqGains{}), std::string(""));
	// a song's af starts with its input brought to 44.1/48 kHz (the EQ, effect and speed work at that rate; the af's
	// output rate then never changes with the EQ, which would reopen the sound output)
	CHECK_EQ(afInputFilter(), std::string("@bpin:lavfi=[aformat=sample_rates=44100|48000]"));
	CHECK_EQ(eqFlat(bass), false);
	CHECK_EQ(eqBandTarget(3), std::string("equalizer@b3"));
	std::string pre = eqPreGainArg(bass);
	CHECK_EQ(pre.size() > 3 && pre[0] == '-' && pre.substr(pre.size() - 2) == "dB", true);
	std::string want = "@bpeq:lavfi=[aformat=sample_fmts=fltp,volume@pre=volume=" + pre + ":precision=float"
		",equalizer@b0=f=31:t=o:w=1:g=3.0:precision=f64,equalizer@b1=f=62:t=o:w=1:g=5.0:precision=f64"
		",equalizer@b2=f=125:t=o:w=1:g=5.0:precision=f64,equalizer@b3=f=250:t=o:w=1:g=3.0:precision=f64"
		",equalizer@b4=f=500:t=o:w=1:g=1.0:precision=f64,equalizer@b5=f=1000:t=o:w=1:g=0.0:precision=f64"
		",equalizer@b6=f=2000:t=o:w=1:g=0.0:precision=f64,equalizer@b7=f=4000:t=o:w=1:g=0.0:precision=f64"
		",equalizer@b8=f=8000:t=o:w=1:g=0.0:precision=f64,equalizer@b9=f=16000:t=o:w=1:g=0.0:precision=f64]";
	CHECK_EQ(eqFilterString(bass), want);
	EqGains cut = builtinEqPresets()[2].gains;
	CHECK_EQ(eqPreGainArg(cut), std::string("0.0dB"));	// a cut only: nothing to take off (and no "-0.0")
	// a lone band: the response at its centre is its gain
	for (int i = 0; i < EQ_BANDS; i++)
		for (double gain : {-12.0, -6.0, 3.0, 12.0})
		{
			EqGains one{};
			one[i] = gain;
			CHECK_EQ(std::fabs(eqResponseDb(one, EQ_FREQS[i]) - gain) < 0.01, true);
		}
	EqGains none{};
	CHECK_EQ(eqResponseDb(none, 1000), 0.0);
	// far from the band it hardly acts
	EqGains lone{};
	lone[5] = 6;
	CHECK_EQ(std::fabs(eqResponseDb(lone, 31)) < 0.3, true);
	double peak = eqPeakDb(bass);
	CHECK_EQ(peak > 6 && peak < 7, true);
	// the pre-gain takes the peak off
	CHECK_EQ(std::fabs(atof(pre.c_str()) + peak) < 0.1, true);
	// names
	std::vector<EqPreset> user;
	EqPreset mine;
	mine.name = "My car";
	user.push_back(mine);
	CHECK_EQ((int)checkEqPresetName("Road trip", user), (int)EqNameError::None);
	CHECK_EQ((int)checkEqPresetName("  Road trip  ", user), (int)EqNameError::None);
	CHECK_EQ((int)checkEqPresetName("", user), (int)EqNameError::Empty);
	CHECK_EQ((int)checkEqPresetName("   ", user), (int)EqNameError::Empty);
	CHECK_EQ((int)checkEqPresetName(std::string(24, 'a'), user), (int)EqNameError::None);
	CHECK_EQ((int)checkEqPresetName(std::string(25, 'a'), user), (int)EqNameError::TooLong);
	CHECK_EQ((int)checkEqPresetName("a\x01" "b", user), (int)EqNameError::BadChar);
	CHECK_EQ((int)checkEqPresetName("Rock", user), (int)EqNameError::Builtin);
	CHECK_EQ((int)checkEqPresetName("rOCK", user), (int)EqNameError::Builtin);
	CHECK_EQ((int)checkEqPresetName("Custom", user), (int)EqNameError::Builtin);
	CHECK_EQ((int)checkEqPresetName("my CAR", user), (int)EqNameError::Taken);
	CHECK_EQ((int)checkEqPresetName("My car", user, "My car"), (int)EqNameError::None);	// rename to itself
	CHECK_EQ((int)checkEqPresetName("MY CAR", user, "My car"), (int)EqNameError::None);	// or its case
	CHECK_EQ((int)checkEqPresetName("Rock", user, "My car"), (int)EqNameError::Builtin);
	user.push_back(EqPreset{"Other", {}, false});
	CHECK_EQ((int)checkEqPresetName("other", user, "My car"), (int)EqNameError::Taken);
	CHECK_EQ((int)checkEqPresetName("Caf\xc3\xa9", user), (int)EqNameError::None);
	// the store
	EqPresetStore st;
	st.parse("# banana-player: presets\r\n"
		"1\t2\t3\t4\t5\t6\t7\t8\t9\t10\tZed\r\n"
		"# a comment\n"
		"1\t2\t3\tshort line\n"
		"0\t0\t0\t0\t0\t0\t0\t0\t0\tx\tnine gains\n"
		"20\t-20\t0\t0\t0\t0\t0\t0\t0\t0\tClamped\n"
		"1\t1\t1\t1\t1\t1\t1\t1\t1\t1\tzed\n"
		"2\t2\t2\t2\t2\t2\t2\t2\t2\t2\tRock\n"
		"0\t0\t0\t0\t0\t0\t0\t0\t0\t0\tbad\x01name\n"
		"0\t0\t0\t0\t0\t0\t0\t0\t0\t3\tCaf\xc3\xa9\n"
		"0\t0\t0\t0\t0\t0\t0\t0\t0\t4\ta=b # c\n");
	CHECK_EQ(st.user().size(), (size_t)5);
	CHECK_EQ(st.all().size(), builtinEqPresets().size() + 5);
	CHECK_EQ(st.all()[0].name, std::string("Flat"));	// built-ins first
	CHECK_EQ(st.all()[0].builtin, true);
	CHECK_EQ(st.find("Zed") != nullptr && st.find("Zed")->gains[9] == 10, true);	// the first duplicate stays
	CHECK_EQ(st.find("Zed")->builtin, false);
	CHECK_EQ(st.find("Clamped") != nullptr && st.find("Clamped")->gains[0] == 12 && st.find("Clamped")->gains[1] == -12, true);
	CHECK_EQ(st.find("Rock (2)") != nullptr && st.find("Rock (2)")->gains[0] == 2, true);
	CHECK_EQ(st.find("Rock")->builtin, true);
	CHECK_EQ(st.find("Caf\xc3\xa9") != nullptr, true);
	CHECK_EQ(st.find("a=b # c") != nullptr, true);
	CHECK_EQ(st.find("badname") == nullptr, true);
	std::vector<std::string> order;
	for (auto& p : st.user()) order.push_back(p.name);
	CHECK_EQ(order == std::vector<std::string>({"a=b # c", "Caf\xc3\xa9", "Clamped", "Rock (2)", "Zed"}), true);	// alphabetical
	EqPresetStore again;
	again.parse(st.serialize());
	CHECK_EQ(again.user().size(), st.user().size());
	bool same = again.user().size() == st.user().size();
	for (size_t i = 0; same && i < st.user().size(); i++)
		same = again.user()[i].name == st.user()[i].name && again.user()[i].gains == st.user()[i].gains;
	CHECK_EQ(same, true);
	CHECK_EQ(st.serialize().rfind("# banana-player:", 0), (size_t)0);
	// put / rename / remove
	EqPresetStore ps;
	CHECK_EQ((int)ps.put("Rock", bass), (int)EqNameError::Builtin);
	CHECK_EQ((int)ps.put("  My car ", bass), (int)EqNameError::None);
	CHECK_EQ(ps.find("My car") != nullptr, true);
	CHECK_EQ((int)ps.put("My car", cut), (int)EqNameError::None);	// overwrites
	CHECK_EQ(ps.user().size(), (size_t)1);
	CHECK_EQ(ps.find("My car")->gains == cut, true);
	CHECK_EQ((int)ps.put("Alpha", bass), (int)EqNameError::None);
	CHECK_EQ(ps.user()[0].name, std::string("Alpha"));
	CHECK_EQ((int)ps.rename("My car", "alpha"), (int)EqNameError::Taken);
	CHECK_EQ((int)ps.rename("My car", "MY CAR"), (int)EqNameError::None);
	CHECK_EQ((int)ps.rename("MY CAR", "Rock"), (int)EqNameError::Builtin);
	CHECK_EQ((int)ps.rename("MY CAR", "Car"), (int)EqNameError::None);
	CHECK_EQ(ps.find("Car") != nullptr && ps.find("MY CAR") == nullptr, true);
	CHECK_EQ(ps.remove("Car"), true);
	CHECK_EQ(ps.remove("Car"), false);
	CHECK_EQ(ps.remove("Rock"), false);
	CHECK_EQ(ps.all().size(), builtinEqPresets().size() + 1);
	// settings
	Settings ed = parseSettings("");
	CHECK_EQ(ed.eqOn, false);
	CHECK_EQ(ed.eqPreset, std::string("Flat"));
	CHECK_EQ(ed.eqOrigin, std::string(""));
	CHECK_EQ(eqFlat(ed.eqGains), true);
	Settings es;
	es.eqOn = true;
	es.eqPreset = "";	// Custom
	es.eqOrigin = "Bass Boost";
	es.eqGains = frac;
	Settings eb = parseSettings(serializeSettings(es));
	CHECK_EQ(eb.eqOn, true);
	CHECK_EQ(eb.eqPreset, std::string(""));
	CHECK_EQ(eb.eqOrigin, std::string("Bass Boost"));
	CHECK_EQ(eb.eqGains == frac, true);
	es.eqPreset = "a=b # c";
	CHECK_EQ(parseSettings(serializeSettings(es)).eqPreset, std::string("a=b # c"));
	CHECK_EQ(parseSettings("eq-gains = 1,2,3\n").eqGains == EqGains{}, true);	// a bad curve: flat
	CHECK_EQ(parseSettings("eq = maybe\n").eqOn, false);
}

static double wavRms(const std::string& w, size_t from, size_t to)
{
	// 16-bit stereo: frames of 4 bytes after the 44-byte header
	double sum = 0;
	for (size_t i = from; i < to; i++)
	{
		const unsigned char* p = (const unsigned char*)w.data() + 44 + i * 4;
		for (int c = 0; c < 2; c++) sum += (double)(int16_t)(p[c * 2] | p[c * 2 + 1] << 8) * (int16_t)(p[c * 2] | p[c * 2 + 1] << 8);
	}
	return std::sqrt(sum / ((to - from) * 2));
}

static void testSoundFx()
{
	// names and keys round-trip
	CHECK_EQ(std::string(soundFxName(SoundFx::Hall)), std::string("Concert hall"));
	CHECK_EQ(std::string(soundFxName(SoundFx::Wide)), std::string("Wide stereo"));
	for (int i = 0; i < SOUNDFX_COUNT; i++) CHECK_EQ(soundFxFromKey(soundFxKey((SoundFx)i)) == (SoundFx)i, true);
	CHECK_EQ(soundFxFromKey("nonsense") == SoundFx::None, true);
	CHECK_EQ(std::string(soundFxKey(SoundFx::Headphones)), std::string("headphones"));

	// the impulse responses: a RIFF header that says what it holds, the same bytes every time
	for (SoundFx fx : {SoundFx::Room, SoundFx::Hall, SoundFx::Church, SoundFx::Cave})
	{
		std::string w = soundFxIrWav(fx);
		CHECK_EQ(w.size() > 44, true);
		CHECK_EQ(w.size() < 1000000, true);
		CHECK_EQ(w.substr(0, 4), std::string("RIFF"));
		CHECK_EQ(w.substr(8, 8), std::string("WAVEfmt "));
		auto u32 = [&](size_t at) { return (uint32_t)((unsigned char)w[at] | (unsigned char)w[at + 1] << 8 | (unsigned char)w[at + 2] << 16 | (uint32_t)(unsigned char)w[at + 3] << 24); };
		CHECK_EQ(u32(4), (uint32_t)(w.size() - 8));
		CHECK_EQ(u32(24), (uint32_t)48000);
		CHECK_EQ(u32(40), (uint32_t)(w.size() - 44));
		CHECK_EQ(w == soundFxIrWav(fx), true);
		// it dies away: the last tenth far below the first
		size_t frames = (w.size() - 44) / 4;
		double first = wavRms(w, 0, frames / 10), last = wavRms(w, frames - frames / 10, frames);
		CHECK_EQ(last * 30 < first, true);
	}
	CHECK_EQ(soundFxIrWav(SoundFx::Room) == soundFxIrWav(SoundFx::Hall), false);
	CHECK_EQ(soundFxIrWav(SoundFx::Room).size() < soundFxIrWav(SoundFx::Hall).size(), true);
	for (SoundFx fx : {SoundFx::None, SoundFx::Wide, SoundFx::Headphones}) CHECK_EQ(soundFxIrWav(fx), std::string(""));

	// the filters
	CHECK_EQ(soundFxFilter(SoundFx::None, "/x/ir.wav"), std::string(""));
	CHECK_EQ(soundFxFilter(SoundFx::Headphones, ""), std::string("@bpfx:lavfi=[crossfeed]"));
	CHECK_EQ(soundFxFilter(SoundFx::Wide, ""), std::string("@bpfx:lavfi=[volume=-2.5dB,extrastereo=m=1.6:c=0,alimiter=limit=0.98:level=0]"));
	CHECK_EQ(soundFxFilter(SoundFx::Hall, "/s/ir-hall.wav"),
		std::string("@bpfx:lavfi=[amovie=/s/ir-hall.wav[ir];[in][ir]afir=minp=1024:dry=7.3:wet=7.3,alimiter=limit=0.98:level=0]"));
	CHECK_EQ(soundFxFilter(SoundFx::Room, "/s/ir-room.wav"),
		std::string("@bpfx:lavfi=[amovie=/s/ir-room.wav[ir];[in][ir]afir=minp=1024:dry=4.5:wet=4.5,alimiter=limit=0.98:level=0]"));
	// a path with the graph's own characters, escaped as an option's value and then in the graph
	CHECK_EQ(soundFxFilter(SoundFx::Cave, "/a b:c,d[e];f'g/ir.wav"),
		std::string("@bpfx:lavfi=[amovie=/a b\\\\:c\\,d\\[e\\]\\;f\\\\\\'g/ir.wav[ir];[in][ir]afir=minp=1024:dry=7.6:wet=7.6,alimiter=limit=0.98:level=0]"));

	// the setting: written, read back, none when absent or unknown
	Settings s;
	CHECK_EQ(s.soundFx == SoundFx::None, true);
	s.soundFx = SoundFx::Church;
	CHECK_EQ(serializeSettings(s).find("sound-effect = church\n") != std::string::npos, true);
	CHECK_EQ(parseSettings(serializeSettings(s)).soundFx == SoundFx::Church, true);
	CHECK_EQ(parseSettings("eq = on\n").soundFx == SoundFx::None, true);
	CHECK_EQ(parseSettings("sound-effect = bogus\n").soundFx == SoundFx::None, true);
}

static std::vector<PlaylistEntry> plv(const char* names)
{
	std::vector<PlaylistEntry> v;
	for (const char* p = names; *p; p++)
	{
		PlaylistEntry e;
		e.path = std::string("/m/") + *p + ".mp4";
		e.title = std::string(1, *p);
		v.push_back(e);
	}
	return v;
}

static std::string plj(const std::vector<PlaylistEntry>& v)
{
	std::string s;
	for (const auto& e : v) s += e.title;
	return s;
}

static std::string plr(const std::vector<int>& r)
{
	std::string s;
	for (int i : r) s += std::to_string(i) + ",";
	return s;
}

static void testLiked(PlaylistStore& st, const std::string& dir)
{
	using namespace bp;
	CHECK_EQ(st.likedFile(), dir + "/.liked.m3u");
	CHECK_EQ(st.isBuiltinFile(st.likedFile()), true);
	CHECK_EQ(st.isBuiltinFile(dir + "/Extra.m3u8"), false);
	auto li = st.list();
	CHECK_EQ(li[0].builtin, true);
	CHECK_EQ(li[0].name, std::string("Liked"));
	for (size_t i = 1; i < li.size(); i++) CHECK_EQ(li[i].builtin, false);
	CHECK_EQ(st.addTargets().size(), li.size() - 1);
	for (const auto& i : st.addTargets()) CHECK_EQ(i.file != st.likedFile(), true);
	// a user's own "Liked" is refused (the built-in holds the name), and can't land on the reserved file
	std::string f;
	CHECK_EQ(st.create("liked", f) == PlNameError::Taken, true);
	// ...but one that already exists as a file coexists with it
	writeFileAtomic(dir + "/Liked.m3u", "#EXTM3U\n/m/own.mp4\n");
	CHECK_EQ(st.isLiked("/m/own.mp4"), false);
	CHECK_EQ(st.list().size(), li.size() + 1);

	CHECK_EQ(st.isLiked("/m/a.mp3"), false);
	CHECK_EQ(st.setLiked("/m/a.mp3", "A", 10, true), true);
	CHECK_EQ(st.setLiked("/m/b.mkv", "", -1, true), true);
	CHECK_EQ(st.setLiked("/m/a.mp3", "A", 10, true), true);	// twice: nothing
	CHECK_EQ(st.isLiked("/m/a.mp3"), true);
	CHECK_EQ(st.likedCount(), 2);
	CHECK_EQ(st.list()[0].count, 2);
	Playlist pl;
	int skp;
	CHECK_EQ(st.load(st.likedFile(), pl, skp), true);
	CHECK_EQ(pl.entries.size(), size_t(2));
	CHECK_EQ(pl.entries[0].path, std::string("/m/b.mkv"));	// newest first
	CHECK_EQ(pl.entries[0].title, std::string("b"));
	CHECK_EQ(pl.entries[1].path, std::string("/m/a.mp3"));
	CHECK_EQ(pl.entries[1].title, std::string("A"));
	CHECK_EQ(st.containsPath(st.likedFile(), "/m/a.mp3"), true);
	CHECK_EQ(st.containsPath(st.likedFile(), "/m/zz.mp3"), false);
	// photos, non-media, relative paths
	CHECK_EQ(st.setLiked("/m/p.jpg", "", -1, true), false);
	CHECK_EQ(st.setLiked("/m/notes.txt", "", -1, true), false);
	CHECK_EQ(st.setLiked("rel/a.mp3", "", -1, true), false);
	CHECK_EQ(st.setLiked("", "", -1, true), false);
	CHECK_EQ(st.isLiked("/m/p.jpg"), false);
	// network
	CHECK_EQ(st.setLiked("bpnet://srv/Music/s.flac", "S", -1, true), true);
	CHECK_EQ(st.isLiked("bpnet://srv/Music/s.flac"), true);
	CHECK_EQ(st.setLiked("bpnet://srv/Music/s.flac", "", -1, false), true);
	CHECK_EQ(st.isLiked("bpnet://srv/Music/s.flac"), false);
	// unlike removes every copy (a hand-edited file may hold two)
	Playlist dup;
	dup.file = st.likedFile();
	dup.name = "Liked";
	dup.entries = {{"/m/d.mp3", "d", -1}, {"/m/e.mp3", "e", -1}, {"/m/d.mp3", "d", -1}};
	CHECK_EQ(st.save(dup), true);
	CHECK_EQ(st.isLiked("/m/d.mp3"), true);	// the cache followed the save
	CHECK_EQ(st.setLiked("/m/d.mp3", "", -1, false), true);
	CHECK_EQ(st.isLiked("/m/d.mp3"), false);
	CHECK_EQ(st.likedCount(), 1);
	CHECK_EQ(st.setLiked("/m/never.mp3", "", -1, false), true);	// unliking what isn't liked: nothing
	// at: back in its place (an Undo), clamped to the end; nothing fell off
	std::string none2 = "x";
	CHECK_EQ(st.setLiked("/m/u.mp3", "", -1, true, 1, &none2), true);
	CHECK_EQ(none2, std::string());
	CHECK_EQ(st.setLiked("/m/v.mp3", "", -1, true, 99), true);
	CHECK_EQ(st.load(st.likedFile(), pl, skp), true);
	CHECK_EQ(pl.entries.size(), size_t(3));
	CHECK_EQ(pl.entries[0].path, std::string("/m/e.mp3"));
	CHECK_EQ(pl.entries[1].path, std::string("/m/u.mp3"));
	CHECK_EQ(pl.entries[2].path, std::string("/m/v.mp3"));
	st.setLiked("/m/u.mp3", "", -1, false);
	st.setLiked("/m/v.mp3", "", -1, false);
	// can't be renamed or deleted
	std::string nf;
	CHECK_EQ(st.rename(st.likedFile(), "Faves", nf) == PlNameError::Builtin, true);
	CHECK_EQ(st.remove(st.likedFile()), false);
	CHECK_EQ(access(st.likedFile().c_str(), F_OK) == 0, true);
	// the cap: the oldest fall off
	dup.entries.clear();
	for (size_t i = 0; i < PL_MAX_ENTRIES; i++) dup.entries.push_back({"/m/s" + std::to_string(i) + ".mp3", "", -1});
	CHECK_EQ(st.save(dup), true);
	std::string fell;
	CHECK_EQ(st.setLiked("/m/new.mp3", "", -1, true, 0, &fell), true);
	CHECK_EQ(fell, "/m/s" + std::to_string(PL_MAX_ENTRIES - 1) + ".mp3");	// said, for the web
	CHECK_EQ(st.likedCount(), int(PL_MAX_ENTRIES));
	CHECK_EQ(st.isLiked("/m/new.mp3"), true);
	CHECK_EQ(st.isLiked("/m/s" + std::to_string(PL_MAX_ENTRIES - 1) + ".mp3"), false);
	// a fresh store reads the same file; with no folder, nothing
	PlaylistStore again(dir);
	CHECK_EQ(again.isLiked("/m/new.mp3"), true);
	PlaylistStore none("");
	CHECK_EQ(none.list().size(), size_t(0));
	CHECK_EQ(none.setLiked("/m/a.mp3", "", -1, true), false);
	CHECK_EQ(none.isLiked("/m/a.mp3"), false);
}

static void testPlaylists()
{
	// round trip, quoted commas
	Playlist p;
	p.name = "Road trip";
	p.entries = {{"/m/a.mp4", "A, \"quoted, comma\"", 61}, {"bpnet://ab12cd34/Music/b.flac", "b", -1}, {"/m/a.mp4", "again", 3}};
	Playlist q;
	int sk = 0;
	CHECK_EQ(parseM3u(serializeM3u(p), "/x", q, sk), true);
	CHECK_EQ(sk, 0);
	CHECK_EQ(q.name, std::string("Road trip"));
	CHECK_EQ(q.entries.size(), size_t(3));
	CHECK_EQ(q.entries[0].title, std::string("A, \"quoted, comma\""));
	CHECK_EQ(q.entries[0].duration, 61.0);
	CHECK_EQ(q.entries[1].path, std::string("bpnet://ab12cd34/Music/b.flac"));
	CHECK_EQ(q.entries[1].duration, -1.0);
	CHECK_EQ(q.entries[2].path, std::string("/m/a.mp4"));	// duplicates kept
	CHECK_EQ(serializeM3u(q), serializeM3u(p));

	// attribute-style EXTINF with a quoted comma
	Playlist at;
	parseM3u("#EXTINF:10 tvg-name=\"x,y\",Real title\n/m/a.mp4\n", "", at, sk);
	CHECK_EQ(at.entries[0].title, std::string("Real title"));

	// CRLF + BOM; comments and blanks; orphan #EXTINF applies to the next path only
	Playlist c;
	parseM3u("\xEF\xBB\xBF#EXTM3U\r\n#PLAYLIST:Crlf\r\n\r\n# a comment\r\n#EXTINF:5,Five\r\n/m/a.mp4\r\n/m/b.mp4\r\n", "", c, sk);
	CHECK_EQ(c.name, std::string("Crlf"));
	CHECK_EQ(c.entries.size(), size_t(2));
	CHECK_EQ(c.entries[0].title, std::string("Five"));
	CHECK_EQ(c.entries[1].title, std::string("b"));
	CHECK_EQ(c.entries[1].duration, -1.0);
	Playlist o;
	parseM3u("#EXTINF:9,Orphan\n#EXTINF:7,Real\n/m/a.mp4\n/m/b.mp4\n", "", o, sk);
	CHECK_EQ(o.entries[0].title, std::string("Real"));
	CHECK_EQ(o.entries[1].title, std::string("b"));
	CHECK_EQ(o.entries[1].duration, -1.0);

	// relative, file://, stem as the name
	Playlist r;
	r.file = "/state/playlists/Weekend.m3u";
	parseM3u("sub/c.mkv\n/abs/d.mp3\nfile:///sd/My%20Movies/e.mp4\nfile://localhost/sd/f.mp4\n", "/state/playlists", r, sk);
	CHECK_EQ(r.name, std::string("Weekend"));
	CHECK_EQ(r.entries.size(), size_t(4));
	CHECK_EQ(r.entries[0].path, std::string("/state/playlists/sub/c.mkv"));
	CHECK_EQ(r.entries[2].path, std::string("/sd/My Movies/e.mp4"));
	CHECK_EQ(r.entries[3].path, std::string("/sd/f.mp4"));
	CHECK_EQ(sk, 0);

	// photos, .txt, http:// skipped and counted
	Playlist s;
	parseM3u("/m/a.jpg\n/m/b.txt\nhttp://x/y.mp4\n/m/ok.mp4\nbpnet://id/p.png\n", "", s, sk);
	CHECK_EQ(s.entries.size(), size_t(1));
	CHECK_EQ(sk, 4);

	// limits
	std::string big;
	for (int i = 0; i < 2005; i++) big += "/m/" + std::to_string(i) + ".mp4\n";
	Playlist capd;
	parseM3u(big, "", capd, sk);
	CHECK_EQ(capd.entries.size(), PL_MAX_ENTRIES);
	CHECK_EQ(sk, 5);
	CHECK_EQ(parseM3u(std::string(PL_MAX_FILE_BYTES + 1, '#'), "", capd, sk), false);
	Playlist longl;
	parseM3u("/" + std::string(5000, 'x') + ".mp4\n/m/a.mp4\n", "", longl, sk);
	CHECK_EQ(longl.entries.size(), size_t(1));
	CHECK_EQ(sk, 1);

	// file names
	CHECK_EQ(playlistFileName("Road trip"), std::string("Road trip.m3u"));
	CHECK_EQ(playlistFileName("a/b"), std::string("a_b.m3u"));
	CHECK_EQ(playlistFileName("..x"), std::string("x.m3u"));
	CHECK_EQ(playlistFileName("x. ."), std::string("x.m3u"));
	CHECK_EQ(playlistFileName("a:b*c?\"d<e>f|g\\h"), std::string("a_b_c__d_e_f_g_h.m3u"));
	CHECK_EQ(playlistFileName(""), std::string("Playlist.m3u"));
	CHECK_EQ(playlistFileName(" ... "), std::string("Playlist.m3u"));
	std::string u8;
	for (int i = 0; i < 30; i++) u8 += "\xC3\xA9";	// 60 bytes
	CHECK_EQ(playlistFileName(u8 + "\xC3\xA9"), u8 + ".m3u");
	CHECK_EQ(playlistFileName("a" + u8), "a" + u8.substr(0, 58) + ".m3u");	// cut before a half character

	// name checks
	std::vector<std::string> have = {"Road trip", "Gym"};
	CHECK_EQ(checkPlaylistName("", have) == PlNameError::Empty, true);
	CHECK_EQ(checkPlaylistName("  ", have) == PlNameError::Empty, true);
	CHECK_EQ(checkPlaylistName(std::string(41, 'a'), have) == PlNameError::TooLong, true);
	CHECK_EQ(checkPlaylistName(std::string(40, 'a'), have) == PlNameError::None, true);
	CHECK_EQ(checkPlaylistName("a/b", have) == PlNameError::BadChar, true);
	CHECK_EQ(checkPlaylistName("a\tb", have) == PlNameError::BadChar, true);
	CHECK_EQ(checkPlaylistName("road TRIP", have) == PlNameError::Taken, true);
	CHECK_EQ(checkPlaylistName("road TRIP", have, "Road trip") == PlNameError::None, true);
	CHECK_EQ(checkPlaylistName("Gym", have, "Road trip") == PlNameError::Taken, true);
	CHECK_EQ(checkPlaylistName("Road trip", have, "Road trip") == PlNameError::None, true);

	// moves
	auto v = plv("abcdef");
	auto rm = moveSelection(v, {0, 1}, -1);	// a block at the top stays
	CHECK_EQ(plj(v), std::string("abcdef"));
	CHECK_EQ(plr(rm), std::string("0,1,2,3,4,5,"));
	v = plv("abcdef");
	rm = moveSelection(v, {0, 3}, -1);	// split: the top one stays, the other moves
	CHECK_EQ(plj(v), std::string("abdcef"));
	CHECK_EQ(plr(rm), std::string("0,1,3,2,4,5,"));
	v = plv("abcdef");
	rm = moveSelection(v, {1, 2, 4}, -1);
	CHECK_EQ(plj(v), std::string("bcaedf"));
	v = plv("abcdef");
	rm = moveSelection(v, {1, 2, 4}, 1);
	CHECK_EQ(plj(v), std::string("adbcfe"));
	CHECK_EQ(plr(rm), std::string("0,2,3,1,5,4,"));
	CHECK_EQ(remapSelection({1, 2, 4}, rm) == std::set<int>({2, 3, 5}), true);
	v = plv("abcdef");
	moveSelection(v, {4, 5}, 1);
	CHECK_EQ(plj(v), std::string("abcdef"));

	// gather
	v = plv("abcdef");
	rm = gatherSelection(v, {1, 3, 5});
	CHECK_EQ(plj(v), std::string("abdfce"));
	CHECK_EQ(plr(rm), std::string("0,1,4,2,5,3,"));
	v = plv("abcdef");
	gatherSelection(v, {4, 0});
	CHECK_EQ(plj(v), std::string("aebcdf"));

	// remove + remapIndex
	v = plv("abcdef");
	rm = removeEntries(v, {1, 2, 5});
	CHECK_EQ(plj(v), std::string("ade"));
	CHECK_EQ(plr(rm), std::string("0,-1,-1,1,2,-1,"));
	CHECK_EQ(remapIndex(3, rm), 1);
	CHECK_EQ(remapIndex(1, rm), 1);	// the next survivor
	CHECK_EQ(remapIndex(5, rm), 2);	// none after: the last
	CHECK_EQ(remapIndex(0, rm), 0);
	CHECK_EQ(remapIndex(0, std::vector<int>{-1, -1}), -1);

	// add, cap and dropped
	v = plv("abc");
	int dropped = 0;
	rm = addEntries(v, plv("xy"), 1, 100, dropped);
	CHECK_EQ(plj(v), std::string("axybc"));
	CHECK_EQ(plr(rm), std::string("0,3,4,"));
	CHECK_EQ(dropped, 0);
	rm = addEntries(v, plv("123"), -1, 7, dropped);
	CHECK_EQ(plj(v), std::string("axybc12"));
	CHECK_EQ(dropped, 1);
	rm = addEntries(v, plv("9"), -1, 7, dropped);
	CHECK_EQ(plj(v), std::string("axybc12"));
	CHECK_EQ(dropped, 1);

	// duplicates
	v = plv("abacbd");
	int removed = 0;
	rm = removeDuplicates(v, removed);
	CHECK_EQ(plj(v), std::string("abcd"));
	CHECK_EQ(removed, 2);
	CHECK_EQ(plr(rm), std::string("0,1,-1,2,-1,3,"));
	rm = removeDuplicates(v, removed);
	CHECK_EQ(removed, 0);

	// shuffle: a permutation, `first` first, the same for a seed
	for (uint64_t seed : {1ull, 2ull, 99ull})
	{
		auto sh = shuffleOrder(20, 7, seed);
		CHECK_EQ(sh.size(), size_t(20));
		CHECK_EQ(sh[0], 7);
		std::set<int> uniq(sh.begin(), sh.end());
		CHECK_EQ(uniq.size(), size_t(20));
		CHECK_EQ(*uniq.begin(), 0);
		CHECK_EQ(*uniq.rbegin(), 19);
		CHECK_EQ(shuffleOrder(20, 7, seed) == sh, true);
	}
	CHECK_EQ(shuffleOrder(20, 7, 1) == shuffleOrder(20, 7, 2), false);
	CHECK_EQ(shuffleOrder(1, 0, 5).size(), size_t(1));
	CHECK_EQ(shuffleOrder(0, 0, 5).size(), size_t(0));
	CHECK_EQ(shuffleOrder(5, -1, 5).size(), size_t(5));

	// folder entries
	int photos = 0;
	auto fe = folderEntries("/sd/Movies", {"10.mp4", ".hidden.mp4", "2.mp4", "a.jpg", "b.PNG", "n.txt", "song.mp3", "sub"}, photos);
	CHECK_EQ(photos, 2);
	CHECK_EQ(fe.size(), size_t(3));
	CHECK_EQ(fe[0].path, std::string("/sd/Movies/2.mp4"));
	CHECK_EQ(fe[1].path, std::string("/sd/Movies/10.mp4"));
	CHECK_EQ(fe[2].title, std::string("song"));
	auto fn = folderEntries(makeNetPath("ab12cd34", "/Media"), {"x.mkv"}, photos);
	CHECK_EQ(fn[0].path, netJoin(makeNetPath("ab12cd34", "/Media"), "x.mkv"));
	CHECK_EQ(entryTitle("/a/b/My Film.v2.mkv"), std::string("My Film.v2"));
	CHECK_EQ(entryTitle(fn[0].path), std::string("x"));

	// the store, on a temp dir
	char tmpl[] = "/tmp/bp-plstore-XXXXXX";
	std::string root = mkdtemp(tmpl);
	std::string dir = root + "/playlists";
	{
		PlaylistStore st(dir);
		CHECK_EQ(st.list().size(), size_t(1));	// Liked, always
		int ser = st.serial();
		std::string f1, f2, f3, fx;
		CHECK_EQ(st.create("beta 10", f1) == PlNameError::None, true);
		CHECK_EQ(st.create("Beta 2", f2) == PlNameError::None, true);
		CHECK_EQ(st.create("BETA 2", fx) == PlNameError::Taken, true);
		CHECK_EQ(st.create("beta/2", fx) == PlNameError::BadChar, true);
		CHECK_EQ(st.create("a:b", f3) == PlNameError::None, true);
		std::string f4;
		CHECK_EQ(st.create("a_b", f4) == PlNameError::None, true);	// same file name as "a:b": " (2)"
		CHECK_EQ(f4 != f3, true);
		CHECK_EQ(st.serial() > ser, true);
		auto li = st.list();
		CHECK_EQ(li.size(), size_t(5));
		CHECK_EQ(li[0].builtin, true);
		CHECK_EQ(li[1].name, std::string("a:b"));
		CHECK_EQ(li[2].name, std::string("a_b"));
		CHECK_EQ(li[3].name, std::string("Beta 2"));	// natural order: 2 before 10
		CHECK_EQ(li[4].name, std::string("beta 10"));
		CHECK_EQ(li[1].count, 0);

		Playlist pl;
		int skp;
		CHECK_EQ(st.load(f2, pl, skp), true);
		pl.entries = plv("abc");
		CHECK_EQ(st.save(pl), true);
		CHECK_EQ(st.list()[3].count, 3);
		Playlist back;
		CHECK_EQ(st.load(f2, back, skp), true);
		CHECK_EQ(back.entries.size(), size_t(3));
		CHECK_EQ(back.name, std::string("Beta 2"));

		std::string nf;
		CHECK_EQ(st.rename(f2, "beta 10", nf) == PlNameError::Taken, true);
		CHECK_EQ(st.rename(f2, "BETA 2", nf) == PlNameError::None, true);	// itself, new case
		CHECK_EQ(st.rename(nf, "Gamma", nf) == PlNameError::None, true);
		CHECK_EQ(access(f2.c_str(), F_OK) != 0, true);	// the old file is gone
		CHECK_EQ(access(nf.c_str(), F_OK) == 0, true);
		CHECK_EQ(nf, dir + "/Gamma.m3u");
		CHECK_EQ(st.load(nf, back, skp), true);
		CHECK_EQ(back.entries.size(), size_t(3));
		CHECK_EQ(back.name, std::string("Gamma"));
		CHECK_EQ(st.list().size(), size_t(5));

		// a stray .m3u8 and a non-playlist
		writeFileAtomic(dir + "/Extra.m3u8", "#EXTM3U\n/m/a.mp4\n");
		writeFileAtomic(dir + "/notes.txt", "x");
		auto l2 = st.list();
		CHECK_EQ(l2.size(), size_t(6));
		bool seen = false;
		for (const auto& i : l2)
			if (i.name == "Extra" && i.count == 1) seen = true;
		CHECK_EQ(seen, true);

		int ser2 = st.serial();
		CHECK_EQ(st.remove(nf), true);
		CHECK_EQ(st.serial() > ser2, true);
		CHECK_EQ(st.list().size(), size_t(5));
		CHECK_EQ(st.remove(nf), false);
		testLiked(st, dir);
	}
	{
		std::string cmd = "rm -rf '" + root + "'";
		if (system(cmd.c_str()) != 0) fprintf(stderr, "could not remove %s\n", root.c_str());
	}

	// 1000 lines, timed
	Playlist bigp;
	bigp.name = "Big";
	for (int i = 0; i < 1000; i++)
		bigp.entries.push_back({"/media/cards/Movies/Some Folder/File number " + std::to_string(i) + ".mkv", "File number " + std::to_string(i), 120.0 + i});
	auto t0 = std::chrono::steady_clock::now();
	std::string text = serializeM3u(bigp);
	Playlist bp2;
	parseM3u(text, "/x", bp2, sk);
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	CHECK_EQ(bp2.entries.size(), size_t(1000));
	CHECK_EQ(ms < 50, true);
	printf("playlists: parse+serialize of 1000 entries took %.2f ms\n", ms);
}


static void testWeb()
{
	// PIN alphabet, uniformity, injected rng
	CHECK_EQ(std::string(WEB_PIN_ALPHABET), std::string("34679ACDEFHJKMNPRTWXY"));
	CHECK_EQ(strlen(WEB_PIN_ALPHABET), size_t(21));
	{
		uint32_t n = 0;
		CHECK_EQ(makePin([&] { return n++; }), std::string("3467"));
		CHECK_EQ(makePin([&] { return uint32_t(20); }), std::string("YYYY"));
		// values at or past the largest multiple of 21 are skipped, not folded onto the first characters
		const uint32_t limit = uint32_t(0x100000000ull - (0x100000000ull % 21));
		std::vector<uint32_t> seq = {0xFFFFFFFFu, limit, limit - 1, 0, 1, 2};
		size_t k = 0;
		CHECK_EQ(makePin([&] { return seq[k++]; }), std::string("Y346"));
		std::map<char, int> hist;
		uint64_t x = 88172645463325252ull;
		auto xr = [&] { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return uint32_t(x >> 16); };
		for (int i = 0; i < 21000; i++)
			for (char c : makePin(xr)) hist[c]++;
		CHECK_EQ(hist.size(), size_t(21));
		for (auto& kv : hist) CHECK_EQ(kv.second > 3600 && kv.second < 4400, true);	// 4000 each
		for (int i = 0; i < 50; i++) CHECK_EQ(validPin(makePin()), true);
	}
	CHECK_EQ(validPin("34AY"), true);
	CHECK_EQ(validPin("34ay"), true);
	CHECK_EQ(validPin("34A"), false);
	CHECK_EQ(validPin("34AY3"), false);
	CHECK_EQ(validPin("0OIL"), false);
	CHECK_EQ(validPin("34A5"), false);
	CHECK_EQ(validPin(""), false);
	CHECK_EQ(validPin(std::string("34A\0", 4)), false);
	CHECK_EQ(pinMatches("34ay", "34AY"), true);
	CHECK_EQ(pinMatches("34AY", "34ay"), true);
	CHECK_EQ(pinMatches("34AX", "34AY"), false);
	CHECK_EQ(pinMatches("34A", "34AY"), false);
	CHECK_EQ(pinMatches("34AYY", "34AY"), false);
	CHECK_EQ(pinMatches("", "34AY"), false);
	CHECK_EQ(pinMatches("", ""), true);

	// PinGate
	{
		PinGate g;
		int64_t t = 1000000;
		PinResult r = g.attempt("1.1.1.1", true, t);
		CHECK_EQ(r.ok, true);
		CHECK_EQ(r.left, 5);
		for (int i = 1; i <= 4; i++)
		{
			r = g.attempt("1.1.1.1", false, t += 1000);
			CHECK_EQ(r.ok, false);
			CHECK_EQ(r.left, 5 - i);
			CHECK_EQ(r.retryAfterS, 0);
		}
		r = g.attempt("1.1.1.1", false, t += 1000);	// the 5th
		int64_t lockT = t;
		CHECK_EQ(r.left, 0);
		CHECK_EQ(r.retryAfterS, 60);
		CHECK_EQ(g.lockedFor("1.1.1.1", t), 60);
		CHECK_EQ(g.lockedFor("1.1.1.1", t + 59001), 1);
		CHECK_EQ(g.lockedFor("1.1.1.1", t + 60000), 0);
		CHECK_EQ(g.lockedFor("2.2.2.2", t), 0);
		// locked: the right PIN is refused too, and nothing is counted
		r = g.attempt("1.1.1.1", true, t + 10000);
		CHECK_EQ(r.ok, false);
		CHECK_EQ(r.retryAfterS, 50);
		// another address is untouched
		r = g.attempt("2.2.2.2", false, t);
		CHECK_EQ(r.left, 4);
		// after it: 5 more, and the next lockout doubles
		t = lockT + 60000;
		for (int i = 0; i < 4; i++) g.attempt("1.1.1.1", false, t += 100);
		r = g.attempt("1.1.1.1", false, t += 100);
		CHECK_EQ(r.retryAfterS, 120);
		t += 120000;
		for (int i = 0; i < 4; i++) g.attempt("1.1.1.1", false, t += 100);
		CHECK_EQ(g.attempt("1.1.1.1", false, t += 100).retryAfterS, 240);
		// up to 15 minutes
		int last = 0;
		for (int round = 0; round < 6; round++)
		{
			t += 16 * 60 * 1000;
			for (int i = 0; i < 5; i++) last = g.attempt("1.1.1.1", false, t += 100).retryAfterS;
		}
		CHECK_EQ(last, 900);
		// a success resets that address
		t += 16 * 60 * 1000;
		g.attempt("1.1.1.1", false, t);
		CHECK_EQ(g.attempt("1.1.1.1", true, t + 1).ok, true);
		for (int i = 0; i < 4; i++) g.attempt("1.1.1.1", false, t += 100);
		CHECK_EQ(g.attempt("1.1.1.1", false, t += 100).retryAfterS, 60);
		// reset() forgets the lockouts
		g.reset();
		CHECK_EQ(g.lockedFor("1.1.1.1", t), 0);
		CHECK_EQ(g.attempt("1.1.1.1", false, t).left, 4);
	}
	{
		// 30 failures from many addresses within 10 minutes: rotate once
		PinGate g;
		int64_t t = 0;
		int rotates = 0, at = -1;
		for (int i = 0; i < 29; i++)
		{
			PinResult r = g.attempt("10.0.0." + std::to_string(i), false, t += 1000);
			if (r.rotate) { rotates++; at = i; }
		}
		CHECK_EQ(rotates, 0);
		PinResult r = g.attempt("10.0.1.1", false, t += 1000);
		CHECK_EQ(r.rotate, true);
		CHECK_EQ(at, -1);
		// the window starts again
		for (int i = 0; i < 29; i++) CHECK_EQ(g.attempt("10.0.2." + std::to_string(i), false, t += 1000).rotate, false);
		CHECK_EQ(g.attempt("10.0.3.1", false, t += 1000).rotate, true);
		// spread over more than 10 minutes: no rotation
		PinGate h;
		t = 0;
		bool any = false;
		for (int i = 0; i < 100; i++) any = h.attempt("10.1.0." + std::to_string(i), false, t += 30000).rotate || any;
		CHECK_EQ(any, false);
		// one address that locks out doesn't count attempts it's refused
		PinGate k;
		t = 0;
		for (int i = 0; i < 100; i++) any = k.attempt("9.9.9.9", false, t += 10).rotate || any;
		CHECK_EQ(any, false);
	}

	// tokens, sessions
	{
		uint32_t n = 0;
		std::string tk = newToken([&] { return n++; });
		CHECK_EQ(tk.size(), size_t(64));
		CHECK_EQ(tk.substr(0, 16), std::string("0000000001000000"));
		std::string t1 = newToken(), t2 = newToken();
		CHECK_EQ(t1.size(), size_t(64));
		CHECK_EQ(t1 != t2, true);
		CHECK_EQ(t1.find_first_not_of("0123456789abcdef"), std::string::npos);
		CHECK_EQ(hashToken("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
		CHECK_EQ(hashToken(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

		SessionStore s;
		std::string h1 = hashToken("one"), h2 = hashToken("two");
		s.add(h1, 1000 + WEB_SESSION_S);
		s.add(h2, 500);
		CHECK_EQ(s.size(), size_t(2));
		bool renew = true;
		CHECK_EQ(s.check(h1, 1000, renew), true);
		CHECK_EQ(renew, false);
		CHECK_EQ(s.check(h1, 1000 + WEB_SESSION_S / 2 + 1, renew), true);
		CHECK_EQ(renew, true);
		CHECK_EQ(s.check(h1, 1000 + WEB_SESSION_S, renew), false);	// expired exactly now
		CHECK_EQ(s.check(h2, 500, renew), false);
		CHECK_EQ(s.check(h2, 499, renew), true);
		CHECK_EQ(s.check("nope", 0, renew), false);
		CHECK_EQ(renew, false);
		s.renew(h2, 9000);
		CHECK_EQ(s.check(h2, 8000, renew), true);
		s.renew("unknown", 9000);
		CHECK_EQ(s.size(), size_t(2));
		std::string text = s.serialize();
		SessionStore s2;
		s2.add(hashToken("stale"), 1);
		s2.parse(text);
		CHECK_EQ(s2.size(), size_t(2));
		CHECK_EQ(s2.serialize(), text);
		CHECK_EQ(s2.check(h1, 1000, renew), true);
		s2.parse("garbage\n" + h1 + "\t12x\n" + h1 + "\t77\nshort\t5\n\n" + h2 + "\n");
		CHECK_EQ(s2.size(), size_t(1));
		CHECK_EQ(s2.serialize(), h1 + "\t77\n");
		s2.remove(h1);
		CHECK_EQ(s2.size(), size_t(0));
		s.clear();
		CHECK_EQ(s.size(), size_t(0));
		CHECK_EQ(s.serialize(), std::string());
	}

	// cookies
	CHECK_EQ(cookieValue("bp_s=abc", "bp_s"), std::string("abc"));
	CHECK_EQ(cookieValue("a=1; bp_s=abc; b=2", "bp_s"), std::string("abc"));
	CHECK_EQ(cookieValue("a=1;bp_s=abc;b=2", "bp_s"), std::string("abc"));
	CHECK_EQ(cookieValue("a=1;   bp_s=abc  ", "bp_s"), std::string("abc"));
	CHECK_EQ(cookieValue("xbp_s=zzz; bp_s=abc", "bp_s"), std::string("abc"));
	CHECK_EQ(cookieValue("bp_s2=zzz", "bp_s"), std::string(""));
	CHECK_EQ(cookieValue("bp_s=", "bp_s"), std::string(""));
	CHECK_EQ(cookieValue("bp_s", "bp_s"), std::string(""));
	CHECK_EQ(cookieValue("", "bp_s"), std::string(""));
	CHECK_EQ(cookieValue("a=1; b=2", "bp_s"), std::string(""));
	CHECK_EQ(cookieValue("bp_s=first; bp_s=second", "bp_s"), std::string("first"));
	CHECK_EQ(cookieValue("a=b=c", "a"), std::string("b=c"));

	// resolveVirtual on a real tree
	{
		char tpl[] = "/tmp/bp-web-XXXXXX";
		char* made = mkdtemp(tpl);
		CHECK_EQ(made != nullptr, true);
		std::string base = made ? made : "/tmp/bp-web-none";
		auto sh = [](const std::string& c) { return system(c.c_str()); };
		sh("mkdir -p '" + base + "/card/Music/sub' '" + base + "/card2' '" + base + "/outside' '" + base + "/card/state' '" +
			base + "/card/.hidden' '" + base + "/cardx'");
		sh("echo a > '" + base + "/card/Music/a.mp3'; echo s > '" + base + "/outside/secret.mp3'; echo x > '" + base +
			"/card/state/web.conf'; echo h > '" + base + "/card/.hidden/h.mp3'; echo b > '" + base + "/cardx/b.mp3'");
		sh("ln -s '" + base + "/outside' '" + base + "/card/Music/escape'; ln -s '" + base + "/card/Music/a.mp3' '" + base +
			"/card/Music/inlink.mp3'; ln -s '" + base + "/outside/secret.mp3' '" + base + "/card/Music/leak.mp3'; ln -s '" +
			base + "/nowhere' '" + base + "/card/Music/dangling'; ln -s '" + base + "/cardx' '" + base + "/card/sib'");
		std::string card, card2;
		{
			char b[PATH_MAX];
			card = realpath((base + "/card").c_str(), b) ? b : "";
			card2 = realpath((base + "/card2").c_str(), b) ? b : "";
		}
		std::vector<WebRoot> roots = {{"This card", base + "/card"}, {"Second", base + "/card2"}};
		std::vector<std::string> deny = {base + "/card/state", "/data/home"};
		std::string real;
		auto ok = [&](const std::string& v, bool create = false) { real.clear(); return resolveVirtual(roots, deny, v, real, create); };

		CHECK_EQ(ok("0"), true);
		CHECK_EQ(real, card);
		CHECK_EQ(ok("0/"), true);
		CHECK_EQ(real, card);
		CHECK_EQ(ok("1"), true);
		CHECK_EQ(real, card2);
		CHECK_EQ(ok("0/Music"), true);
		CHECK_EQ(real, card + "/Music");
		CHECK_EQ(ok("0/Music/"), true);
		CHECK_EQ(real, card + "/Music");
		CHECK_EQ(ok("0/Music/a.mp3"), true);
		CHECK_EQ(real, card + "/Music/a.mp3");
		CHECK_EQ(ok("0/Music/inlink.mp3"), true);	// a link that stays inside is fine
		CHECK_EQ(real, card + "/Music/a.mp3");
		// refusals
		for (const char* bad : {"", "/", "/0", "0//Music", "0/Music//a.mp3", "0/../outside", "0/Music/../../outside",
			"0/..", "0/.", "0/./Music", "0/.hidden", "0/.hidden/h.mp3", "0/Music/.x", "2", "2/", "99/Music", "-1", "x/Music",
			"+0", " 0", "0 ", "00000000000/x", "0/missing", "0/Music/escape", "0/Music/escape/secret.mp3", "0/Music/leak.mp3",
			"0/Music/dangling", "0/sib", "0/sib/b.mp3", "0/state", "0/state/web.conf", "0/state/"})
		{
			CHECK_EQ(ok(bad), false);
			if (ok(bad)) fprintf(stderr, "  accepted: %s\n", bad);
		}
		CHECK_EQ(ok(std::string("0/Music/a.mp3\0x", 15)), false);
		CHECK_EQ(ok(std::string("0/Mu\0sic", 8)), false);
		real = "unchanged";
		CHECK_EQ(resolveVirtual(roots, deny, "0/..", real, false), false);
		CHECK_EQ(real, std::string("unchanged"));
		// a root whose name is a prefix of a sibling (card vs cardx) doesn't leak through "card/../cardx"
		CHECK_EQ(ok("0/../cardx/b.mp3"), false);
		// deny matches the real path: a link into the state folder is refused too
		sh("ln -s '" + base + "/card/state' '" + base + "/card/Music/tolink'");
		CHECK_EQ(ok("0/Music/tolink/web.conf"), false);
		CHECK_EQ(ok("0/Music/tolink"), false);
		// deny with a trailing slash, and one that doesn't exist yet
		CHECK_EQ(resolveVirtual(roots, {base + "/card/Music/"}, "0/Music/a.mp3", real, false), false);
		CHECK_EQ(resolveVirtual(roots, {base + "/card/Mus"}, "0/Music/a.mp3", real, false), true);	// "/Mus" isn't "/Music"
		CHECK_EQ(resolveVirtual(roots, {base + "/card/Later"}, "0/Later", real, true), false);	// not made, and under deny
		// a root that is missing refuses
		CHECK_EQ(resolveVirtual({{"gone", base + "/missing"}}, deny, "0", real, false), false);
		CHECK_EQ(resolveVirtual({}, deny, "0", real, false), false);

		// forCreate
		CHECK_EQ(ok("0/Music/new.mp3", true), true);
		CHECK_EQ(real, card + "/Music/new.mp3");
		CHECK_EQ(ok("0/Music/a.mp3", true), true);	// existing is fine: the caller checks the clash
		CHECK_EQ(real, card + "/Music/a.mp3");
		CHECK_EQ(ok("0/Top.srt", true), true);
		CHECK_EQ(real, card + "/Top.srt");
		CHECK_EQ(ok("0/Music/new folder/", true), true);
		CHECK_EQ(real, card + "/Music/new folder");
		CHECK_EQ(ok("0/Music/inlink.mp3", true), true);	// a link inside stays
		for (const char* bad : {"0", "0/", "0/Music/sub/x/y.mp3", "0/nope/x.mp3", "0/Music/.new", "0/Music/a:b.mp3",
			"0/Music/a?.mp3", "0/Music/ ", "0/Music/escape/new.mp3", "0/Music/leak.mp3", "0/Music/dangling", "0/Music/../x.mp3",
			"0/Music//x.mp3", "0/state/new.mp3", "0/Music/a\\b", "2/x.mp3", "0/.hidden/new.mp3", "0/sib/new.mp3"})
		{
			CHECK_EQ(ok(bad, true), false);
			if (ok(bad, true)) fprintf(stderr, "  create accepted: %s\n", bad);
		}
		CHECK_EQ(ok(std::string("0/Music/a\0.mp3", 14), true), false);
		CHECK_EQ(ok(std::string(300, 'a').insert(0, "0/Music/"), true), false);	// too long a name

		// toVirtual
		CHECK_EQ(toVirtual(roots, card), std::string("0"));
		CHECK_EQ(toVirtual(roots, card + "/Music/a.mp3"), std::string("0/Music/a.mp3"));
		CHECK_EQ(toVirtual(roots, base + "/card/Music/a.mp3"), std::string("0/Music/a.mp3"));	// unresolved base too
		CHECK_EQ(toVirtual(roots, card2 + "/x/y.mp3"), std::string("1/x/y.mp3"));	// not there: still by name
		CHECK_EQ(toVirtual(roots, card + "/Music/escape/secret.mp3"), std::string(""));	// resolves outside
		CHECK_EQ(toVirtual(roots, base + "/cardx/b.mp3"), std::string(""));
		CHECK_EQ(toVirtual(roots, base), std::string(""));
		CHECK_EQ(toVirtual(roots, "/etc/passwd"), std::string(""));
		CHECK_EQ(toVirtual(roots, ""), std::string(""));
		CHECK_EQ(toVirtual({}, card), std::string(""));
		// the most specific root wins when one sits in another
		CHECK_EQ(toVirtual({{"a", base + "/card"}, {"b", base + "/card/Music"}}, card + "/Music/a.mp3"), std::string("1/a.mp3"));
		// servedVirtual (Recent): never the state folder, a hidden part or outside; a gone file still by name
		CHECK_EQ(servedVirtual(roots, deny, card + "/Music/a.mp3"), std::string("0/Music/a.mp3"));
		CHECK_EQ(servedVirtual(roots, deny, card + "/Music/gone.mp3"), std::string("0/Music/gone.mp3"));
		CHECK_EQ(servedVirtual(roots, deny, card + "/state/x.mp3"), std::string(""));
		CHECK_EQ(servedVirtual(roots, deny, card + "/.hidden/h.mp3"), std::string(""));
		CHECK_EQ(servedVirtual(roots, deny, card + "/Music/tolink/web.conf"), std::string(""));
		CHECK_EQ(servedVirtual(roots, deny, "/etc/passwd"), std::string(""));

		sh("rm -rf '" + base + "'");
	}

	// names
	CHECK_EQ(validName("a.mp3"), true);
	CHECK_EQ(validName("My Song (live) [2020] #1 & more, 'ok'.flac"), true);
	CHECK_EQ(validName("caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC.mp3"), true);
	CHECK_EQ(validName(std::string(255, 'a')), true);
	CHECK_EQ(validName(std::string(256, 'a')), false);
	CHECK_EQ(validName(std::string(86, '\xE6') ), true);	// 86 bytes, bytes not characters
	CHECK_EQ(validName(std::string(128, '\xC3') + std::string(128, 'a')), false);
	CHECK_EQ(validName(""), false);
	CHECK_EQ(validName(" "), false);
	CHECK_EQ(validName("   "), false);
	CHECK_EQ(validName("."), false);
	CHECK_EQ(validName(".."), false);
	CHECK_EQ(validName("..."), true);
	CHECK_EQ(validName(" a "), true);
	for (char c : std::string("/\\<>:\"|?*"))
	{
		CHECK_EQ(validName(std::string("a") + c + "b"), false);
		CHECK_EQ(validName(std::string(1, c)), false);
	}
	CHECK_EQ(validName("a\tb"), false);
	CHECK_EQ(validName("a\nb"), false);
	CHECK_EQ(validName(std::string("a\0b", 3)), false);
	CHECK_EQ(validName("a\x7f"), false);
	CHECK_EQ(validName("a\x1f"), false);
	// addresses
	for (const char* ip : {"127.0.0.1", "127.255.255.255", "10.0.0.1", "10.255.255.255", "172.16.0.1", "172.31.255.255",
		"192.168.0.1", "192.168.255.255", "169.254.1.1", "::1", "fe80::1", "fe80::abcd%wlan0", "febf::1", "fc00::1",
		"fd12:3456::1", "fdff::1", "::ffff:192.168.1.5", "::ffff:127.0.0.1", "::ffff:10.1.1.1"})
	{
		CHECK_EQ(privateAddress(ip), true);
		if (!privateAddress(ip)) fprintf(stderr, "  not private: %s\n", ip);
	}
	for (const char* ip : {"8.8.8.8", "172.15.255.255", "172.32.0.1", "192.167.1.1", "192.169.1.1", "11.0.0.1", "169.253.1.1",
		"0.0.0.0", "100.64.0.1", "1.1.1.1", "2001:db8::1", "2606:4700::1111", "fec0::1", "fe00::1", "fb00::1", "fe::1",
		"::", "::2", "::ffff:8.8.8.8", "::ffff:172.32.0.1", "", "localhost", "evil.com", "192.168.1", "192.168.1.256",
		"192.168.1.1.1", "not an ip", "[::1]", "::1x", " 10.0.0.1"})
	{
		CHECK_EQ(privateAddress(ip), false);
		if (privateAddress(ip)) fprintf(stderr, "  private: %s\n", ip);
	}
	for (const char* h : {"192.168.1.5", "192.168.1.5:8080", "localhost", "localhost:8080", "LOCALHOST:80", "127.0.0.1:8081",
		"[::1]", "[::1]:8080", "[fe80::1]:80", "10.0.0.1:1", "8.8.8.8", "[2001:db8::1]:8080"})
	{
		CHECK_EQ(hostAllowed(h), true);
		if (!hostAllowed(h)) fprintf(stderr, "  host refused: %s\n", h);
	}
	for (const char* h : {"", "evil.com", "evil.com:8080", "192.168.1.5.evil.com", "localhost.evil.com", "localhost.", "foo.localhost",
		"banana.local", "192.168.1.5:", "192.168.1.5:abc", "192.168.1.5:123456", "192.168.1.5:80:80", "::1", "[::1", "[::1]8080",
		"[::1]:", "[::1]:x", "[192.168.1.5]", "[evil]", ":8080", "192.168.1", "192.168.1.256", "1.2.3", " 192.168.1.5",
		"192.168.1.5 ", "0x7f.0.0.1", "2130706433", "192.168.1.5/", "user@192.168.1.5", "[::1]:8080:1"})
	{
		CHECK_EQ(hostAllowed(h), false);
		if (hostAllowed(h)) fprintf(stderr, "  host allowed: %s\n", h);
	}
	CHECK_EQ(interfaceRank("wlan0"), 3);
	CHECK_EQ(interfaceRank("wlan1"), 3);
	CHECK_EQ(interfaceRank("eth0"), 2);
	CHECK_EQ(interfaceRank("enp3s0"), 2);
	CHECK_EQ(interfaceRank("en0"), 2);
	CHECK_EQ(interfaceRank("br0"), 1);
	CHECK_EQ(interfaceRank("tun0"), 1);
	CHECK_EQ(interfaceRank("wwan0"), 1);
	CHECK_EQ(interfaceRank("lo"), 0);
	CHECK_EQ(interfaceRank("usb0"), 0);
	CHECK_EQ(interfaceRank("docker0"), 0);
	CHECK_EQ(interfaceRank("veth1a2b"), 0);
	CHECK_EQ(interfaceRank(""), 1);

	// JSON
	{
		JsonWriter j;
		j.obj().key("a").num(1).key("b").str("x").key("c").arr().num(1.5).num(int64_t(-7)).boolean(true).null().end()
			.key("d").obj().end().key("e").arr().end().key("f").obj().key("g").boolean(false).end().end();
		CHECK_EQ(j.out(), std::string("{\"a\":1,\"b\":\"x\",\"c\":[1.5,-7,true,null],\"d\":{},\"e\":[],\"f\":{\"g\":false}}"));
		JsonWriter k;
		k.arr().obj().key("p").str("q").end().obj().end().arr().num(1).end().end();
		CHECK_EQ(k.out(), std::string("[{\"p\":\"q\"},{},[1]]"));
		JsonWriter r;
		r.obj().key("x").raw("{\"y\":[1,2]}").key("z").raw("").key("w").num(2).end();
		CHECK_EQ(r.out(), std::string("{\"x\":{\"y\":[1,2]},\"z\":null,\"w\":2}"));
		JsonWriter u;
		u.arr().num(size_t(5)).num(int(-1)).num(uint64_t(1) << 40).num(3.0f).num(0.1).num(1e20).num(-0.0).end();
		CHECK_EQ(u.out(), std::string("[5,-1,1099511627776,3,0.1,1e+20,-0]"));
		JsonWriter n;
		n.arr().num(std::nan("")).num(INFINITY).num(-INFINITY).end();
		CHECK_EQ(n.out(), std::string("[null,null,null]"));
		JsonWriter v;
		v.arr().num(123456789.123456789).num(1.0 / 3).end();
		CHECK_EQ(v.out(), std::string("[123456789.12345679,0.3333333333333333]"));
		JsonWriter e;
		CHECK_EQ(e.out(), std::string());
		e.str("x");
		CHECK_EQ(e.out(), std::string("\"x\""));
	}
	CHECK_EQ(JsonWriter::escape("a\"b\\c/d"), std::string("\"a\\\"b\\\\c/d\""));
	CHECK_EQ(JsonWriter::escape("\b\f\n\r\t"), std::string("\"\\b\\f\\n\\r\\t\""));
	CHECK_EQ(JsonWriter::escape(std::string("\x01\x1f\x00", 3)), std::string("\"\\u0001\\u001f\\u0000\""));
	CHECK_EQ(JsonWriter::escape("\x7f"), std::string("\"\x7f\""));
	CHECK_EQ(JsonWriter::escape("caf\xC3\xA9 \xE6\x97\xA5 \xF0\x9F\x8E\xB5"), std::string("\"caf\xC3\xA9 \xE6\x97\xA5 \xF0\x9F\x8E\xB5\""));
	const std::string FFFD = "\xEF\xBF\xBD";
	CHECK_EQ(JsonWriter::escape("a\xFF" "b"), "\"a" + FFFD + "b\"");
	CHECK_EQ(JsonWriter::escape("\x80"), "\"" + FFFD + "\"");
	CHECK_EQ(JsonWriter::escape("a\xC3"), "\"a" + FFFD + "\"");	// cut short
	CHECK_EQ(JsonWriter::escape("\xC3("), "\"" + FFFD + "(\"");
	CHECK_EQ(JsonWriter::escape("\xC0\xAF"), "\"" + FFFD + FFFD + "\"");	// overlong
	CHECK_EQ(JsonWriter::escape("\xE0\x80\x80"), "\"" + FFFD + FFFD + FFFD + "\"");
	CHECK_EQ(JsonWriter::escape("\xED\xA0\x80"), "\"" + FFFD + FFFD + FFFD + "\"");	// a surrogate
	CHECK_EQ(JsonWriter::escape("\xF4\x90\x80\x80"), "\"" + FFFD + FFFD + FFFD + FFFD + "\"");	// past U+10FFFF
	CHECK_EQ(JsonWriter::escape("\xF0\x9F\x8E"), "\"" + FFFD + FFFD + FFFD + "\"");
	CHECK_EQ(JsonWriter::escape("\xE2\x82\xAC\xE2\x82"), "\"\xE2\x82\xAC" + FFFD + FFFD + "\"");

	// playability
	CHECK_EQ(std::string(mimeFor("a.mp3")), std::string("audio/mpeg"));
	CHECK_EQ(std::string(mimeFor("A.MP3")), std::string("audio/mpeg"));
	CHECK_EQ(std::string(mimeFor("a.m4a")), std::string("audio/mp4"));
	CHECK_EQ(std::string(mimeFor("a.flac")), std::string("audio/flac"));
	CHECK_EQ(std::string(mimeFor("a.opus")), std::string("audio/ogg"));
	CHECK_EQ(std::string(mimeFor("a.wav")), std::string("audio/wav"));
	CHECK_EQ(std::string(mimeFor("a.mp4")), std::string("video/mp4"));
	CHECK_EQ(std::string(mimeFor("a.webm")), std::string("video/webm"));
	CHECK_EQ(std::string(mimeFor("a.mkv")), std::string("video/x-matroska"));
	CHECK_EQ(std::string(mimeFor("a.jpg")), std::string("image/jpeg"));
	CHECK_EQ(std::string(mimeFor("a.jpeg")), std::string("image/jpeg"));
	CHECK_EQ(std::string(mimeFor("a.png")), std::string("image/png"));
	CHECK_EQ(std::string(mimeFor("a.vtt")), std::string("text/vtt; charset=utf-8"));
	CHECK_EQ(std::string(mimeFor("index.html")), std::string("text/html; charset=utf-8"));
	CHECK_EQ(std::string(mimeFor("app.js")), std::string("text/javascript; charset=utf-8"));
	CHECK_EQ(std::string(mimeFor("style.css")), std::string("text/css; charset=utf-8"));
	CHECK_EQ(std::string(mimeFor("sprite.svg")), std::string("image/svg+xml"));
	CHECK_EQ(std::string(mimeFor("Inter.ttf")), std::string("font/ttf"));
	CHECK_EQ(std::string(mimeFor("x.woff2")), std::string("font/woff2"));
	CHECK_EQ(std::string(mimeFor("OFL.txt")), std::string("text/plain; charset=utf-8"));
	CHECK_EQ(std::string(mimeFor("a.json")), std::string("application/json; charset=utf-8"));
	CHECK_EQ(std::string(mimeFor("a.xyz")), std::string("application/octet-stream"));
	CHECK_EQ(std::string(mimeFor("noext")), std::string("application/octet-stream"));
	CHECK_EQ(std::string(mimeFor("")), std::string("application/octet-stream"));

	CHECK_EQ(playableByName("a.mp3") == Playable::Yes, true);
	CHECK_EQ(playableByName("a.M4A") == Playable::Yes, true);
	CHECK_EQ(playableByName("a.flac") == Playable::Yes, true);
	CHECK_EQ(playableByName("a.jpg") == Playable::Yes, true);
	CHECK_EQ(playableByName("a.mp4") == Playable::Maybe, true);
	CHECK_EQ(playableByName("a.mkv") == Playable::Maybe, true);
	CHECK_EQ(playableByName("a.webm") == Playable::Maybe, true);
	CHECK_EQ(playableByName("a.ape") == Playable::No, true);
	CHECK_EQ(playableByName("a.wma") == Playable::No, true);
	CHECK_EQ(playableByName("a.avi") == Playable::No, true);
	CHECK_EQ(playableByName("a.xyz") == Playable::No, true);
	CHECK_EQ(playableByName("noext") == Playable::No, true);

	const char* MP4 = "mov,mp4,m4a,3gp,3g2,mj2";
	const char* MKV = "matroska,webm";
	auto P = [](const char* c, const char* v, const char* a) { return playability(c, v, a); };
	// yes
	CHECK_EQ(P("mp3", "", "mp3") == Playable::Yes, true);
	CHECK_EQ(P("mp3", "mjpeg", "mp3") == Playable::Maybe, true);	// cover art counts as a video stream: left to the browser
	CHECK_EQ(P("aac", "", "aac") == Playable::Yes, true);
	CHECK_EQ(P(MP4, "", "aac") == Playable::Yes, true);
	CHECK_EQ(P("flac", "", "flac") == Playable::Yes, true);
	CHECK_EQ(P("ogg", "", "vorbis") == Playable::Yes, true);
	CHECK_EQ(P("ogg", "", "opus") == Playable::Yes, true);
	CHECK_EQ(P("wav", "", "pcm_s16le") == Playable::Yes, true);
	CHECK_EQ(P("wav", "", "pcm_s24le") == Playable::Yes, true);
	CHECK_EQ(P(MP4, "h264", "aac") == Playable::Yes, true);
	CHECK_EQ(P(MP4, "h264", "") == Playable::Yes, true);
	CHECK_EQ(P(MP4, "vp9", "opus") == Playable::Yes, true);
	CHECK_EQ(P(MP4, "av1", "aac") == Playable::Yes, true);
	CHECK_EQ(P(MKV, "vp8", "vorbis") == Playable::Yes, true);
	CHECK_EQ(P(MKV, "vp9", "opus") == Playable::Yes, true);
	CHECK_EQ(P(MKV, "av1", "opus") == Playable::Yes, true);
	CHECK_EQ(P("MOV,MP4", "H264", "AAC") == Playable::Yes, true);
	// maybe
	CHECK_EQ(P(MP4, "hevc", "aac") == Playable::Maybe, true);
	CHECK_EQ(P(MKV, "h264", "aac") == Playable::Maybe, true);
	CHECK_EQ(P(MKV, "h264", "opus") == Playable::Maybe, true);
	CHECK_EQ(P(MKV, "vp9", "aac") == Playable::Maybe, true);
	CHECK_EQ(P(MKV, "", "flac") == Playable::Maybe, true);
	CHECK_EQ(P(MP4, "", "alac") == Playable::Maybe, true);
	CHECK_EQ(P(MP4, "h264", "alac") == Playable::Maybe, true);
	CHECK_EQ(P("ogg", "theora", "vorbis") == Playable::Maybe, true);
	CHECK_EQ(P("whatever", "h264", "aac") == Playable::Maybe, true);
	CHECK_EQ(P(MP4, "h264", "ac3") == Playable::No, true);	// silent in Chrome and Firefox
	CHECK_EQ(P(MP4, "h264", "eac3") == Playable::No, true);
	CHECK_EQ(P(MKV, "h264", "ac3") == Playable::No, true);
	CHECK_EQ(P("ac3", "", "ac3") == Playable::No, true);
	CHECK_EQ(P("eac3", "", "eac3") == Playable::No, true);
	// no
	CHECK_EQ(P("ape", "", "ape") == Playable::No, true);
	CHECK_EQ(P("asf", "wmv3", "wmav2") == Playable::No, true);
	CHECK_EQ(P("asf", "", "wmav2") == Playable::No, true);
	CHECK_EQ(P(MP4, "h264", "wmapro") == Playable::No, true);
	CHECK_EQ(P(MKV, "h264", "dts") == Playable::No, true);
	CHECK_EQ(P("dts", "", "dts") == Playable::No, true);
	CHECK_EQ(P(MKV, "h264", "truehd") == Playable::No, true);
	CHECK_EQ(P(MP4, "mpeg4", "aac") == Playable::No, true);
	CHECK_EQ(P("avi", "mpeg4", "mp3") == Playable::No, true);
	CHECK_EQ(P("avi", "h264", "aac") == Playable::No, true);
	CHECK_EQ(P("mpeg", "mpeg2video", "mp2") == Playable::No, true);
	CHECK_EQ(P(MP4, "mpeg2video", "aac") == Playable::No, true);
	CHECK_EQ(P("mpegts", "h264", "aac") == Playable::No, true);
	CHECK_EQ(P("avi", "msmpeg4v3", "mp3") == Playable::No, true);
	CHECK_EQ(P(MP4, "", "") == Playable::No, true);
	CHECK_EQ(P("", "", "") == Playable::No, true);
	CHECK_EQ(P(MP4, "none", "none") == Playable::No, true);

	// subtitles as WebVTT
	CHECK_EQ(vttTimestamp(3723.4567), std::string("01:02:03.457"));
	CHECK_EQ(vttTimestamp(-1), std::string("00:00:00.000"));
	CHECK_EQ(vttTimestamp(0.5), std::string("00:00:00.500"));
	CHECK_EQ(assCueText("{\\an8}Hello\\Nworld"), std::string("Hello\nworld"));
	CHECK_EQ(assCueText("a\\hb"), std::string("a b"));
	CHECK_EQ(assCueText("{\\i1}Hi{\\i0} there"), std::string("<i>Hi</i> there"));
	CHECK_EQ(assCueText("x{\\p1}m 0 0 l 10 0 10 10{\\p0}y"), std::string("xy"));
	CHECK_EQ(assCueText("a < b & c, d"), std::string("a &lt; b &amp; c, d"));
	CHECK_EQ(vttCue(1, 2, "Hi"), std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nHi\n\n"));
	CHECK_EQ(vttCue(1, 1, "Hi"), std::string(""));
	CHECK_EQ(vttCue(1, 2, " \n"), std::string(""));
	CHECK_EQ(vttCue(1, 2, "a\n\nb"), std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\na\nb\n\n"));
	CHECK_EQ(subtitleFileToVtt("srt", "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\n{\\an8}<i>Hi</i> <font color=red>x</font>\r\nthere\r\n\r\n2\r\n0:00:03,5 --> 0:00:04,000 X1:1\r\n&\r\n"),
		std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.500\n<i>Hi</i> x\nthere\n\n00:00:03.500 --> 00:00:04.000\n&amp;\n\n"));
	CHECK_EQ(subtitleFileToVtt("SRT", "1\n00:00:01,000 --> 00:00:02,000\ncaf\xE9" "\n"),
		std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\ncaf\xC3\xA9" "\n\n"));	// Latin-1
	CHECK_EQ(subtitleFileToVtt("ass",
		"[Script Info]\nTitle: x\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
		"Dialogue: 0,0:00:05.00,0:00:06.50,Default,,0,0,0,,Late, with comma\\Nline2\n"
		"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\b1}First{\\b0}\nComment: 0,0:00:09.00,0:00:10.00,Default,,0,0,0,,no\n"),
		std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n<b>First</b>\n\n00:00:05.000 --> 00:00:06.500\nLate, with comma\nline2\n\n"));
	CHECK_EQ(subtitleFileToVtt("vtt", "WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nx\n"), std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nx\n"));
	CHECK_EQ(subtitleFileToVtt("sup", "x"), std::string(""));
	{
		// Shift-JIS "こんにちは" (82 B1 82 F1 82 C9 82 BF 82 CD) with the ja hint; CP1252 "café" by default; UTF-16LE with a BOM
		std::string head = "1\n00:00:01,000 --> 00:00:02,000\n", tail = "\n";
		CHECK_EQ(subtitleFileToVtt("srt", head + "\x82\xB1\x82\xF1\x82\xC9\x82\xBF\x82\xCD" + tail, "ja"),
			std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\n\n"));
		CHECK_EQ(subtitleFileToVtt("srt", head + "caf\xE9 \x93q\x94" + tail),
			std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\ncaf\xC3\xA9 \xE2\x80\x9Cq\xE2\x80\x9D\n\n"));
		std::string u16 = "\xFF\xFE";
		for (char c : head + "Hi" + tail)
		{
			u16 += c;
			u16 += '\0';
		}
		CHECK_EQ(subtitleFileToVtt("srt", u16), std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nHi\n\n"));
	}
	CHECK_EQ(imageSubtitleCodec("hdmv_pgs_subtitle"), true);
	CHECK_EQ(imageSubtitleCodec("dvd_subtitle"), true);
	CHECK_EQ(imageSubtitleCodec("subrip"), false);

	// snapshotChanged
	{
		WebSnapshot a;
		a.path = "0/a.mp3";
		a.title = "T";
		a.duration = 200;
		a.position = 10;
		a.positionAtMs = 100000;
		a.volume = 50;
		a.loop = "no";
		a.chapters = {{"One", 0}, {"Two", 60}};
		WebSnapshot b = a;
		CHECK_EQ(snapshotChanged(a, b), false);
		// time passes at the normal speed: the position follows, no push
		b.positionAtMs = 105000;
		b.position = 15;
		CHECK_EQ(snapshotChanged(a, b), false);
		b.position = 16.4;
		CHECK_EQ(snapshotChanged(a, b), false);
		b.position = 16.6;
		CHECK_EQ(snapshotChanged(a, b), true);
		b.position = 13.4;
		CHECK_EQ(snapshotChanged(a, b), true);
		b.position = 13.6;
		CHECK_EQ(snapshotChanged(a, b), false);
		// a seek
		b.position = 100;
		CHECK_EQ(snapshotChanged(a, b), true);
		// position and its clock alone, no time between: a small move is fine, a jump isn't
		b = a;
		b.position = 11;
		CHECK_EQ(snapshotChanged(a, b), false);
		b.position = 12;
		CHECK_EQ(snapshotChanged(a, b), true);
		// nothing playing: time passing is no change
		{
			WebSnapshot idle, idle2;
			idle.positionAtMs = 1000;
			idle2.positionAtMs = 20000;
			CHECK_EQ(snapshotChanged(idle, idle2), false);
		}
		// speed multiplies the elapsed time
		a.speed = 2;
		b = a;
		b.positionAtMs = 105000;
		b.position = 20;
		CHECK_EQ(snapshotChanged(a, b), false);
		b.position = 15;
		CHECK_EQ(snapshotChanged(a, b), true);
		a.speed = 1;
		// paused: the position stays put
		a.paused = true;
		b = a;
		b.positionAtMs = 105000;
		b.position = 10.5;
		CHECK_EQ(snapshotChanged(a, b), false);
		b.position = 15;
		CHECK_EQ(snapshotChanged(a, b), true);
		a.paused = false;
		// the clock going backwards counts as no time
		b = a;
		b.positionAtMs = 90000;
		b.position = 10;
		CHECK_EQ(snapshotChanged(a, b), false);
		// every other field
		auto differs = [&](const std::function<void(WebSnapshot&)>& f)
		{
			WebSnapshot c = a;
			f(c);
			return snapshotChanged(a, c);
		};
		CHECK_EQ(differs([](WebSnapshot& s) { s.path = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.title = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.artist = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.album = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.kind = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.sourceKind = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.sourceTitle = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.sourceId = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.index = 1; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.count = 1; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.loading = true; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.paused = true; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.duration = 201; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.speed = 1.5; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.volume = 51; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.eqOn = true; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.eqPreset = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.eqGains[9] = 1; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.fx = "room"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.loop = "all"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.shuffled = true; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.chapters[1].title = "x"; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.chapters[1].start = 61; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.chapters.pop_back(); }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.chapter = 1; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.clients = 2; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.playlistSerial = 3; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.showHiddenFolders = true; }), true);
		CHECK_EQ(differs([](WebSnapshot& s) { s.mangaOn = true; }), true);

		WebSnapshot j;
		j.path = "0/M\"x\\.mp3";
		j.kind = "audio";
		j.sourceKind = "playlist";
		j.sourceId = "Road.m3u";
		j.count = 2;
		j.position = 1.5;
		j.positionAtMs = 42;
		j.speed = 1;
		j.eqGains[0] = -3;
		j.chapters = {{"A", 0}};
		j.chapter = 0;
		j.loop = "one";
		j.clients = 1;
		j.screen = "browser";
		CHECK_EQ(toJson(j), std::string("{\"path\":\"0/M\\\"x\\\\.mp3\",\"title\":\"\",\"artist\":\"\",\"album\":\"\",\"kind\":\"audio\","
			"\"source\":{\"kind\":\"playlist\",\"title\":\"\",\"id\":\"Road.m3u\",\"index\":0,\"count\":2},\"screen\":\"browser\",\"loading\":false,"
			"\"paused\":false,\"position\":1.5,\"positionAt\":42,\"duration\":0,\"speed\":1,\"volume\":0,"
			"\"eq\":{\"on\":false,\"preset\":\"\",\"gains\":[-3,0,0,0,0,0,0,0,0,0]},\"fx\":\"\",\"loop\":\"one\","
			"\"shuffled\":false,\"shuffleMode\":false,\"chapters\":[{\"title\":\"A\",\"start\":0}],\"chapter\":0,\"tracks\":null,"
			"\"clients\":1,\"session\":{\"boot\":0,\"v\":0},\"playlistSerial\":0,\"liked\":false,\"showHiddenFolders\":false,\"mangaOn\":false,\"incognito\":false}"));

		// the handheld's screen (its player left, or back to it) is news: the dashboard says "in the background"
		{
			WebSnapshot x = j, y = j;
			y.screen = "player";
			CHECK_EQ(snapshotChanged(x, y), true);
		}

		// a video's tracks: listed, and a change of choice is news
		WebSnapshot v;
		v.hasTracks = true;
		v.audioTracks = {{1, "English", true, true, false, false}, {2, "Track 2", false, false, false, false}};
		v.subTracks = {{1, "French \u00b7 SRT", false, false, true, true}};
		v.aid = 1;
		std::string vj = toJson(v);
		CHECK_EQ(vj.find("\"tracks\":{\"audio\":[{\"id\":1,\"label\":\"English\",\"selected\":true,\"default\":true,"
			"\"forced\":false,\"external\":false,\"lang\":\"\"},{\"id\":2,") != std::string::npos, true);
		CHECK_EQ(vj.find("\"forced\":true,\"external\":true,\"lang\":\"\"}],\"aid\":1,\"sid\":0}") != std::string::npos, true);
		WebSnapshot v2 = v;
		CHECK_EQ(snapshotChanged(v, v2), false);
		v2.audioTracks[0].selected = false;
		v2.audioTracks[1].selected = true;
		v2.aid = 2;
		CHECK_EQ(snapshotChanged(v, v2), true);
		v2 = v;
		v2.sid = 1;
		CHECK_EQ(snapshotChanged(v, v2), true);
	}

	// webTitle: the name less its extension, and a song's less its track number
	{
		CHECK_EQ(webTitle("/m/01 Sunny Peel.mp3"), std::string("Sunny Peel"));
		CHECK_EQ(webTitle("/m/01. Sunny Peel.flac"), std::string("Sunny Peel"));
		CHECK_EQ(webTitle("/m/01 - Sunny Peel.ogg"), std::string("Sunny Peel"));
		CHECK_EQ(webTitle("/m/1 - Intro.mp3"), std::string("Intro"));
		CHECK_EQ(webTitle("/m/07_Outro.mp3"), std::string("Outro"));
		CHECK_EQ(webTitle("/m/2 Fast.mp3"), std::string("2 Fast"));
		CHECK_EQ(webTitle("/m/1984.mp3"), std::string("1984"));
		CHECK_EQ(webTitle("/m/01.mp3"), std::string("01"));
		CHECK_EQ(webTitle("/m/Song.mp3"), std::string("Song"));
		CHECK_EQ(webTitle("/v/01 Pilot.mkv"), std::string("01 Pilot"));	// a video keeps its number
		CHECK_EQ(webTitle("/v/.hidden"), std::string(".hidden"));
		CHECK_EQ(webTitle("/m/01 Sunny Peel.mp3", "01 Sunny Peel.mp3"), std::string("Sunny Peel"));	// mpv's name: no tag
		CHECK_EQ(webTitle("/m/01 Sunny Peel.mp3", "Sunny Peel (Live)"), std::string("Sunny Peel (Live)"));
		CHECK_EQ(webTitle("/m/01 Sunny Peel.mp3", ""), std::string("Sunny Peel"));
	}

	// moveEntriesTo
	{
		auto mk = [](const std::string& order)
		{
			std::vector<PlaylistEntry> v;
			for (char c : order) { PlaylistEntry e; e.path = std::string("/") + c; v.push_back(e); }
			return v;
		};
		auto str = [](const std::vector<PlaylistEntry>& v) { std::string s; for (auto& e : v) s += e.path.back(); return s; };
		auto v = mk("abcdef");
		auto remap = moveEntriesTo(v, {1, 3}, 0);
		CHECK_EQ(str(v), std::string("bdacef"));
		CHECK_EQ(remap, (std::vector<int>{2, 0, 3, 1, 4, 5}));
		v = mk("abcdef");
		remap = moveEntriesTo(v, {1, 3}, 2);	// starting at index 2 of the final list
		CHECK_EQ(str(v), std::string("acbdef"));
		CHECK_EQ(remap, (std::vector<int>{0, 2, 1, 3, 4, 5}));
		v = mk("abcdef");
		moveEntriesTo(v, {0, 1}, 4);	// 4 of the 4 left: the end
		CHECK_EQ(str(v), std::string("cdefab"));
		v = mk("abcdef");
		moveEntriesTo(v, {0, 1}, 2);
		CHECK_EQ(str(v), std::string("cdabef"));
		v = mk("abcdef");
		moveEntriesTo(v, {4, 5}, 100);	// clamped
		CHECK_EQ(str(v), std::string("abcdef"));
		v = mk("abcdef");
		moveEntriesTo(v, {0}, 100);
		CHECK_EQ(str(v), std::string("bcdefa"));
		v = mk("abcdef");
		moveEntriesTo(v, {5}, -5);
		CHECK_EQ(str(v), std::string("fabcde"));
		v = mk("abcdef");
		moveEntriesTo(v, {2, 4, 0}, 1);	// order kept (a, c, e), not the set's insertion order
		CHECK_EQ(str(v), std::string("bacedf"));
		v = mk("abcdef");
		remap = moveEntriesTo(v, {}, 2);
		CHECK_EQ(str(v), std::string("abcdef"));
		CHECK_EQ(remap, (std::vector<int>{0, 1, 2, 3, 4, 5}));
		v = mk("abc");
		moveEntriesTo(v, {0, 1, 2}, 1);	// everything selected: nothing moves
		CHECK_EQ(str(v), std::string("abc"));
		v = mk("abc");
		moveEntriesTo(v, {7, -1}, 1);	// out of range ones are ignored
		CHECK_EQ(str(v), std::string("abc"));
		v = mk("");
		remap = moveEntriesTo(v, {0}, 0);
		CHECK_EQ(v.size(), size_t(0));
		CHECK_EQ(remap.size(), size_t(0));
		// the remap lines up with the old indexes
		v = mk("abcdef");
		auto before = v;
		remap = moveEntriesTo(v, {3, 5}, 1);
		for (size_t i = 0; i < before.size(); i++) CHECK_EQ(v[remap[i]].path, before[i].path);
	}
}

static void testCrashLog()
{
	namespace cl = bp::crashlog;
	char t[48];
	CHECK_EQ(std::string(t, cl::formatTime(t, sizeof t, 0, 0)), std::string("1970-01-01 00:00:00 +0000"));
	CHECK_EQ(std::string(t, cl::formatTime(t, sizeof t, 1794225792, 7 * 3600)), std::string("2026-11-09 19:03:12 +0700"));
	CHECK_EQ(std::string(t, cl::formatTime(t, sizeof t, 951782400, -5 * 3600 - 1800)), std::string("2000-02-28 18:30:00 -0530"));
	CHECK_EQ(std::string(t, cl::formatTime(t, 10, 0, 0)), std::string("1970-01-01"));	// never past the buffer

	std::string a = "=== crash A ===\nline\n\n", b = "=== crash B ===\nx\n\n", c = "=== crash C ===\nyy\n\n";
	std::string all = a + b + c;
	CHECK_EQ(cl::trimmed(all, all.size()), all);
	CHECK_EQ(cl::trimmed(all, all.size() - 1), b + c);
	CHECK_EQ(cl::trimmed(all, c.size()), c);
	CHECK_EQ(cl::trimmed(all, c.size() - 1), std::string());
	// a mark inside a line isn't an entry's start
	CHECK_EQ(cl::trimmed("=== crash A ===\nsaid === crash B\n", 20), std::string());

	char dir[] = "/tmp/bp-crash-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string path = std::string(dir) + "/crash.log";
	std::string big;
	for (int i = 0; big.size() < 200 * 1024; i++)
		big += "=== crash " + std::to_string(i) + " ===\n" + std::string(900, 'x') + "\n\n";
	{ std::ofstream(path, std::ios::binary) << big; }
	CHECK_EQ(cl::trimFile(path, cl::CAP - cl::ENTRY_MAX, cl::KEEP), true);
	std::ifstream in(path, std::ios::binary);
	std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK_EQ(kept.size() <= cl::KEEP && kept.size() > cl::KEEP - 1024, true);
	CHECK_EQ(kept.rfind(cl::MARK, 0), (size_t)0);
	CHECK_EQ(big.compare(big.size() - kept.size(), kept.size(), kept), 0);	// the newest part
	CHECK_EQ(cl::trimFile(path, cl::CAP - cl::ENTRY_MAX, cl::KEEP), false);	// under the limit: left alone
	struct stat st;
	CHECK_EQ(stat((path + ".tmp").c_str(), &st) != 0, true);
	unlink(path.c_str());
	rmdir(dir);
}


// ---- the playback Session's reducer (docs/design/session.md §6) ----

namespace
{
	// a session driven by hand: each origin's seq grows, the basis is what it last saw (the latest, unless given)
	struct Sim
	{
		Session s;
		int64_t now = 100000;
		std::map<std::string, uint64_t> seqs;
		std::vector<Effect> fx;	// everything since the last clear
		Result last;
		Sim() { s.boot = 7; }
		Intent mk(IntentType t, const std::string& origin = "hh")
		{
			Intent i;
			i.type = t;
			i.origin = origin;
			i.seq = ++seqs[origin];
			i.basis = s.v;
			return i;
		}
		Result go(const Intent& i)
		{
			Result r = apply(s, i, now);
			std::string bad = checkTransition(s, i, r, now);
			if (!bad.empty())
			{
				failures++;
				fprintf(stderr, "session: %s (after %s)\n", bad.c_str(), intentName(i.type));
			}
			s = r.s;
			last = r;
			fx.insert(fx.end(), r.fx.begin(), r.fx.end());
			return r;
		}
		Result open(int count, int index, const std::string& tab = "")
		{
			Intent i = mk(IntentType::Open, tab.empty() ? "hh" : tab);
			for (int k = 0; k < count; k++)
			{
				char p[32];
				snprintf(p, sizeof p, "/m/%02d.mp4", k);
				i.queue.push_back(p);
			}
			i.index = index;
			i.source.title = "Films";
			if (!tab.empty())
			{
				i.hasOut = true;
				i.out = browserOut(tab);
			}
			return go(i);
		}
		Result report(IntentType t, const std::string& origin, uint64_t load, double tPos = 0, bool playing = true)
		{
			Intent i = mk(t, origin);
			i.load = load;
			i.dur = 100;
			i.t = tPos;
			i.playing = playing;
			i.reason = "format";
			return go(i);
		}
		Result loaded() { return report(IntentType::Loaded, outOrigin(s), s.load); }
		Result step(int n, const std::string& origin = "hh")
		{
			Intent i = mk(IntentType::Step, origin);
			i.n = n;
			return go(i);
		}
		Result output(const Output& to, const std::string& origin, uint64_t basis, double at = -1)
		{
			Intent i = mk(IntentType::SetOutput, origin);
			i.hasOut = true;
			i.out = to;
			i.basis = basis;
			if (at >= 0) { i.hasT = true; i.t = at; }
			return go(i);
		}
		Result host(IntentType t, const std::string& tab = "")
		{
			Intent i;
			i.type = t;
			i.path = tab;
			return go(i);
		}
		// A (or the page's play button): the opposite of what it knows, resolved at the edge
		Result toggle(const std::string& origin = "hh") { return go(mk(s.playing ? IntentType::Pause : IntentType::Play, origin)); }
		int count(Effect::Kind k, const std::vector<Effect>& list) const
		{
			int n = 0;
			for (auto& e : list) n += e.kind == k;
			return n;
		}
		bool said(const std::string& text) const
		{
			for (auto& e : fx)
				if (e.kind == Effect::Note && e.text == text) return true;
			return false;
		}
	};
}

static void testSessionNamed()
{
	{
		// stop on handheld keeps the browser as the output for the next open
		Sim m;
		m.open(5, 1, "tabA");
		m.loaded();
		m.go(m.mk(IntentType::Stop));
		CHECK_EQ(m.s.index, -1);
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		m.fx.clear();
		m.open(5, 3);	// picked on the handheld
		CHECK_EQ(m.s.out, browserOut("tabA"));
		CHECK_EQ(m.count(Effect::HhPlay, m.fx), 0);
		CHECK_EQ(m.fx.size() > 0 && m.fx[0].kind == Effect::HhLoad && m.fx[0].hold, true);
	}
	{
		// open from the handheld with pref=browser goes to the browser; from the page with pref=handheld, the handheld
		Sim m;
		m.output(browserOut("tabA"), "tabA", m.s.v);	// the switch, with nothing playing
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		m.open(4, 0);
		CHECK_EQ(m.s.out, browserOut("tabA"));
		Sim h;
		Intent o = h.mk(IntentType::Open, "tabA");
		o.queue = {"/m/a.mp4", "/m/b.mp4"};
		h.go(o);
		CHECK_EQ(h.s.out, handheldOut());
		CHECK_EQ(h.count(Effect::HhPlay, h.fx), 1);
	}
	{
		// stop, the end of the list, a photo, a file the browser can't play, a pause: pref stays the browser
		Sim m;
		m.open(2, 0, "tabA");
		m.loaded();
		m.toggle("tabA");
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		Intent park = m.mk(IntentType::PhotoPark);
		m.go(park);
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		m.report(IntentType::CantPlay, "tabA", m.s.load);	// can't play it after all: skipped, the next one in the browser
		CHECK_EQ(m.s.out, browserOut("tabA"));
		CHECK_EQ(m.s.index, 1);
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		Intent no = m.mk(IntentType::Open);
		no.queue = {"/m/x.mkv", "/m/y.mkv"};
		no.browserCan = false;
		m.go(no);
		CHECK_EQ(m.s.out, handheldOut());
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		m.step(1);
		m.loaded();
		m.report(IntentType::Ended, outOrigin(m.s), m.s.load);	// the end of the list
		CHECK_EQ(m.s.index, -1);
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		m.open(3, 0);
		CHECK_EQ(m.s.out, browserOut("tabA"));
		// only SetOutput moves it
		m.output(handheldOut(), "tabA", m.s.v);
		CHECK_EQ(m.s.pref, handheldOut());
		m.go(m.mk(IntentType::Stop));
		m.open(3, 0);
		CHECK_EQ(m.s.out, handheldOut());
	}
	{
		// fast Next x15 Prev x7 ends at +8, reports arriving shuffled; one HhLoad per pass
		for (int browser = 0; browser < 2; browser++)
		{
			Sim m;
			m.open(30, 3, browser ? "tabA" : "");
			m.loaded();
			m.fx.clear();
			std::vector<uint64_t> loads;
			for (int k = 0; k < 15; k++) { m.step(1); loads.push_back(m.s.load); }
			for (int k = 0; k < 7; k++) { m.step(-1); loads.push_back(m.s.load); }
			std::vector<Effect> pass = m.fx;
			// the renderer's Loaded for older loads, in any order: all dropped
			for (int k = (int)loads.size() - 2; k >= 0; k -= 3)
				CHECK_EQ(m.report(IntentType::Loaded, browser ? "tabA" : "hh", loads[k]).reject, std::string("stale-report"));
			CHECK_EQ(m.s.index, 11);
			std::vector<Effect> run = coalesce(pass);
			CHECK_EQ(m.count(Effect::HhLoad, run), 1);
			for (auto& e : run)
				if (e.kind == Effect::HhLoad) { CHECK_EQ(e.path, std::string("/m/11.mp4")); CHECK_EQ(e.hold, browser == 1); }
			CHECK_EQ(m.loaded().reject, std::string(""));
		}
	}
	{
		// two Next in one loop pass both count
		Sim m;
		m.open(10, 0);
		m.step(1);
		m.step(1);
		CHECK_EQ(m.s.index, 2);
	}
	{
		// Next while the held copy loads goes to the page's session, never unpauses mpv (I2)
		Sim m;
		m.open(10, 2, "tabA");
		m.fx.clear();
		m.step(1);
		CHECK_EQ(m.s.out, browserOut("tabA"));
		CHECK_EQ(m.s.index, 3);
		CHECK_EQ(m.count(Effect::HhPlay, m.fx), 0);
		CHECK_EQ(m.fx.size() > 0 && m.fx[0].kind == Effect::HhLoad && m.fx[0].hold, true);
	}
	{
		// late browser-pos for the previous file is dropped / reordered Loaded from an older load is dropped
		Sim m;
		m.open(10, 0, "tabA");
		m.loaded();
		uint64_t first = m.s.load;
		m.report(IntentType::Pos, "tabA", first, 50);
		m.step(1);
		CHECK_EQ(m.report(IntentType::Pos, "tabA", first, 60).reject, std::string("stale-report"));
		CHECK_EQ(m.s.pos, 0.0);
		CHECK_EQ(m.report(IntentType::Loaded, "tabA", first).reject, std::string("stale-report"));
		CHECK_EQ(m.s.loaded, false);
	}
	{
		// A x3 from playing ends paused; a toggle seen twice (dup seq) flips once
		Sim m;
		m.open(3, 0);
		m.loaded();
		m.toggle();
		m.toggle();
		m.toggle();
		CHECK_EQ(m.s.playing, false);
		Intent play = m.mk(IntentType::Play, "tabA");
		m.go(play);
		uint64_t v = m.s.v;
		Result again = m.go(play);
		CHECK_EQ(again.dup, true);
		CHECK_EQ(m.s.playing, true);
		CHECK_EQ(m.s.v, v);
	}
	{
		// stop right after play from a never-played file keeps playing (9434cb7)
		Sim m;
		m.open(5, 1, "tabA");
		uint64_t seen = m.s.v;	// what the page saw
		m.output(handheldOut(), "tabA", seen, 12);
		Intent play = m.mk(IntentType::Play, "tabA");
		play.basis = seen;
		m.go(play);
		Intent stop = m.mk(IntentType::Stop, "tabA");
		stop.basis = seen;	// the page's trailing stop, aimed at the browser's playing
		CHECK_EQ(m.go(stop).reject, std::string("stale"));
		CHECK_EQ(m.s.index, 1);
		CHECK_EQ(m.s.playing, true);
		CHECK_EQ(m.s.out, handheldOut());
	}
	{
		// handoff to the handheld of a file it only held keeps playing there (ao-reload on the first HhPlay)
		Sim m;
		m.open(5, 1, "tabA");
		m.loaded();
		m.fx.clear();
		m.output(handheldOut(), "tabA", m.s.v, 42);
		CHECK_EQ(m.s.out, handheldOut());
		CHECK_EQ(m.s.pos, 42.0);
		bool reload = false;
		for (auto& e : m.fx) reload |= e.kind == Effect::HhPlay && e.aoReload;
		CHECK_EQ(reload, true);
		CHECK_EQ(m.loaded().reject, std::string(""));	// the handheld is the one that reports now
	}
	{
		// tracks survive browser→handheld→browser (renderer counts differ); an unplayable audio pick plays the default
		// in the browser and comes back on the handheld
		Sim m;
		m.open(5, 0, "tabA");
		Intent a = m.mk(IntentType::SetAudio, "tabA");
		a.audio = {2, 3, "jpn", "Japanese (DTS 5.1)", false, true};
		m.go(a);
		Intent sb = m.mk(IntentType::SetSub, "tabA");
		sb.sub = {1, 2, "eng", "English [ASS]", true, true};
		m.go(sb);
		m.output(handheldOut(), "tabA", m.s.v);
		std::vector<TrackInfo> hhAudio = {{"eng", "English (AAC)"}, {"eng", "Commentary"}, {"jpn", "Japanese (DTS 5.1)"}};
		std::vector<TrackInfo> hhSubs = {{"eng", "English"}, {"fre", "French"}, {"spa", "Spanish"}, {"eng", "English [ASS]", true}};
		CHECK_EQ(matchTrack(m.s.audio, hhAudio), 2);
		CHECK_EQ(matchTrack(m.s.sub, hhSubs), 3);
		m.output(browserOut("tabA"), "hh", m.s.v);
		CHECK_EQ(m.s.audio.ord, 2);
		CHECK_EQ(m.s.sub.label, std::string("English [ASS]"));
		std::vector<TrackInfo> chromeAudio = hhAudio;
		chromeAudio[2].playable = false;
		CHECK_EQ(matchTrack(m.s.audio, chromeAudio), MATCH_DEFAULT);
		std::vector<TrackInfo> pageSubs = {{"eng", "English [ASS]", true}};
		CHECK_EQ(matchTrack(m.s.sub, pageSubs), 0);
		m.output(handheldOut(), "tabA", m.s.v);
		CHECK_EQ(matchTrack(m.s.audio, hhAudio), 2);
		CHECK_EQ(matchTrack(TrackPick(), hhAudio), MATCH_DEFAULT);
		TrackPick off;
		off.set = true;
		CHECK_EQ(matchTrack(off, hhSubs), MATCH_OFF);
		TrackPick byLang;
		byLang.set = true;
		byLang.ord = 5;
		byLang.lang = "FRE";
		CHECK_EQ(matchTrack(byLang, hhSubs), 1);
	}
	{
		// photo over music keeps the browser's song
		Sim m;
		m.open(5, 0, "tabA");
		m.loaded();
		m.fx.clear();
		m.go(m.mk(IntentType::PhotoPark));
		CHECK_EQ(m.s.out, browserOut("tabA"));
		CHECK_EQ(m.s.playing, true);
		CHECK_EQ(m.s.parked, false);
		CHECK_EQ(m.count(Effect::HhPhoto, m.fx), 1);
	}
	{
		// photo over music on the handheld parks and resumes at its place
		Sim m;
		m.open(5, 0);
		m.loaded();
		m.report(IntentType::Pos, "hh", m.s.load, 42);
		Intent park = m.mk(IntentType::PhotoPark);
		park.hasT = true;
		park.t = 42;
		m.go(park);
		CHECK_EQ(m.s.parked, true);
		CHECK_EQ(m.s.playing, false);
		m.now += 60000;
		CHECK_EQ(posNow(m.s, m.now), 42.0);
		CHECK_EQ(m.report(IntentType::Pos, "hh", m.s.load, 50).reject, std::string("parked"));
		m.fx.clear();
		uint64_t load = m.s.load;
		m.go(m.mk(IntentType::PhotoResume));
		CHECK_EQ(m.s.parked, false);
		CHECK_EQ(m.s.playing, true);
		CHECK_EQ(m.s.load, load + 1);
		CHECK_EQ(m.fx.size() == 2 && m.fx[0].kind == Effect::HhLoad && m.fx[0].t == 42.0 && m.fx[1].kind == Effect::HhPlay, true);
	}
	{
		// Prev ≥5 s restarts, Prev <5 s goes back, a Prev burst from 7 s restarts then n-1, Prev on the first <5 s rejects
		Sim m;
		m.open(10, 5);
		m.loaded();
		m.report(IntentType::Pos, "hh", m.s.load, 7);
		uint64_t seekN = m.s.seekN;
		m.step(-1);
		CHECK_EQ(m.s.index, 5);
		CHECK_EQ(m.s.seekN, seekN + 1);
		CHECK_EQ(m.s.pos, 0.0);
		m.now += 3000;
		m.step(-1);
		CHECK_EQ(m.s.index, 4);
		m.loaded();
		m.report(IntentType::Pos, "hh", m.s.load, 7);
		m.step(-3);
		CHECK_EQ(m.s.index, 2);
		Sim f;
		f.open(10, 0);
		f.loaded();
		f.now += 2000;
		f.fx.clear();
		CHECK_EQ(f.step(-1).reject, std::string("first"));
		CHECK_EQ(f.said("This is the first one"), true);
		CHECK_EQ(f.s.index, 0);
	}
	{
		// end of list stops, says Finished; loop one restarts; loop all wraps both ways
		Sim m;
		m.open(3, 2);
		m.loaded();
		m.report(IntentType::Ended, "hh", m.s.load);
		CHECK_EQ(m.s.index, -1);
		CHECK_EQ(m.s.playing, false);
		CHECK_EQ(m.said("Finished"), true);
		Sim one;
		one.open(3, 1);
		Intent l = one.mk(IntentType::SetLoop);
		l.loop = SESSION_LOOP_ONE;
		one.go(l);
		one.loaded();
		uint64_t seekN = one.s.seekN;
		one.report(IntentType::Ended, "hh", one.s.load);
		CHECK_EQ(one.s.index, 1);
		CHECK_EQ(one.s.seekN, seekN + 1);
		Sim all;
		all.open(3, 2);
		l = all.mk(IntentType::SetLoop);
		l.loop = SESSION_LOOP_ALL;
		all.go(l);
		all.loaded();
		all.report(IntentType::Ended, "hh", all.s.load);
		CHECK_EQ(all.s.index, 0);
		all.step(-1);
		CHECK_EQ(all.s.index, 2);
		all.step(1);
		CHECK_EQ(all.s.index, 0);
	}
	{
		// the end of a list keeps it (the pages offer it again); Stop puts it away; a pick, a Step-free Open, clears it
		Sim m;
		m.open(3, 2, "tabA");
		m.report(IntentType::Loaded, "tabA", m.s.load);
		Intent e = m.mk(IntentType::Ended, "tabA");
		e.load = m.s.load;
		m.go(e);
		CHECK_EQ(m.s.index, -1);
		CHECK_EQ(m.s.ended.index, 2);
		CHECK_EQ(m.s.ended.queue.size(), (size_t)3);
		CHECK_EQ(m.s.ended.source.title, std::string("Films"));
		CHECK_EQ(m.s.ended.dur, 100.0);
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		std::string js = sessionJson(m.s, m.now, [](const std::string& p) { return "0" + p; });
		CHECK_EQ(js.find("\"ended\":{\"queue\":[\"0/m/00.mp4\",\"0/m/01.mp4\",\"0/m/02.mp4\"],\"index\":2") != std::string::npos, true);
		// (its last item not served: nothing to show)
		js = sessionJson(m.s, m.now, [](const std::string& p) { return p == "/m/02.mp4" ? std::string() : "0" + p; });
		CHECK_EQ(js.find("\"ended\":null") != std::string::npos, true);
		// Next / Play / Seek have nothing to act on; the switch of output keeps it
		CHECK_EQ(m.step(1).reject, std::string("nothing"));
		CHECK_EQ(m.go(m.mk(IntentType::Play)).reject, std::string("nothing"));
		m.output(handheldOut(), "tabA", m.s.v);
		CHECK_EQ(m.s.ended.index, 2);
		// a row of it played again: an Open, and the finished list is gone
		Intent o = m.mk(IntentType::Open, "tabA");
		o.queue = m.s.ended.queue;
		o.index = 1;
		o.source = m.s.ended.source;
		m.go(o);
		CHECK_EQ(m.s.index, 1);
		CHECK_EQ(m.s.ended.index, -1);
		CHECK_EQ(m.s.playing, true);
		// to its end again, then Stop: nothing playing, nothing offered; a second Stop has nothing to do
		m.loaded();
		m.report(IntentType::Ended, "hh", m.s.load);
		CHECK_EQ(m.s.index, 2);
		m.loaded();
		m.report(IntentType::Ended, "hh", m.s.load);
		CHECK_EQ(m.s.ended.index, 2);
		uint64_t v = m.s.v;
		CHECK_EQ(m.go(m.mk(IntentType::Stop, "tabA")).reject, std::string(""));
		CHECK_EQ(m.s.ended.index, -1);
		CHECK_EQ(m.s.v, v + 1);
		CHECK_EQ(m.go(m.mk(IntentType::Stop)).reject, std::string("nothing"));
		js = sessionJson(m.s, m.now, [](const std::string& p) { return p; });
		CHECK_EQ(js.find("\"ended\":null") != std::string::npos, true);
		// Stop of a list still playing: no finished list (Stop means stop)
		Sim st;
		st.open(2, 1);
		st.loaded();
		st.go(st.mk(IntentType::Stop));
		CHECK_EQ(st.s.ended.index, -1);
		// a loop never ends a list; an Ended in the middle steps on
		Sim mid;
		mid.open(3, 0);
		mid.loaded();
		mid.report(IntentType::Ended, "hh", mid.s.load);
		CHECK_EQ(mid.s.index, 1);
		CHECK_EQ(mid.s.ended.index, -1);
		// a file of the finished list deleted: left out (the index follows); its last item deleted: gone
		Sim g;
		g.open(4, 3);
		g.loaded();
		g.report(IntentType::Ended, "hh", g.s.load);
		Intent fg = g.mk(IntentType::FileGone);
		fg.path = "/m/01.mp4";
		g.go(fg);
		CHECK_EQ(g.s.ended.queue.size(), (size_t)3);
		CHECK_EQ(g.s.ended.index, 2);
		CHECK_EQ(g.s.ended.queue[2], std::string("/m/03.mp4"));
		fg = g.mk(IntentType::FileGone);
		fg.path = "/m/03.mp4";
		g.go(fg);
		CHECK_EQ(g.s.ended.index, -1);
		CHECK_EQ(g.s.ended.queue.empty(), true);
	}
	{
		// Shuffle (§14): a hidden order from the item (the queue as it is), Next along it and Previous back, each item
		// once a round; with Loop all a new round, with Loop off the end; independent of Loop; a new list keeps it
		Sim m;
		Intent on = m.mk(IntentType::SetShuffle);
		on.on = true;
		on.rnd = 42;
		CHECK_EQ(m.go(on).reject, std::string(""));	// (nothing playing: the mode for the next list)
		CHECK_EQ(m.s.shuffle, true);
		CHECK_EQ(m.s.order.empty(), true);
		auto openR = [&](Sim& x, int count, int index, uint64_t rnd) {
			Intent i = x.mk(IntentType::Open);
			for (int k = 0; k < count; k++) { char p[32]; snprintf(p, sizeof p, "/m/%02d.mp3", k); i.queue.push_back(p); }
			i.index = index;
			i.rnd = rnd;
			return x.go(i);
		};
		openR(m, 6, 2, 7);
		CHECK_EQ(m.s.index, 2);	// the item picked plays first
		CHECK_EQ(m.s.queue[0], std::string("/m/00.mp3"));	// the queue keeps its order
		CHECK_EQ(m.s.order.size(), (size_t)6);
		CHECK_EQ(m.s.order[0], 2);
		std::string js = sessionJson(m.s, m.now, [](const std::string& p) { return p; });
		CHECK_EQ(js.find("\"shuffle\":true") != std::string::npos, true);
		// to the end of a round: every item once, then (Loop off) the end, as In order
		std::set<int> seen = {m.s.index};
		std::vector<int> played = {m.s.index};
		for (int k = 0; k < 5; k++)
		{
			m.loaded();
			Intent e = m.mk(IntentType::Ended);
			e.load = m.s.load;
			e.rnd = 100 + k;
			m.go(e);
			seen.insert(m.s.index);
			played.push_back(m.s.index);
		}
		CHECK_EQ(seen.size(), (size_t)6);
		CHECK_EQ(played == m.s.order, true);
		// Previous goes back in it, Next forward again
		m.step(-1);
		CHECK_EQ(m.s.index, played[4]);
		m.step(1);
		CHECK_EQ(m.s.index, played[5]);
		CHECK_EQ(hasNext(m.s), true);	// (Next at the round's end: a new round; the Next key isn't dimmed)
		m.loaded();
		Intent e = m.mk(IntentType::Ended);
		e.load = m.s.load;
		m.go(e);
		CHECK_EQ(m.s.index, -1);
		CHECK_EQ(m.s.ended.index, played[5]);
		CHECK_EQ(m.s.shuffle, true);	// (the setting stays)
		// Loop all: rounds go on for ever, a new order each, never the last one twice in a row
		Sim a;
		{ Intent sh = a.mk(IntentType::SetShuffle); sh.on = true; sh.rnd = 42; a.go(sh); }
		Intent lp = a.mk(IntentType::SetLoop);
		lp.loop = SESSION_LOOP_ALL;
		a.go(lp);
		CHECK_EQ(a.s.shuffle, true);	// (independent)
		openR(a, 4, 0, 3);
		int ends = 0;
		for (int k = 0; k < 20; k++)
		{
			int was = a.s.index;
			a.loaded();
			Intent x = a.mk(IntentType::Ended);
			x.load = a.s.load;
			x.rnd = 1000 + k;
			a.go(x);
			CHECK_EQ(a.s.index >= 0, true);
			CHECK_EQ(a.s.index != was, true);
			ends++;
		}
		CHECK_EQ(ends, 20);
		CHECK_EQ(checkInvariants(a.s), std::string(""));
		// Next past a round with Loop all: a new round too
		uint64_t load = a.s.load;
		for (int k = 0; k < 6; k++) CHECK_EQ(a.step(1).reject, std::string(""));
		CHECK_EQ(a.s.load > load, true);
		// Loop one with Shuffle: the same song again
		Sim o;
		{ Intent sh = o.mk(IntentType::SetShuffle); sh.on = true; sh.rnd = 42; o.go(sh); }
		Intent one = o.mk(IntentType::SetLoop);
		one.loop = SESSION_LOOP_ONE;
		o.go(one);
		openR(o, 5, 1, 9);
		o.loaded();
		Intent x = o.mk(IntentType::Ended);
		x.load = o.s.load;
		o.go(x);
		CHECK_EQ(o.s.index, 1);
		// another list opened: Shuffle and Loop both kept, a new order from its item
		openR(o, 3, 2, 5);
		CHECK_EQ(o.s.shuffle, true);
		CHECK_EQ(o.s.loop, SESSION_LOOP_ONE);
		CHECK_EQ(o.s.order[0], 2);
		// Shuffle off mid-list: no reload, Next in the queue's order; on again: a new order from the item, no reload
		Sim f;
		{ Intent sh = f.mk(IntentType::SetShuffle); sh.on = true; sh.rnd = 42; f.go(sh); }
		openR(f, 5, 3, 1);
		f.loaded();
		load = f.s.load;
		Intent off = f.mk(IntentType::SetShuffle);
		f.go(off);
		CHECK_EQ(f.s.load, load);
		CHECK_EQ(f.s.order.empty(), true);
		f.step(1);
		CHECK_EQ(f.s.index, 4);
		load = f.s.load;
		Intent on2 = f.mk(IntentType::SetShuffle);
		on2.on = true;
		on2.rnd = 77;
		f.go(on2);
		CHECK_EQ(f.s.load, load);
		CHECK_EQ(f.s.order[0], 4);
		// a file gone: out of the hidden order, its places renumbered
		Intent fg = f.mk(IntentType::FileGone);
		fg.path = "/m/01.mp3";
		f.go(fg);
		CHECK_EQ(f.s.order.size(), (size_t)4);
		CHECK_EQ(checkInvariants(f.s), std::string(""));
	}
	{
		// Requeue: a song added to the playlist that plays, its last song playing: Next reaches it, nothing reloads
		Sim m;
		Intent op = m.mk(IntentType::Open);
		op.queue = {"/m/a.mp3", "/m/b.mp3", "/m/c.mp3"};
		op.index = 2;
		op.source = {2, "Mix", "/pl/Mix.m3u"};
		m.go(op);
		m.loaded();
		CHECK_EQ(hasNext(m.s), false);
		uint64_t load = m.s.load;
		Intent rq = m.mk(IntentType::Requeue);
		rq.source = op.source;
		rq.queue = {"/m/a.mp3", "/m/b.mp3", "/m/c.mp3", "/m/d.mp3"};
		rq.remap = {0, 1, 2};
		rq.index = 2;
		CHECK_EQ(m.go(rq).reject, std::string(""));
		CHECK_EQ(m.s.load, load);
		CHECK_EQ(hasNext(m.s), true);
		m.step(1);
		CHECK_EQ(m.s.queue[m.s.index], std::string("/m/d.mp3"));
		// another list's edit, or a remap that doesn't fit: refused
		Intent other = m.mk(IntentType::Requeue);
		other.source = {2, "Other", "/pl/Other.m3u"};
		other.queue = rq.queue;
		other.remap = {0, 1, 2, 3};
		other.index = 3;
		CHECK_EQ(m.go(other).reject, std::string("other-list"));
		Intent bad = m.mk(IntentType::Requeue);
		bad.source = op.source;
		bad.queue = {"/m/d.mp3", "/m/a.mp3"};
		bad.remap = {1, -1, -1, 1};
		bad.index = 1;
		CHECK_EQ(m.go(bad).reject, std::string("bad-remap"));
		// Shuffle: what was kept keeps its hidden order, what was added plays after (I13 holds)
		Sim sh;
		{ Intent on = sh.mk(IntentType::SetShuffle); on.on = true; on.rnd = 42; sh.go(on); }
		Intent o2 = sh.mk(IntentType::Open);
		o2.queue = {"/m/a.mp3", "/m/b.mp3", "/m/c.mp3", "/m/d.mp3"};
		o2.index = 1;
		o2.rnd = 5;
		o2.source = op.source;
		sh.go(o2);
		std::vector<int> was = sh.s.order;
		Intent r2 = sh.mk(IntentType::Requeue);
		r2.source = op.source;
		r2.queue = {"/m/b.mp3", "/m/c.mp3", "/m/d.mp3", "/m/e.mp3"};	// a removed, e added
		r2.remap = {-1, 0, 1, 2};
		r2.index = 0;
		CHECK_EQ(sh.go(r2).reject, std::string(""));
		CHECK_EQ(sh.s.order.size(), (size_t)4);
		CHECK_EQ(sh.s.order[0], 0);	// (b, playing, first as before)
		CHECK_EQ(sh.s.order.back(), 3);	// (e, added: last)
		CHECK_EQ(checkInvariants(sh.s), std::string(""));
	}
	{
		// handheld Stop with the browser as output stops the page; the next pick goes to the browser (pref)
		Sim m;
		m.open(5, 0, "tabA");
		m.loaded();
		m.go(m.mk(IntentType::Stop));
		CHECK_EQ(m.s.index, -1);
		CHECK_EQ(m.s.playing, false);
		m.open(5, 3);
		CHECK_EQ(m.s.out, browserOut("tabA"));
		Intent unserved = m.mk(IntentType::Open);
		unserved.queue = {"/m/x.mkv"};
		unserved.browserCan = false;
		m.go(unserved);
		CHECK_EQ(m.s.out, handheldOut());
		CHECK_EQ(m.s.pref, browserOut("tabA"));
	}
	{
		// browser can't play → skipped in the browser (said in the page), the next one plays there; after a Previous,
		// skipped backwards; a whole list it can't play stops once each was tried
		Sim m;
		m.open(5, 0, "tabA");
		m.loaded();
		m.report(IntentType::Pos, "tabA", m.s.load, 30, true);
		m.report(IntentType::CantPlay, "tabA", m.s.load);
		CHECK_EQ(m.s.out, browserOut("tabA"));
		CHECK_EQ(m.s.index, 1);
		CHECK_EQ(m.s.playing, true);
		m.loaded();
		m.step(1);
		m.step(-1);	// back to 1 (under RESTART_S)
		CHECK_EQ(m.s.index, 1);
		m.step(-1);
		CHECK_EQ(m.s.index, 0);
		m.report(IntentType::CantPlay, "tabA", m.s.load);	// at the start going back: nothing before it, it stops
		CHECK_EQ(m.s.index, -1);
		Sim b;
		b.open(5, 3, "tabA");
		b.loaded();
		b.step(-1);
		CHECK_EQ(b.s.index, 2);
		b.report(IntentType::CantPlay, "tabA", b.s.load);
		CHECK_EQ(b.s.index, 1);	// skipped backwards
		CHECK_EQ(b.s.out, browserOut("tabA"));
		Sim all;
		all.open(3, 0, "tabA");
		for (int k = 0; k < 3 && all.s.index >= 0; k++) all.report(IntentType::CantPlay, "tabA", all.s.load);
		CHECK_EQ(all.s.index, -1);	// each tried once, then stopped
		// unanswered load for 8 s → skipped too
		Sim u;
		u.open(5, 0, "tabA");
		u.now += 7900;
		u.host(IntentType::Tick);
		CHECK_EQ(u.s.index, 0);
		u.now += 200;
		u.host(IntentType::Tick);
		CHECK_EQ(u.s.out, browserOut("tabA"));
		CHECK_EQ(u.s.index, 1);
	}
	{
		// output tab gone 10 s → handheld paused; tab back within 9 s keeps it
		Sim m;
		m.open(5, 0, "tabA");
		m.loaded();
		m.report(IntentType::Pos, "tabA", m.s.load, 20, true);
		m.host(IntentType::TabGone, "tabA");
		m.now += 9999;
		m.host(IntentType::Tick);
		CHECK_EQ(m.s.out, browserOut("tabA"));
		m.now += 2;
		m.host(IntentType::Tick);
		CHECK_EQ(m.s.out, handheldOut());
		CHECK_EQ(m.s.playing, false);
		CHECK_EQ(m.s.pref, handheldOut());
		CHECK_EQ(m.said("Your browser went \xC2\xB7 paused here"), true);
		CHECK_EQ(m.count(Effect::HhPlay, m.fx), 0);
		Sim b;
		b.open(5, 0, "tabA");
		b.loaded();
		b.host(IntentType::TabGone, "tabA");
		b.now += 9000;
		b.host(IntentType::TabBack, "tabA");
		b.now += 5000;
		b.host(IntentType::Tick);
		CHECK_EQ(b.s.out, browserOut("tabA"));
		CHECK_EQ(b.s.playing, true);
	}
	{
		// a second tab takes the output; the first's reports are dropped
		Sim m;
		m.open(5, 0, "tabA");
		m.loaded();
		uint64_t loadA = m.s.load;
		m.output(browserOut("tabB"), "tabB", m.s.v);
		CHECK_EQ(m.report(IntentType::Pos, "tabA", loadA, 70).reject, std::string("stale-report"));
		CHECK_EQ(m.report(IntentType::Pos, "tabA", m.s.load, 70).reject, std::string("stale-report"));
		CHECK_EQ(m.report(IntentType::Loaded, "tabB", m.s.load).reject, std::string(""));
	}
	{
		// Woke after sleep pauses; a new boot resets the page
		Sim m;
		m.open(5, 0);
		m.loaded();
		m.host(IntentType::Slept);
		m.host(IntentType::Woke);
		CHECK_EQ(m.s.playing, false);
		CHECK_EQ(takesSnapshot(7, 10, 8, 1), true);
		CHECK_EQ(takesSnapshot(7, 10, 7, 9), false);
		CHECK_EQ(takesSnapshot(7, 10, 7, 10), false);
		CHECK_EQ(takesSnapshot(7, 10, 7, 11), true);
		// a host-only intent from a tab is refused
		Intent t;
		t.type = IntentType::Woke;
		t.origin = "tabA";
		CHECK_EQ(m.go(t).reject, std::string("origin"));
	}
	{
		// FileGone of the current file stops; of an earlier one shifts the index; of a later one keeps it
		Sim m;
		m.open(6, 3);
		Intent g = m.mk(IntentType::FileGone);
		g.path = "/m/01.mp4";
		m.go(g);
		CHECK_EQ(m.s.index, 2);
		CHECK_EQ(m.s.queue[2], std::string("/m/03.mp4"));
		g = m.mk(IntentType::FileGone);
		g.path = "/m/05.mp4";
		m.go(g);
		CHECK_EQ(m.s.index, 2);
		CHECK_EQ(m.s.queue.size(), (size_t)4);
		g = m.mk(IntentType::FileGone);
		g.path = "/m/";
		m.go(g);
		CHECK_EQ(m.s.index, -1);
		CHECK_EQ(m.said("Stopped: the file was deleted"), true);
	}
	{
		// the handheld playing again (hold A) makes it the output
		Sim m;
		m.open(5, 0, "tabA");
		m.loaded();
		m.toggle("tabA");	// the page paused
		m.fx.clear();
		m.output(handheldOut(), "hh", m.s.v);
		m.go(m.mk(IntentType::Play));
		CHECK_EQ(m.s.out, handheldOut());
		CHECK_EQ(m.s.playing, true);
		CHECK_EQ(m.count(Effect::HhPlay, m.fx), 1);
	}
	{
		// photos never enter the queue; a photo picked alone is refused
		Sim m;
		Intent o = m.mk(IntentType::Open);
		o.queue = {"/m/a.jpg", "/m/b.mp3", "/m/c.png", "/m/d.mp3"};
		o.index = 3;
		m.go(o);
		CHECK_EQ(m.s.queue.size(), (size_t)2);
		CHECK_EQ(m.s.index, 1);
		o = m.mk(IntentType::Open);
		o.queue = {"/m/a.jpg"};
		CHECK_EQ(m.go(o).reject, std::string("empty"));
	}
	{
		// the seen store keeps the last 16 origins: a retry from one pushed out is taken as new
		Sim m;
		m.open(5, 0);
		std::vector<Intent> first;
		for (int k = 0; k < 17; k++)
		{
			Intent i = m.mk(IntentType::Seek, "tab" + std::to_string(k));
			i.t = k;
			first.push_back(i);
			m.go(i);
		}
		CHECK_EQ(m.s.seen.size(), (size_t)16);
		CHECK_EQ(m.go(first[16]).dup, true);
		CHECK_EQ(m.go(first[0]).dup, false);
		CHECK_EQ(m.s.pos, 0.0);
	}
	{
		// Play on (the handheld's picker): a browser refused the sound without a tap waits for one, the wish kept, the
		// place frozen; its play (Pos playing) ends the wait
		Sim m;
		m.open(5, 1);
		m.loaded();
		m.report(IntentType::Pos, "hh", m.s.load, 24, true);
		m.output(browserOut("tabA"), "hh", m.s.v);
		CHECK_EQ(m.s.out, browserOut("tabA"));
		CHECK_EQ(m.s.pref, browserOut("tabA"));
		CHECK_EQ(m.s.pos, 24.0);
		CHECK_EQ(m.report(IntentType::Blocked, "hh", m.s.load).reject, std::string("stale-report"));
		Intent b = m.mk(IntentType::Blocked, "tabA");
		b.load = m.s.load;
		b.hasT = true;
		b.t = 24.2;
		uint64_t v = m.s.v;
		m.go(b);
		CHECK_EQ(m.s.blocked, true);
		CHECK_EQ(m.s.playing, true);
		CHECK_EQ(m.s.loaded, true);
		CHECK_EQ(m.s.v, v + 1);
		m.now += 5000;
		CHECK_EQ(posNow(m.s, m.now), 24.2);	// (nothing sounds: the place doesn't run on)
		m.report(IntentType::Pos, "tabA", m.s.load, 24.2, true);
		CHECK_EQ(m.s.blocked, false);
		CHECK_EQ(m.s.playing, true);
	}
	{
		// waiting for a tap: B on the handheld (SetOutput here) plays here, at the place, its sound reopened
		Sim m;
		m.open(5, 1);
		m.loaded();
		m.output(browserOut("tabA"), "hh", m.s.v);
		Intent b = m.mk(IntentType::Blocked, "tabA");
		b.load = m.s.load;
		m.go(b);
		CHECK_EQ(m.s.blocked, true);
		m.fx.clear();
		m.output(handheldOut(), "hh", m.s.v);
		CHECK_EQ(m.s.blocked, false);
		CHECK_EQ(m.s.out, handheldOut());
		CHECK_EQ(m.s.playing, true);
		CHECK_EQ(m.count(Effect::HhPlay, m.fx), 1);
		CHECK_EQ(m.fx.back().aoReload, true);
		// a pause, a Next, a stop end the wait; a paused session has nothing to wait for
		Sim p;
		p.open(5, 1, "tabA");
		p.loaded();
		Intent k = p.mk(IntentType::Blocked, "tabA");
		k.load = p.s.load;
		p.go(k);
		p.toggle();
		CHECK_EQ(p.s.blocked, false);
		k = p.mk(IntentType::Blocked, "tabA");
		k.load = p.s.load;
		CHECK_EQ(p.go(k).reject, std::string("paused"));
		p.toggle();
		k = p.mk(IntentType::Blocked, "tabA");
		k.load = p.s.load;
		p.go(k);
		CHECK_EQ(p.s.blocked, true);
		uint64_t oldLoad = p.s.load;
		p.step(1);
		CHECK_EQ(p.s.blocked, false);
		CHECK_EQ(p.s.out, browserOut("tabA"));
		// (the old load's word comes late: dropped)
		k = p.mk(IntentType::Blocked, "tabA");
		k.load = oldLoad;
		CHECK_EQ(p.go(k).reject, std::string("stale-report"));
	}
	{
		// the waiting tab gone 10 s: the handheld, paused (never sound by itself), the wait over
		Sim m;
		m.open(3, 0, "tabA");
		m.loaded();
		Intent b = m.mk(IntentType::Blocked, "tabA");
		b.load = m.s.load;
		m.go(b);
		m.host(IntentType::TabGone, "tabA");
		m.now += TAB_GRACE_MS;
		m.host(IntentType::Tick);
		CHECK_EQ(m.s.out, handheldOut());
		CHECK_EQ(m.s.playing, false);
		CHECK_EQ(m.s.blocked, false);
	}
}

// the property test: a model network (delay, reordering, 10 % duplicates, 5 % drops retried with the same seq), model
// renderers for the output, random intents of every kind; I1–I10 after every apply, liveness once it all settles
static int fuzzEnded = 0;	// sessions the fuzz left with a finished list (it must get there to test it)
static int fuzzShuffled = 0, fuzzUnshuffled = 0, fuzzRounds = 0;	// Shuffle turned on / off while playing; new rounds
static int fuzzRequeued = 0;	// playlist edits taken while an item played (shuffled or not)
static bool fuzzSession(uint32_t seed, int steps)
{
	std::mt19937 rng(seed);
	std::mt19937_64 rr(seed * 7919 + 1);	// (Shuffle's random numbers: a stream of their own)
	auto pick = [&](int n) { return (int)(rng() % (uint32_t)n); };
	Session s;
	s.boot = seed + 1;
	int64_t now = 1000;
	struct Msg { Intent i; int64_t at; };
	std::vector<Msg> net;
	std::map<std::string, uint64_t> seqs;
	std::map<std::string, Session> view;	// each tab's last snapshot
	const char* TABS[] = {"tabA", "tabB"};
	const char* FILES[] = {"/m/01.mp3", "/m/02.mp3", "/m/03.mkv", "/m/04.mp4", "/m/05.mp3", "/m/06.jpg", "/m/07.mp3"};
	std::string why;
	int at = 0;
	auto run = [&](const Intent& i) {
		Result r = apply(s, i, now);
		std::string bad = checkTransition(s, i, r, now);
		if (bad.empty() && i.seq > 0)
		{
			// the same message again at once: the same answer, nothing done twice (I10)
			Result again = apply(r.s, i, now);
			if (!again.dup || again.reject != r.reject || !again.fx.empty() || again.s.v != r.s.v) bad = "I10: a repeat wasn't a dup";
		}
		if (!bad.empty() && why.empty()) why = bad + " (" + intentName(i.type) + " from " + i.origin + ", step " + std::to_string(at) + ")";
		if (s.ended.index < 0 && r.s.ended.index >= 0) fuzzEnded++;
		if (i.type == IntentType::Requeue && r.reject.empty() && !r.dup) fuzzRequeued++;
		if (i.type == IntentType::SetShuffle && r.reject.empty() && !r.dup && s.index >= 0)
			(r.s.shuffle ? fuzzShuffled : fuzzUnshuffled) += s.shuffle != r.s.shuffle;
		// a shuffled round that went on past its end (Loop all: a new round)
		if (i.type == IntentType::Ended && r.reject.empty() && !r.dup && s.shuffle && s.index >= 0 && r.s.index >= 0 &&
			shufflePlace(s) + 1 == (int)s.order.size() && s.order.size() > 1)
			fuzzRounds++;
		s = r.s;
	};
	auto userIntent = [&](const std::string& origin) {
		Intent i;
		i.origin = origin;
		i.seq = ++seqs[origin];
		i.basis = origin == "hh" ? s.v : view[origin].v;
		static const IntentType KINDS[] = {IntentType::Open, IntentType::Play, IntentType::Pause, IntentType::Seek,
			IntentType::SeekBy, IntentType::Step, IntentType::Step, IntentType::Step, IntentType::SetAudio, IntentType::SetSub,
			IntentType::SetSpeed, IntentType::SetLoop, IntentType::SetOutput, IntentType::SetOutput, IntentType::Stop,
			IntentType::PhotoPark, IntentType::PhotoResume, IntentType::FileGone, IntentType::SetShuffle, IntentType::Requeue};
		i.type = KINDS[pick((int)(sizeof KINDS / sizeof KINDS[0]))];
		switch (i.type)
		{
		case IntentType::Open:
		{
			int n = 1 + pick(6);
			for (int k = 0; k < n; k++) i.queue.push_back(FILES[pick(7)]);
			i.index = pick(n + 1) - (pick(10) == 0 ? 1 : 0);
			i.t = pick(4) ? 0 : pick(200);
			i.browserCan = pick(5) != 0;
			i.rnd = rr();
			if (pick(2))
			{
				i.hasOut = true;
				int o = pick(3);
				i.out = o == 0 ? handheldOut() : browserOut(TABS[o - 1]);
			}
			break;
		}
		case IntentType::Seek: i.t = pick(400) - 50; break;
		case IntentType::SeekBy: i.t = pick(60) - 30; break;
		case IntentType::Step: i.n = pick(9) - 4; i.browserCan = pick(5) != 0; i.rnd = rr(); break;
		case IntentType::SetAudio: case IntentType::SetSub:
			i.audio = i.sub = {pick(4) - 1, pick(4), "eng", "Track " + std::to_string(pick(3)), false, true};
			break;
		case IntentType::SetSpeed: i.speed = 0.25 * pick(12); break;
		case IntentType::SetLoop: i.loop = pick(4) - (pick(8) == 0 ? 1 : 0); break;
		case IntentType::SetOutput:
		{
			i.hasOut = pick(20) != 0;
			int o = pick(3);
			i.out = o == 0 ? handheldOut() : browserOut(TABS[o - 1]);
			if (pick(2)) { i.hasT = true; i.t = pick(300); }
			break;
		}
		case IntentType::PhotoPark: if (pick(2)) { i.hasT = true; i.t = pick(300); } break;
		case IntentType::SetShuffle: i.on = pick(3) == 0; i.rnd = rr(); break;
		case IntentType::FileGone: i.path = pick(6) ? FILES[pick(7)] : "/m"; break;
		case IntentType::Requeue:
		{
			// the list as a playlist edit leaves it: some gone (never the one playing), some added at the end
			i.source = pick(8) ? s.source : SessionSource{2, "x", "/pl/x.m3u"};
			if (s.index < 0) break;
			for (int k = 0; k < (int)s.queue.size(); k++)
			{
				if (k != s.index && pick(4) == 0) { i.remap.push_back(-1); continue; }
				if (k == s.index) i.index = (int)i.queue.size();
				i.remap.push_back((int)i.queue.size());
				i.queue.push_back(s.queue[k]);
			}
			for (int k = pick(3); k > 0; k--) i.queue.push_back(FILES[pick(7)]);
			if (pick(10) == 0 && !i.remap.empty()) i.remap[0] = (int)i.queue.size();	// (a bad one, now and then)
			break;
		}
		default: break;
		}
		return i;
	};
	// a renderer's report, from what it last saw (a tab's view lags; the handheld's is the session)
	auto rendererReport = [&](const std::string& origin) -> bool {
		const Session& v = origin == "hh" ? s : view[origin];
		if (v.index < 0 || outOrigin(v) != origin) return false;
		Intent i;
		i.origin = origin;
		i.seq = ++seqs[origin];
		i.basis = v.v;
		i.load = v.load;
		int r = pick(22);
		i.type = r < 6 ? IntentType::Loaded : r < 16 ? IntentType::Pos : r < 18 ? IntentType::Ended : r < 20 ? IntentType::CantPlay
			: IntentType::Blocked;
		i.hasT = pick(2);
		i.dur = 30 + pick(300);
		i.audioOnly = pick(2);
		i.t = pick(330);
		i.playing = pick(4) ? v.playing : !v.playing;
		static const char* REASONS[] = {"format", "net", "error", "timeout"};
		i.reason = REASONS[pick(4)];
		i.rnd = rr();
		if (origin == "hh") run(i);
		else net.push_back({i, now + pick(2000)});
		return true;
	};
	auto send = [&](const Intent& i) {
		int r = pick(100);
		if (r < 5) net.push_back({i, now + 3000 + pick(3000)});	// dropped, retried with the same seq later
		else
		{
			net.push_back({i, now + pick(800)});
			if (r < 15) net.push_back({i, now + pick(1500)});	// duplicated
		}
	};
	auto deliver = [&](size_t k) {
		Intent i = net[k].i;
		net.erase(net.begin() + k);
		run(i);
	};
	for (at = 0; at < steps && why.empty(); at++)
	{
		now += pick(300);
		int r = pick(100);
		if (r < 25) run(userIntent("hh"));
		else if (r < 50) send(userIntent(TABS[pick(2)]));
		else if (r < 68)
		{
			const char* ORIGINS[] = {"hh", "tabA", "tabB"};
			rendererReport(ORIGINS[pick(3)]);
		}
		else if (r < 80) { if (!net.empty()) deliver(pick((int)net.size())); }
		else if (r < 88)
		{
			now += pick(15001);
			Intent t;
			run(t);
		}
		else if (r < 93)
		{
			Intent t;
			t.type = pick(2) ? IntentType::TabGone : IntentType::TabBack;
			t.path = TABS[pick(2)];
			run(t);
		}
		else if (r < 95)
		{
			Intent t;
			t.type = pick(2) ? IntentType::Slept : IntentType::Woke;
			run(t);
		}
		else view[TABS[pick(2)]] = s;
		// what's due arrives (in its own order)
		for (size_t k = 0; k < net.size() && why.empty();)
			if (net[k].at <= now && pick(3)) deliver(k);
			else k++;
	}
	// settle: everything delivered, the renderers up to date, time passing: something plays, or it stopped, or it says
	// why it can't
	while (!net.empty() && why.empty()) deliver(pick((int)net.size()));
	for (int round = 0; round < 4 && why.empty(); round++)
	{
		now += 30000;
		run(Intent());
		if (s.index >= 0 && !s.loaded && s.error.empty() && !s.parked)
		{
			Intent i;
			i.type = IntentType::Loaded;
			i.origin = outOrigin(s);
			i.seq = ++seqs[i.origin];
			i.basis = s.v;
			i.load = s.load;
			i.dur = 120;
			run(i);
		}
	}
	if (why.empty() && !(s.index == -1 || s.loaded || !s.error.empty() || s.parked)) why = "I6: stuck after settling";
	if (!why.empty())
	{
		fprintf(stderr, "fuzzSession seed %u: %s\n", seed, why.c_str());
		return false;
	}
	return true;
}

// matchTrack against tests/session_vectors.txt, which web/js/match.js runs too (tools/web-smoke.sh)
static void testMatchVectors()
{
	std::ifstream f(BP_TESTS_DIR "/session_vectors.txt");
	std::string line;
	int n = 0;
	auto split = [](const std::string& s, const std::string& sep) {
		std::vector<std::string> out;
		size_t at = 0;
		for (size_t k; (k = s.find(sep, at)) != std::string::npos; at = k + sep.size()) out.push_back(s.substr(at, k - at));
		out.push_back(s.substr(at));
		return out;
	};
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		auto c = split(line, "\t");
		if (c.size() != 8) { CHECK_EQ(line, std::string("8 columns")); continue; }
		TrackPick k;
		k.set = c[1] == "1";
		k.ord = atoi(c[2].c_str());
		k.of = atoi(c[3].c_str());
		k.lang = c[4];
		k.label = c[5];
		k.external = c[6] == "1";
		std::vector<TrackInfo> list;
		for (auto& t : split(c[7], " ;; "))
		{
			auto x = split(t, "|");
			if (x.size() == 4) list.push_back({x[0], x[1], x[2] == "1", x[3] == "1"});
		}
		CHECK_EQ(matchTrack(k, list), atoi(c[0].c_str()));
		n++;
	}
	CHECK_EQ(n >= 10, true);
}

static void testSessionFuzz()
{
	int runs = 2000;
	if (const char* n = getenv("BP_FUZZ")) runs = std::max(1, atoi(n));
	int bad = 0;
	for (int k = 0; k < runs && bad < 5; k++)
		if (!fuzzSession(1000 + k, 200)) bad++;
	failures += bad;
	// (Shuffle, a lasting mode, makes a list's end rarer: every item once first)
	CHECK_EQ(fuzzEnded > runs / 40, true);
	CHECK_EQ(fuzzShuffled > runs / 20 && fuzzUnshuffled > runs / 40, true);
	CHECK_EQ(fuzzRounds > 0, true);
	CHECK_EQ(fuzzRequeued > 0, true);
}

// picture subtitles for the browser: the cue timing rules, the window a request may ask for, the paletted PNG
static void testPicSubs()
{
	auto ev = [](double s, double e, bool clear, int id) { PicEvent x; x.start = s; x.end = e; x.clear = clear; x.id = id; return x; };
	// PGS: no end times; an empty event ends the one before, the next picture replaces it
	{
		auto c = pictureCues({ev(10, -1, false, 0), ev(13, -1, true, 1), ev(20, -1, false, 2), ev(22, -1, false, 3), ev(25, -1, true, 4)}, 0, 60, 40, false);
		CHECK_EQ(c.size(), (size_t)3);
		CHECK_EQ(c[0].start, 10.0); CHECK_EQ(c[0].end, 13.0); CHECK_EQ(c[0].id, 0);
		CHECK_EQ(c[1].end, 22.0); CHECK_EQ(c[2].end, 25.0); CHECK_EQ(c[2].id, 3);
	}
	// VobSub: its own stop time, cut short by a picture that comes sooner; read out of order, sorted
	{
		auto c = pictureCues({ev(5, 8, false, 1), ev(1, 4, false, 0), ev(7, 9, false, 2)}, 0, 60, 40, false);
		CHECK_EQ(c.size(), (size_t)3);
		CHECK_EQ(c[0].id, 0); CHECK_EQ(c[0].end, 4.0); CHECK_EQ(c[1].end, 7.0); CHECK_EQ(c[2].end, 9.0);
	}
	// no end and nothing after: on to where reading stopped (a later window says more), 5 s at the end of the file
	{
		auto c = pictureCues({ev(50, -1, false, 0)}, 0, 60, 70, false);
		CHECK_EQ(c.size(), (size_t)1); CHECK_EQ(c[0].end, 70.0);
		c = pictureCues({ev(50, -1, false, 0)}, 0, 60, 50, true);
		CHECK_EQ(c[0].end, 55.0);
		// never over maxShow (a VobSub stop of 4294967 s, as ffmpeg's conversions write)
		c = pictureCues({ev(50, 50 + 4294967.295, false, 0)}, 0, 60, 70, false);
		CHECK_EQ(c[0].end, 110.0);
	}
	// the window: a cue showing as it starts is in, one before it or from its end on is out; same time: the last wins
	{
		auto c = pictureCues({ev(5, 12, false, 0), ev(2, 4, false, 1), ev(60, 62, false, 2), ev(30, 31, false, 3), ev(30, 33, false, 4)}, 10, 60, 70, false);
		CHECK_EQ(c.size(), (size_t)2);
		CHECK_EQ(c[0].id, 0); CHECK_EQ(c[1].id, 4);
	}
	double f = 0, t = 0;
	CHECK_EQ(pictureWindow(30, 90, 120, f, t), true); CHECK_EQ(f, 30.0); CHECK_EQ(t, 90.0);
	CHECK_EQ(pictureWindow(-5, 500, 120, f, t), true); CHECK_EQ(f, 0.0); CHECK_EQ(t, 120.0);
	CHECK_EQ(pictureWindow(90, 30, 120, f, t), false);
	CHECK_EQ(pictureWindow(NAN, 30, 120, f, t), false);
	CHECK_EQ(pictureWindow(2e6, 2e6 + 1, 120, f, t), false);

	// the PNG: signature, chunks with good CRCs, a palette with alpha, and the rows back out of IDAT
	{
		const int W = 5, H = 3, STRIDE = 8;
		uint8_t idx[STRIDE * H];
		for (int i = 0; i < STRIDE * H; i++) idx[i] = (uint8_t)(i % 3);
		uint32_t pal[3] = {0x00000000u, 0xffffffffu, 0x80102030u};
		std::string png = pngIndexed(W, H, idx, STRIDE, pal, 3);
		CHECK_EQ(png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)), 0);
		auto be = [&](size_t o) { return (uint32_t)(uint8_t)png[o] << 24 | (uint32_t)(uint8_t)png[o + 1] << 16 | (uint32_t)(uint8_t)png[o + 2] << 8 | (uint8_t)png[o + 3]; };
		std::map<std::string, std::string> chunks;
		std::vector<std::string> order;
		size_t o = 8;
		bool crcOk = true;
		while (o + 12 <= png.size())
		{
			uint32_t n = be(o);
			std::string type = png.substr(o + 4, 4);
			if (crc32(0, (const Bytef*)png.data() + o + 4, n + 4) != be(o + 8 + n)) crcOk = false;
			chunks[type] = png.substr(o + 8, n);
			order.push_back(type);
			o += 12 + n;
		}
		CHECK_EQ(crcOk, true);
		CHECK_EQ(o, png.size());
		CHECK_EQ(order.size(), (size_t)5);
		CHECK_EQ(order.front(), std::string("IHDR")); CHECK_EQ(order.back(), std::string("IEND"));
		CHECK_EQ(chunks["IHDR"].size(), (size_t)13);
		CHECK_EQ((int)(uint8_t)chunks["IHDR"][9], 3);	// paletted
		CHECK_EQ(chunks["PLTE"], std::string("\x00\x00\x00\xff\xff\xff\x10\x20\x30", 9));
		CHECK_EQ(chunks["tRNS"], std::string("\x00\xff\x80", 3));
		std::string raw((W + 1) * H, '\0');
		uLongf n = raw.size();
		CHECK_EQ(uncompress((Bytef*)&raw[0], &n, (const Bytef*)chunks["IDAT"].data(), chunks["IDAT"].size()), Z_OK);
		CHECK_EQ((size_t)n, raw.size());
		bool rows = true;
		for (int y = 0; y < H; y++)
		{
			if (raw[y * (W + 1)] != 0) rows = false;
			for (int x = 0; x < W; x++)
				if ((uint8_t)raw[y * (W + 1) + 1 + x] != idx[y * STRIDE + x]) rows = false;
		}
		CHECK_EQ(rows, true);
		CHECK_EQ(pngIndexed(0, 3, idx, STRIDE, pal, 3), std::string());
		CHECK_EQ(pngIndexed(W, H, idx, 2, pal, 3), std::string());
	}
}

// the Suwayomi GraphQL client: every request body is built and every recorded answer (from tests/fixtures/suwayomi/)
// is parsed, with the same small JSON reader the rest of bpcore uses.
static void testSuwayomi()
{
	// a saved series checked against the engine's own database: title, source and chapter ids
	{
		SuwayomiMangaCheck chk;
		std::string err;
		CHECK_EQ(suwayomiCheckManga(7).find("manga(id: $id)") != std::string::npos, true);
		CHECK_EQ(suwayomiParseCheckManga(R"({"data":{"manga":{"id":7,"title":"Genius","sourceId":"4688509366795716377",)"
			R"("chapters":{"nodes":[{"id":11},{"id":12}]}}}})", chk, err), true);
		CHECK_EQ(chk.title, std::string("Genius"));
		CHECK_EQ(chk.sourceId, std::string("4688509366795716377"));
		CHECK_EQ(chk.chapterIds.size(), (size_t)2);
		CHECK_EQ(chk.chapterIds[1], 12);
		// an id the engine doesn't know: its errors, so no manga
		CHECK_EQ(suwayomiParseCheckManga(R"({"data":null,"errors":[{"message":"Collection is empty."}]})", chk, err), false);
	}
	const std::string F = std::string(BP_TESTS_DIR) + "/fixtures/suwayomi/";
	// a fixture is read into a string; the file is always there (a missing one is a test failure, not a skip)
	auto read = [](const std::string& path) -> std::string
	{
		std::ifstream f(path);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};

	// ---- request bodies: each is a JSON string the caller POSTs to /api/graphql ----

	// addExtensionStore: the index url is a variable; the query names the mutation, and the variable is
	// escaped (a url with quotes or backslashes would otherwise break the body)
	std::string add = suwayomiAddExtensionStore("https://github.com/keiyoushi/extensions/raw/repo/index.pb");
	CHECK_EQ(add.find("\"addExtensionStore\"") != std::string::npos || add.find("addExtensionStore") != std::string::npos, true);
	CHECK_EQ(add.find("keiyoushi") != std::string::npos, true);
	std::string remove = suwayomiRemoveExtensionStore("https://github.com/keiyoushi/extensions/raw/repo/index.pb");
	CHECK_EQ(remove.find("removeExtensionStore") != std::string::npos, true);
	// install / update / uninstall: the same mutation name, different isInstalled flags in the body
	std::string inst = suwayomiInstallExtension("eu.kanade.tachiyomi.extension.all.mangaplus");
	CHECK_EQ(inst.find("eu.kanade.tachiyomi.extension.all.mangaplus") != std::string::npos, true);
	CHECK_EQ(inst.find("isInstalled") != std::string::npos, true);
	std::string upd = suwayomiUpdateExtension("eu.kanade.tachiyomi.extension.all.mangaplus");
	CHECK_EQ(upd.find("updateExtension") != std::string::npos, true);
	std::string unin = suwayomiUninstallExtension("eu.kanade.tachiyomi.extension.all.mangaplus");
	CHECK_EQ(unin.find("isInstalled") != std::string::npos, true);
	// the catalogue queries: popular / latest / search, with the source id and page sent as variables
	std::string pop = suwayomiFetchSourceMangaPopular("1998944621602463790", 1);
	CHECK_EQ(pop.find("POPULAR") != std::string::npos, true);
	CHECK_EQ(pop.find("1998944621602463790") != std::string::npos, true);
	std::string lat = suwayomiFetchSourceMangaLatest("1998944621602463790", 1);
	CHECK_EQ(lat.find("type: LATEST") != std::string::npos, true);
	// what Suwayomi v2.4's schema requires, checked against a live server: these are mutations, an extension is
	// changed by id + patch, a chapter's pages by chapterId, the enum is LATEST, the filters a union (name per type)
	CHECK_EQ(suwayomiFetchExtensions().find("mutation { fetchExtensions(input: {})") != std::string::npos, true);
	CHECK_EQ(inst.find("patch: {install: true}") != std::string::npos, true);
	CHECK_EQ(upd.find("patch: {update: true}") != std::string::npos, true);
	CHECK_EQ(unin.find("patch: {uninstall: true}") != std::string::npos, true);
	CHECK_EQ(pop.rfind("{\"query\":\"mutation", 0) == 0, true);
	CHECK_EQ(lat.find("LATEST_UPDATES") == std::string::npos, true);
	CHECK_EQ(suwayomiFetchChapterPages(6).find("chapterId: $id") != std::string::npos, true);
	CHECK_EQ(suwayomiFetchMangaAndChapters(1).find("fetchManga: true, fetchChapters: true") != std::string::npos, true);
	CHECK_EQ(suwayomiFetchSourceFilters("1").find("filters { __typename name") == std::string::npos, true);
	// a search body carries the query text, escaped; a "&" in the query is not the JSON separator
	std::string srch = suwayomiFetchSourceMangaSearch("1998944621602463790", "one & piece", 1, "[]");
	CHECK_EQ(srch.find("SEARCH") != std::string::npos, true);
	CHECK_EQ(srch.find("one ") != std::string::npos, true);
	// the source id in the request is a string, never a number (the 64-bit overflow rule)
	CHECK_EQ(pop.find("\"1998944621602463790\"") != std::string::npos, true);

	// ---- parsers: each fixture is a real Suwayomi v2.4.2366 answer ----

	std::string error;

	// the GraphQL error check: a body with errors is caught; a good body is not flagged
	CHECK_EQ(suwayomiCheckErrors("{\"errors\":[{\"message\":\"not found\"}]}", error), true);
	CHECK_EQ(error, std::string("not found"));
	CHECK_EQ(suwayomiCheckErrors("{\"data\":{\"hello\":\"world\"}}", error), false);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(suwayomiCheckErrors("not json", error), false);

	// add-store.json: the store was created; the parser sees a data.addExtensionStore path and nothing else
	std::string addStore = read(F + "add-store.json");
	CHECK_EQ(suwayomiParseAddExtensionStore(addStore, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(suwayomiParseAddExtensionStore("{}", error), false);
	CHECK_EQ(suwayomiParseAddExtensionStore("{\"errors\":[{\"message\":\"store exists\"}]}", error), false);
	CHECK_EQ(error, std::string("store exists"));

	// extensions.json: the full extension list
	std::string extsJson = read(F + "extensions.json");
	std::vector<SuwayomiExt> exts;
	CHECK_EQ(suwayomiParseExtensions(extsJson, exts, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(exts.empty(), false);
	// the MANGA Plus extension is in the list (the fixture has it)
	bool foundMp = false;
	for (const SuwayomiExt& e : exts)
		if (e.pkgName == "eu.kanade.tachiyomi.extension.all.mangaplus")
		{
			foundMp = true;
			CHECK_EQ(e.name, std::string("MANGA Plus by SHUEISHA"));
			CHECK_EQ(e.lang, std::string("all"));
			CHECK_EQ(e.versionName, std::string("1.6.68"));
			CHECK_EQ(e.versionCode, (int64_t)106068);
			CHECK_EQ(e.isNsfw, false);
			CHECK_EQ(e.isInstalled, false);
			CHECK_EQ(e.iconUrl, std::string("/api/v1/extension/icon/eu.kanade.tachiyomi.extension.all.mangaplus"));
		}
	CHECK_EQ(foundMp, true);
	// a malformed answer isn't usable
	CHECK_EQ(suwayomiParseExtensions("not json", exts, error), false);

	// install.json: the sources an install returns; ids are strings (the 64-bit overflow rule)
	std::string instJson = read(F + "install.json");
	std::vector<SuwayomiSource> sources;
	CHECK_EQ(suwayomiParseInstallResult(instJson, sources, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(sources.size(), (size_t)9);
	// the first source (MANGA Plus EN) carries the id as a string; the int 1998944621602463790 overflows a double,
	// so it must come out as the exact text the server wrote
	CHECK_EQ(sources[0].id, std::string("1998944621602463790"));
	CHECK_EQ(sources[0].name, std::string("MANGA Plus by SHUEISHA"));
	CHECK_EQ(sources[0].lang, std::string("en"));
	CHECK_EQ(sources[0].displayName, std::string("MANGA Plus by SHUEISHA (EN)"));
	CHECK_EQ(sources[0].supportsLatest, true);
	CHECK_EQ(sources[0].isNsfw, false);
	// the last one (DE): different id, same name, different lang
	CHECK_EQ(sources[8].lang, std::string("de"));
	CHECK_EQ(sources[8].id, std::string("1893513843840146580"));

	// filters-mangaplus.json: the source's filter definitions, onto the MangaFilter structs the sheet uses
	std::string filtJson = read(F + "filters-mangaplus.json");
	std::vector<MangaFilter> defs;
	CHECK_EQ(suwayomiParseFilters(filtJson, defs, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(defs.size(), (size_t)2);	// Status and Genre, both SelectFilter
	CHECK_EQ(defs[0].type, std::string("select"));
	CHECK_EQ(defs[0].name, std::string("Status"));
	CHECK_EQ(defs[0].options.size(), (size_t)3);	// Serializing, Completed, One-shot
	CHECK_EQ(defs[0].options[0].name, std::string("Serializing"));
	CHECK_EQ(defs[0].options[1].name, std::string("Completed"));
	// the default is index 0, which maps to the first option's id
	CHECK_EQ(defs[0].textDefault, std::string("Serializing"));
	CHECK_EQ(defs[1].name, std::string("Genre"));
	CHECK_EQ(defs[1].options.size(), (size_t)16);
	CHECK_EQ(defs[1].options[1].name, std::string("Battle / Action"));

	// the reverse: the sheet's chosen values onto the Suwayomi filter input list
	std::vector<MangaFilterValue> vals = mangaFilterDefaults(defs);
	vals[0].select = "Completed";	// index 1
	vals[1].select = "Romance";	// index 5 (All=0, Battle/Action=1, Comedy=2, Sport=3, Mystery=4, Romance=5)
	std::string inputs = suwayomiFilterInputsJson(defs, vals);
	// the input is a JSON array; each entry has a position and the right state field for its type
	CHECK_EQ(inputs.find("\"position\":0") != std::string::npos, true);
	CHECK_EQ(inputs.find("\"selectState\":1") != std::string::npos, true);	// Status = Completed = index 1
	CHECK_EQ(inputs.find("\"position\":1") != std::string::npos, true);
	CHECK_EQ(inputs.find("\"selectState\":5") != std::string::npos, true);	// Genre = Romance = index 5 (All Battle Comedy Sport Mystery Romance)

	// popular.json: one page of a source's popular catalogue
	std::string popJson = read(F + "popular.json");
	std::vector<SuwayomiManga> mangas;
	bool hasMore = true;
	CHECK_EQ(suwayomiParseSourceManga("http://127.0.0.1:4567", popJson, mangas, hasMore, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(mangas.empty(), false);
	CHECK_EQ(hasMore, false);	// the fixture says hasNextPage:false
	CHECK_EQ(mangas[0].id, 1);
	CHECK_EQ(mangas[0].title, std::string("One Piece"));
	// the thumbnail url is made absolute against the base url the caller gave
	CHECK_EQ(mangas[0].thumbnailUrl, std::string("http://127.0.0.1:4567/api/v1/manga/1/thumbnail"));

	// search.json: same shape, different results
	std::string srchJson = read(F + "search.json");
	std::vector<SuwayomiManga> srchMangas;
	CHECK_EQ(suwayomiParseSourceManga("http://127.0.0.1:4567", srchJson, srchMangas, hasMore, error), true);
	CHECK_EQ(srchMangas.empty(), false);
	// the search results include One Piece and a few titles that contain the search term
	bool foundOp = false;
	for (const SuwayomiManga& m : srchMangas)
		if (m.title == "One Piece") foundOp = true;
	CHECK_EQ(foundOp, true);

	// manga-chapters.json: a manga's details and its chapters, ordered for the series page
	std::string mcJson = read(F + "manga-chapters.json");
	SuwayomiMangaDetails manga;
	std::vector<SuwayomiChapter> chapters;
	CHECK_EQ(suwayomiParseMangaAndChapters("http://127.0.0.1:4567", mcJson, manga, chapters, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(manga.id, 1);
	CHECK_EQ(manga.title, std::string("One Piece"));
	CHECK_EQ(manga.author, std::string("Eiichiro Oda"));
	CHECK_EQ(manga.status, std::string("ONGOING"));
	CHECK_EQ(manga.thumbnailUrl, std::string("http://127.0.0.1:4567/api/v1/manga/1/thumbnail"));
	CHECK_EQ(manga.realUrl, std::string("https://mangaplus.shueisha.co.jp/titles/100020"));
	CHECK_EQ(manga.genre.size() >= (size_t)2, true);
	CHECK_EQ(chapters.empty(), false);
	// chapters are ordered by sourceOrder ascending (the first one in the fixture has sourceOrder:1)
	CHECK_EQ(chapters[0].id, 1);
	CHECK_EQ(chapters[0].name.find("Romance Dawn") != std::string::npos, true);
	CHECK_EQ(chapters[0].chapterNumber, 1.0);
	CHECK_EQ(chapters[0].scanlator, std::string("MANGA Plus"));
	CHECK_EQ(chapters[0].isRead, false);

	// pages.json: the page paths, each made absolute
	std::string pgJson = read(F + "pages.json");
	std::vector<std::string> pages;
	CHECK_EQ(suwayomiParsePages("http://127.0.0.1:4567", pgJson, pages, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(pages.size(), (size_t)17);
	CHECK_EQ(pages[0], std::string("http://127.0.0.1:4567/api/v1/manga/1/chapter/6/page/0"));
	CHECK_EQ(pages[16], std::string("http://127.0.0.1:4567/api/v1/manga/1/chapter/6/page/16"));

	// filters-mangadex.json: groups, and a tri state child. A Suwayomi tri state is one filter with its own
	// value, not a list: it becomes a one-option tri row, and its default comes from triDefault.
	std::string mdFiltJson = read(F + "filters-mangadex.json");
	std::vector<MangaFilter> mdDefs;
	CHECK_EQ(suwayomiParseFilters(mdFiltJson, mdDefs, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(mdDefs.size(), (size_t)12);
	CHECK_EQ(mdDefs[8].type, std::string("group"));
	CHECK_EQ(mdDefs[8].name, std::string("Content"));
	CHECK_EQ(mdDefs[8].children.size(), (size_t)2);
	CHECK_EQ(mdDefs[8].children[0].type, std::string("tri"));
	CHECK_EQ(mdDefs[8].children[0].name, std::string("Gore"));
	CHECK_EQ(mdDefs[8].children[0].options.size(), (size_t)1);
	std::vector<MangaFilterValue> mdVals = mangaFilterDefaults(mdDefs);
	CHECK_EQ(mdVals[8].children[0].tri.size(), (size_t)1);
	CHECK_EQ(mdVals[8].children[0].tri[0], 0);	// triDefault IGNORE
	mdVals[8].children[0].tri[0] = 2;			// exclude
	mdVals[5].sortBy = mdDefs[5].options[5].id;	// Relevance, as the fixture's sortDefault says
	mdVals[5].sortAsc = false;
	std::string mdInputs = suwayomiFilterInputsJson(mdDefs, mdVals);
	// a group child is the group's position with a nested groupChange; its own position is inside the group
	CHECK_EQ(mdInputs.find("\"position\":8,\"groupChange\":{\"position\":0,\"triState\":\"EXCLUDE\"}") != std::string::npos, true);
	// the sort left at the source's own default isn't sent (only what differs is: the engine starts from the defaults)
	CHECK_EQ(mdInputs.find("\"position\":5,") == std::string::npos, true);
	mdVals[5].sortAsc = true;
	CHECK_EQ(suwayomiFilterInputsJson(mdDefs, mdVals).find("\"position\":5,\"sortState\":{\"index\":5,\"ascending\":true}") != std::string::npos, true);
	// a group inside a group nests one groupChange further
	MangaFilter outer;
	outer.id = "outer";
	outer.type = "group";
	MangaFilter inner;
	inner.id = "inner";
	inner.type = "group";
	MangaFilter leaf;
	leaf.id = "leaf";
	leaf.type = "tri";
	leaf.options.push_back({"tri", "Leaf"});
	inner.children.push_back(leaf);
	outer.children.push_back(inner);
	std::vector<MangaFilter> nestedDefs{outer};
	std::vector<MangaFilterValue> nestedVals = mangaFilterDefaults(nestedDefs);
	nestedVals[0].children[0].children[0].tri[0] = 1;
	std::string nestedInputs = suwayomiFilterInputsJson(nestedDefs, nestedVals);
	CHECK_EQ(nestedInputs.find("\"position\":0,\"groupChange\":{\"position\":0,\"groupChange\":{\"position\":0,\"triState\":\"INCLUDE\"}}") != std::string::npos, true);

	// filtered-group.json: a search with the group changes above applied; the page still says there is more
	std::string groupJson = read(F + "filtered-group.json");
	std::vector<SuwayomiManga> groupMangas;
	bool groupMore = false;
	CHECK_EQ(suwayomiParseSourceManga("http://127.0.0.1:4567", groupJson, groupMangas, groupMore, error), true);
	CHECK_EQ(groupMore, true);
	CHECK_EQ(groupMangas.size(), (size_t)20);
	CHECK_EQ(groupMangas[0].id, 323);

	// a GraphQL error in a body that otherwise looks right is caught before the data is read
	CHECK_EQ(suwayomiParseExtensions("{\"errors\":[{\"message\":\"no extensions\"}]}", exts, error), false);
	CHECK_EQ(error, std::string("no extensions"));
}

// the Java extensions' store: a repository URL, the store's answer, an install's sources, and their persistence
static void testMangaJava()
{
	const std::string F = std::string(BP_TESTS_DIR) + "/fixtures/suwayomi/";
	auto read = [](const std::string& path) -> std::string
	{
		std::ifstream f(path);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};
	// a Tachiyomi index is index.pb or index.min.json; a Lua repository's index.json is not one
	CHECK_EQ(MangaJava::isRepoUrl("https://github.com/keiyoushi/extensions/raw/repo/index.pb"), true);
	CHECK_EQ(MangaJava::isRepoUrl("https://example.org/mihon/index.min.json"), true);
	CHECK_EQ(MangaJava::isRepoUrl("https://example.org/repo/index.json"), false);
	CHECK_EQ(MangaJava::isRepoUrl("not a url"), false);
	CHECK_EQ(MangaJava::sourceId("123"), std::string("java:123"));
	CHECK_EQ(MangaJava::suwayomiSourceId("java:123"), std::string("123"));

	char tmpl[] = "/tmp/bp-mangajava-XXXXXX";
	char* dir = mkdtemp(tmpl);
	CHECK_EQ(dir != nullptr, true);
	if (!dir) return;

	MangaJava m;
	std::vector<std::string> bodies;
	uint64_t next = 1, last = 0;
	m.setSubmit([&](const std::string& body) { bodies.push_back(body); last = next++; return last; });
	CHECK_EQ(m.load(dir), true);
	std::string error;
	const std::string url = "https://github.com/keiyoushi/extensions/raw/repo/index.pb";
	CHECK_EQ(m.addRepo(url, error), true);
	CHECK_EQ(error, std::string(""));
	CHECK_EQ(bodies.size(), (size_t)1);
	CHECK_EQ(bodies[0].find("addExtensionStore") != std::string::npos, true);
	CHECK_EQ(m.repos().size(), (size_t)1);
	CHECK_EQ(m.repos()[0].added, false);	// not written until the store answers
	CHECK_EQ(m.onAnswer(last, read(F + "add-store.json"), ""), true);	CHECK_EQ(m.repos()[0].name, std::string("Keiyoushi"));
	CHECK_EQ(m.repos()[0].added, true);
	{
		auto ev = m.takeEvents();
		CHECK_EQ(ev.size(), (size_t)1);
		CHECK_EQ(ev[0].kind == MangaJava::Event::RepoAdded, true);
		CHECK_EQ(ev[0].message, std::string("Keiyoushi"));
	}

	// install: the answer's sources become this package's sources (all nine language rows)
	const std::string pkg = "eu.kanade.tachiyomi.extension.all.mangaplus";
	CHECK_EQ(m.install(pkg, error), true);
	CHECK_EQ(m.busy(pkg), true);
	CHECK_EQ(m.onAnswer(last, read(F + "install.json"), ""), true);
	CHECK_EQ(m.busy(pkg), false);
	CHECK_EQ(m.sources().size(), (size_t)9);
	CHECK_EQ(m.source("java:1998944621602463790") != nullptr, true);
	CHECK_EQ(m.source("java:1998944621602463790")->lang, std::string("en"));
	CHECK_EQ(m.source("java:1998944621602463790")->pkgName, pkg);
	CHECK_EQ(m.source("java:1998944621602463790")->title(), std::string("MANGA Plus by SHUEISHA (EN)"));
	CHECK_EQ(m.extension(pkg) != nullptr, true);
	CHECK_EQ(m.extension(pkg)->isInstalled, true);
	{
		auto ev = m.takeEvents();
		CHECK_EQ(ev.size(), (size_t)1);
		CHECK_EQ(ev[0].kind == MangaJava::Event::Installed, true);
	}

	// save and load: the store's name and the sources stay (the extensions list itself is asked again)
	CHECK_EQ(m.save(), true);
	MangaJava m2;
	m2.setSubmit([&](const std::string&) { last = ++next; return last; });
	CHECK_EQ(m2.load(dir), true);
	CHECK_EQ(m2.repos().size(), (size_t)1);
	CHECK_EQ(m2.repos()[0].name, std::string("Keiyoushi"));
	CHECK_EQ(m2.sources().size(), (size_t)9);

	// uninstall: the answer need not carry a source list; its package's sources go
	CHECK_EQ(m2.uninstall(pkg, error), true);
	CHECK_EQ(m2.onAnswer(last, "{\"data\":{\"updateExtension\":{\"extension\":{\"pkgName\":\"" + pkg +
		"\",\"isInstalled\":false}}}}", ""), true);
	CHECK_EQ(m2.sources().empty(), true);

	// remove: removeExtensionStore is sent, and the row goes at once
	CHECK_EQ(m2.removeRepo(url, error), true);
	CHECK_EQ(m2.repos().empty(), true);
	CHECK_EQ(bodies.size(), (size_t)2);	// the add store and the install; m2's submit is its own
}

// the Java runtime's archive: a .tar.gz unpacked with only the rules the pinned one needs (docs/design/manga.md 6)
static void tarField(char* dst, size_t n, const std::string& s)
{
	size_t len = std::min(n, s.size());
	memcpy(dst, s.data(), len);
}

static std::string tarHeader(const std::string& name, unsigned mode, uint64_t size, char type,
	const std::string& link = "", const std::string& prefix = "")
{
	char h[512];
	memset(h, 0, sizeof h);
	tarField(h, 100, name);
	snprintf(h + 100, 8, "%07o", mode & 07777);
	snprintf(h + 108, 8, "%07o", 0);
	snprintf(h + 116, 8, "%07o", 0);
	snprintf(h + 124, 12, "%011llo", (unsigned long long)size);
	snprintf(h + 136, 12, "%011o", 0);
	h[156] = type;
	tarField(h + 157, 100, link);
	memcpy(h + 257, "ustar", 6);
	h[262] = '0';
	h[263] = '0';
	tarField(h + 265, 32, "root");
	tarField(h + 297, 32, "root");
	tarField(h + 345, 155, prefix);
	memset(h + 148, ' ', 8);
	unsigned sum = 0;
	for (size_t i = 0; i < sizeof h; i++) sum += (unsigned char)h[i];
	snprintf(h + 148, 8, "%06o", sum);
	h[154] = '\0';
	h[155] = ' ';
	return std::string(h, sizeof h);
}

static void tarAdd(std::string& tar, const std::string& name, char type, const std::string& data,
	unsigned mode = 0644, const std::string& link = "", const std::string& prefix = "")
{
	tar += tarHeader(name, mode, data.size(), type, link, prefix);
	if (!data.empty())
	{
		tar += data;
		tar.append((512 - data.size() % 512) % 512, '\0');
	}
}

static bool gzWriteFile(const std::string& path, const std::string& data)
{
	gzFile g = gzopen(path.c_str(), "wb");
	if (!g) return false;
	bool ok = data.empty() || gzwrite(g, data.data(), (unsigned)data.size()) == (int)data.size();
	return gzclose(g) == Z_OK && ok;
}

static std::string readWhole(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void testTarX()
{
	char tmpl[] = "/tmp/bp-tarx-XXXXXX";
	char* made = mkdtemp(tmpl);
	if (!made) { failures++; return; }
	std::string root = made, dest = root + "/out";

	std::string tar;
	tarAdd(tar, "top", '5', "", 0755);
	tarAdd(tar, "hello.txt", '0', "hello\n", 0644, "", "top");	// ustar's prefix field
	tarAdd(tar, "run.sh", '0', "#!/bin/sh\n", 0755, "", "top");
	// a name over 100 characters comes in a GNU long-name entry before its own header
	std::string longName = "top/" + std::string(120, 'a') + ".txt";
	std::string longData = longName;
	longData.push_back('\0');
	tarAdd(tar, "././@LongLink", 'L', longData);
	tarAdd(tar, "short", '0', "long", 0644);
	// symlinks and hard links are skipped, never made: the card is exFAT
	tarAdd(tar, "link", '2', "", 0777, "hello.txt", "top");
	tarAdd(tar, "hard", '1', "", 0644, "hello.txt", "top");
	tar.append(1024, '\0');
	std::string archive = root + "/a.tar.gz", why;
	CHECK_EQ(gzWriteFile(archive, tar), true);
	CHECK_EQ(extractTarGz(archive, dest, why), true);
	CHECK_EQ(why.empty(), true);
	CHECK_EQ(readWhole(dest + "/top/hello.txt"), std::string("hello\n"));
	CHECK_EQ(readWhole(dest + "/top/run.sh"), std::string("#!/bin/sh\n"));
	CHECK_EQ(readWhole(dest + "/" + longName), std::string("long"));
	struct stat st;
	CHECK_EQ(stat((dest + "/top/run.sh").c_str(), &st), 0);
	CHECK_EQ((st.st_mode & 0111) != 0, true);	// the archive's executable bit
	CHECK_EQ(stat((dest + "/top/hello.txt").c_str(), &st), 0);
	CHECK_EQ((st.st_mode & 0111) == 0, true);
	CHECK_EQ(access((dest + "/top/link").c_str(), F_OK), -1);
	CHECK_EQ(access((dest + "/top/hard").c_str(), F_OK), -1);

	// a path that climbs out is refused, not clipped, and nothing lands beside the folder
	std::string evilTar;
	tarAdd(evilTar, "../escape.txt", '0', "evil", 0644);
	evilTar.append(1024, '\0');
	std::string evilArchive = root + "/evil.tar.gz", evilDest = root + "/evil";
	CHECK_EQ(gzWriteFile(evilArchive, evilTar), true);
	CHECK_EQ(extractTarGz(evilArchive, evilDest, why), false);
	CHECK_EQ(why.empty(), false);
	CHECK_EQ(access((root + "/escape.txt").c_str(), F_OK), -1);

	// an absolute path is not under the folder either
	std::string absTar;
	tarAdd(absTar, "/abs.txt", '0', "abs", 0644);
	absTar.append(1024, '\0');
	std::string absArchive = root + "/abs.tar.gz", absDest = root + "/abs";
	CHECK_EQ(gzWriteFile(absArchive, absTar), true);
	CHECK_EQ(extractTarGz(absArchive, absDest, why), false);

	unlink((dest + "/top/hello.txt").c_str());
	unlink((dest + "/top/run.sh").c_str());
	unlink((dest + "/" + longName).c_str());
	rmdir((dest + "/top").c_str());
	rmdir(dest.c_str());
	rmdir(evilDest.c_str());
	rmdir(absDest.c_str());
	unlink(archive.c_str());
	unlink(evilArchive.c_str());
	unlink(absArchive.c_str());
	rmdir(root.c_str());
}

// an image's size from its header (the reader's strip sizes a long page's place before decoding it)
static void testImageSize()
{
	for (const char* ext : {"jpg", "png", "gif"})
	{
		int w = 0, h = 0;
		std::string path = std::string(BP_TESTS_DIR) + "/fixtures/imagesize/tall." + ext;
		CHECK_EQ(imageSizeFile(path, w, h), true);
		CHECK_EQ(w, 720);
		CHECK_EQ(h, 9000);
	}
	int w = 0, h = 0;
	// a WebP's extended header (VP8X): width-1 and height-1 as 24-bit little-endian numbers
	uint8_t webp[30] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', 'X', 10, 0, 0, 0, 0, 0, 0, 0};
	webp[24] = (719) & 0xff; webp[25] = (719 >> 8) & 0xff; webp[26] = 0;
	webp[27] = (8999) & 0xff; webp[28] = (8999 >> 8) & 0xff; webp[29] = 0;
	CHECK_EQ(imageSize(webp, sizeof webp, w, h), true);
	CHECK_EQ(w, 720);
	CHECK_EQ(h, 9000);
	const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30};
	CHECK_EQ(imageSize(junk, sizeof junk, w, h), false);
	CHECK_EQ(imageSizeFile("/nonexistent/x.jpg", w, h), false);
}

int main()
{
	testImageSize();
	testPicSubs();
	testTarX();
	testSessionNamed();
	testMatchVectors();
	testSessionFuzz();
	testCrashLog();
	testWeb();
	testChapters();
	testSpeed();
	testSubStyle();
	testSettings();
	testEq();
	testSoundFx();
	testResume();
	testPlaylist();
	testPlaylists();
	testPhoto();
	testUpdate();
	testBacklight();
	testKeyboard();
	testNet();
	testFtpList();
	testBrowser();
	testHistory();
	testClock();
	testBattery();
	testAspect();
	testTime();
	testDurations();
	testMangaNames();
	testReaderFetchPlan();
	testSeekRun();
	testReaderFetchTargetFirst();
	testBrowserCheck();
	testPublicAddress();
	testDohQuery();
	testDohParseCname();
	testDohParseNoRecords();
	testDohParseNxdomain();
	testDohParseTruncated();
	testDohCache();
	testDohWantedFor();
	testMangaRemote();
	testMangaServers();
	testMangaKavitaKey();
	testHtmlDoc();
	testPageCache();
	testCoverCache();
	testDlQueue();
	testMangaRepo();
	testOpds();
	testCbzWrite();
	testMangaUiLogic();
	testMangaSeriesCache();
	testMangaFilters();
	testPaged();
	testMangaStore();
	testMangaLocal();
	testMangaNetFolders();
	testComicInfo();
	testPageDecode();
	testBands();
	testCbz();
	testStrip();
	testLabels();
	testTrackList();
	testSuwayomi();
	testMangaJava();
	if (failures) fprintf(stderr, "%d failed\n", failures);
	else printf("core: all passed\n");
	return failures != 0;
}
