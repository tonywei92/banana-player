// The parts without a window: aspect modes, track labels, mpv's track-list, times.
#include "aspect.h"
#include "battery.h"
#include "browser.h"
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
#include "netpath.h"
#include "secret.h"
#include "servers.h"
#include "sidecar.h"
#include "soundfx.h"
#include "resume.h"
#include "session.h"
#include "settings.h"
#include "tracks.h"
#include "json.h"
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
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
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
	CHECK_EQ(allowedFileType("a.mp3"), true);
	CHECK_EQ(allowedFileType("A.MKV"), true);
	CHECK_EQ(allowedFileType("a.jpg"), true);
	CHECK_EQ(allowedFileType("cover.PNG"), true);
	for (const char* e : {"srt", "ass", "ssa", "vtt", "sub", "idx", "lrc", "SRT"})
		CHECK_EQ(allowedFileType(std::string("x.") + e), true);
	for (const char* n : {"x.exe", "x.sh", "x.txt", "x.m3u", "x", "x.", ".mp3x", "a.mp3.exe", "x.html", "x.zip"})
		CHECK_EQ(allowedFileType(n), false);

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
			"\"clients\":1,\"session\":{\"boot\":0,\"v\":0},\"playlistSerial\":0,\"liked\":false,\"showHiddenFolders\":false}"));

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

int main()
{
	testPicSubs();
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
	testLabels();
	testTrackList();
	if (failures) fprintf(stderr, "%d failed\n", failures);
	else printf("core: all passed\n");
	return failures != 0;
}
