#include "cbz.h"
#include "comicinfo.h"
#include "dlqueue.h"
#include "manga_names.h"
#include "mangarepo.h"
#include "mangastore.h"
#include "opds.h"
#include "pagecache.h"
#include "pagedecode.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace
{
	using Clock = std::chrono::steady_clock;
	using Seeds = std::vector<std::string>;
	constexpr size_t MAX_MUTANT = 4 * 1024 * 1024;

	struct Result
	{
		int accepted = 0;
		int slow = 0;
	};

	std::string readFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	bool writeFile(const std::string& path, const std::string& data)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out.write(data.data(), static_cast<std::streamsize>(data.size()));
		return static_cast<bool>(out);
	}

	std::string chooseSeed(const Seeds& seeds, std::mt19937_64& rng)
	{
		return seeds[static_cast<size_t>(rng() % seeds.size())];
	}

	void mutate(std::string& data, const std::string& other, int operation, int run, std::mt19937_64& rng)
	{
		static const std::array<unsigned char, 10> interesting = {
			0x00, 0xFF, 0x7F, 0x80, '<', '>', '&', '"', '\t', '\n'
		};
		switch (operation)
		{
		case 0: // Flip bits to disturb structural fields without replacing the whole input.
			if (!data.empty())
			{
				int count = 1 + static_cast<int>(rng() % 8);
				while (count--)
				{
					size_t at = static_cast<size_t>(rng() % data.size());
					data[at] = static_cast<char>(static_cast<unsigned char>(data[at]) ^ (1u << (rng() % 8)));
				}
			}
			break;
		case 1: // XML, JSON and TSV delimiters matter as much as arbitrary binary values.
			if (!data.empty())
			{
				int count = 1 + static_cast<int>(rng() % 8);
				while (count--)
					data[static_cast<size_t>(rng() % data.size())] = static_cast<char>(interesting[rng() % interesting.size()]);
			}
			break;
		case 2: // Sample every 1/50th boundary over repeated deterministic runs.
			if (!data.empty())
			{
				size_t part = static_cast<size_t>((run / 7) % 50 + 1);
				data.resize(data.size() * part / 50);
			}
			break;
		case 3: // Repeat a bounded chunk to stress parsers' length and repetition handling.
			if (!data.empty() && data.size() < MAX_MUTANT)
			{
				size_t start = static_cast<size_t>(rng() % data.size());
				size_t length = std::min<size_t>(1 + rng() % 4096, data.size() - start);
				length = std::min(length, MAX_MUTANT - data.size());
				data.insert(start, data.substr(start, length));
			}
			break;
		case 4: // Long repeated runs exercise oversized lengths and delimiter scans.
			if (data.size() < MAX_MUTANT)
			{
				size_t length = std::min<size_t>(1 + rng() % 4096, MAX_MUTANT - data.size());
				char value = static_cast<char>(interesting[rng() % interesting.size()]);
				size_t at = data.empty() ? 0 : static_cast<size_t>(rng() % (data.size() + 1));
				data.insert(at, length, value);
			}
			break;
		case 5: // Splice independently valid documents so either side may look plausible.
			if (data.size() + other.size() <= MAX_MUTANT)
			{
				size_t left = data.empty() ? 0 : static_cast<size_t>(rng() % (data.size() + 1));
				size_t right = other.empty() ? 0 : static_cast<size_t>(rng() % (other.size() + 1));
				data = data.substr(0, left) + other.substr(right);
			}
			break;
		default:
			break;
		}
	}

	std::string mutant(const Seeds& seeds, int run, std::mt19937_64& rng)
	{
		std::string data = chooseSeed(seeds, rng);
		std::string other = chooseSeed(seeds, rng);
		if (seeds.size() > 1 && other == data)
			other = seeds[(static_cast<size_t>(rng() % (seeds.size() - 1)) + 1) % seeds.size()];
		mutate(data, other, run % 6, run, rng);
		int extra = static_cast<int>(rng() % 3);
		while (extra-- && data.size() < MAX_MUTANT)
			mutate(data, other, static_cast<int>(rng() % 6), run, rng);
		return data;
	}

	template <typename Call>
	void timed(const std::string& parser, int run, Result& result, Call call)
	{
		auto start = Clock::now();
		try
		{
			if (call()) result.accepted++;
		}
		catch (const std::exception& e)
		{
			std::fprintf(stderr, "%s: run %d threw: %s\n", parser.c_str(), run, e.what());
		}
		catch (...)
		{
			std::fprintf(stderr, "%s: run %d threw an unknown exception\n", parser.c_str(), run);
		}
		double seconds = std::chrono::duration<double>(Clock::now() - start).count();
		if (seconds > 2.0)
		{
			result.slow++;
			std::fprintf(stderr, "%s: run %d took %.3f s\n", parser.c_str(), run, seconds);
		}
	}

	void summary(const std::string& name, int runs, const Result& result)
	{
		std::printf("%s: %d runs, %d accepted, %d slow\n", name.c_str(), runs, result.accepted, result.slow);
	}

	std::string temporaryRoot()
	{
		char name[] = "/tmp/bp-fuzz-XXXXXX";
		char* path = mkdtemp(name);
		return path ? std::string(path) : std::string();
	}

	void runCbz(int runs, std::mt19937_64& rng, const std::string& root)
	{
		Seeds seeds;
		for (const char* name : {"stored.cbz", "deflated.cbz", "nested.cbz"})
			seeds.push_back(readFile(std::string(BP_FUZZ_DIR) + "/" + name));
		Result result;
		std::string path = root + "/cbz.cbz";
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			writeFile(path, data);
			timed("cbz", i, result, [&]() {
				bp::Cbz archive;
				if (!archive.open(path)) return false;
				for (const auto& entry : archive.entries())
				{
					std::vector<uint8_t> bytes;
					archive.read(entry.name, bytes);
				}
				return true;
			});
		}
		summary("cbz", runs, result);
	}

	void runComicInfo(int runs, std::mt19937_64& rng)
	{
		Seeds seeds = {
			"<?xml version=\"1.0\"?><ComicInfo><Title>Lantern &amp; Moon</Title><Series>Ember</Series><Number>12.5</Number><Volume>2</Volume><PageCount>24</PageCount><Writer>Studio</Writer></ComicInfo>",
			"<ComicInfo><Series>North</Series><Title>Quiet Hours</Title><Manga>YesAndRightToLeft</Manga><Web>https://example.org</Web></ComicInfo>"
		};
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			timed("comicinfo", i, result, [&]() { bp::ComicInfo out; return bp::parseComicInfo(data, out); });
		}
		summary("comicinfo", runs, result);
	}

	void runOpds(int runs, std::mt19937_64& rng)
	{
		Seeds seeds = {
			"<feed xmlns=\"http://www.w3.org/2005/Atom\"><id>urn:example:feed</id><title>Catalog</title><entry><id>urn:series:1</id><title>Lantern</title><link rel=\"http://opds-spec.org/image\" href=\"/cover.jpg\"/></entry></feed>",
			"<?xml version=\"1.0\"?><a:feed xmlns:a=\"http://www.w3.org/2005/Atom\"><a:id>urn:feed:2</a:id><a:title>Books</a:title><a:link rel=\"next\" href=\"?page=2\"/><a:entry><a:id>urn:book:2</a:id><a:title><![CDATA[Chapter & Moon]]></a:title></a:entry></a:feed>"
		};
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			timed("opds", i, result, [&]() { bp::OpdsFeed out; return bp::parseOpds(data, out); });
		}
		summary("opds", runs, result);
	}

	void runKomga(int runs, std::mt19937_64& rng)
	{
		Seeds seeds = {
			R"JSON({"content":[{"id":"book-1","name":"Lantern","number":"12.5","media":{"pagesCount":24},"numberSort":12.5}],"totalPages":3})JSON",
			R"JSON({"content":[{"id":"series-1","name":"North"}],"totalPages":1})JSON"
		};
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			timed("komga", i, result, [&]() { std::vector<bp::KomgaItem> items; int total = 0; return bp::parseKomgaPage(data, items, total); });
		}
		summary("komga", runs, result);
	}

	void runRepo(int runs, std::mt19937_64& rng)
	{
		Seeds seeds = {
			R"JSON({"format":1,"name":"Sample repository","homepage":"https://example.org","sources":[{"id":"ember-reader","name":"Ember Reader","lang":"en","version":"1.2.0","api":1,"nsfw":false,"file":"sources/ember.lua","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","icon":"icons/ember.png"}]})JSON",
			R"JSON({"name":"Empty repository","sources":[]})JSON"
		};
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			timed("repo", i, result, [&]() { bp::RepoIndex out; std::string error; return bp::parseRepoIndex(data, out, error); });
		}
		summary("repo", runs, result);
	}

	void runMangaStore(int runs, std::mt19937_64& rng, const std::string& root)
	{
		const std::string library = "# banana-player manga library v1\nlocal:/ember\tEmber Lantern\tlocal\t\t100\t200\t1\trtl\n";
		const std::string progress = "# banana-player manga progress v1\nlocal:/ember\tchapter:12\tChapter 12\t3\t24\t0\t300\t0\n";
		Seeds seeds = {library, progress};
		std::string dir = root + "/store";
		std::filesystem::create_directories(dir + "/manga");
		std::string libPath = dir + "/manga/library.tsv";
		std::string progressPath = dir + "/manga/progress.tsv";
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			bool mutateLibrary = (i % 2) == 0;
			writeFile(libPath, mutateLibrary ? data : library);
			writeFile(progressPath, mutateLibrary ? progress : data);
			timed("mangastore", i, result, [&]() { bp::MangaStore store; return store.load(dir); });
		}
		summary("mangastore", runs, result);
	}

	void runQueue(int runs, std::mt19937_64& rng, const std::string& root)
	{
		Seeds seeds = {
			"# banana-player manga downloads-queue v1\njob-1\tseries-1\tEmber Lantern\tchapter-12\tChapter 12\tlocal\twaiting\t1\t128\t1700000000\t\t/data/Manga/Ember/12.cbz\thttps://example.org/1.jpg\\nhttps://example.org/2.jpg\n",
			"# banana-player manga downloads-queue v1\njob-2\tseries-2\tNorth\tchapter-3\tChapter 3\tkomga\tpaused\t0\t0\t1700000001\t\t/data/Manga/North/3.cbz\thttps://example.org/3.jpg\n"
		};
		std::string dir = root + "/queue";
		std::filesystem::create_directories(dir + "/manga");
		std::string path = dir + "/manga/downloads-queue.tsv";
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			writeFile(path, data);
			timed("dlqueue", i, result, [&]() { bp::DownloadQueue queue; return queue.load(dir); });
		}
		summary("dlqueue", runs, result);
	}

	void runPageCache(int runs, std::mt19937_64& rng, const std::string& root)
	{
		std::string dir = root + "/cache";
		std::filesystem::create_directories(dir);
		std::string index = dir + "/index.tsv";
		{
			bp::PageCache cache;
			cache.open(dir, 1024 * 1024);
			const uint8_t bytes[] = {0xFF, 0xD8, 0xFF, 0xD9};
			cache.put("https://example.org/page.jpg", bytes, sizeof bytes);
			cache.save();
		}
		Seeds seeds = {readFile(index), "# page cache index\nhttps://example.org/other.png\t4\t100\n"};
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			writeFile(index, data);
			timed("pagecache", i, result, [&]() { bp::PageCache cache; return cache.open(dir, 1024 * 1024); });
		}
		summary("pagecache", runs, result);
	}

	void runDecode(int runs, std::mt19937_64& rng)
	{
		Seeds seeds;
		for (const char* name : {"page.jpg", "page.png", "page.webp"})
			seeds.push_back(readFile(std::string(BP_FUZZ_DIR) + "/" + name));
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			timed("pagedecode", i, result, [&]() {
				auto image = bp::decodePage(reinterpret_cast<const uint8_t*>(data.data()), data.size(), 640, 4096);
				if (image.ok())
				{
					size_t expected = static_cast<size_t>(image.w) * static_cast<size_t>(image.h) * 4;
					if (image.rgba.size() != expected)
						std::fprintf(stderr, "pagedecode: run %d returned inconsistent RGBA size\n", i);
				}
				return image.ok();
			});
		}
		summary("pagedecode", runs, result);
	}

	void runNames(int runs, std::mt19937_64& rng)
	{
		Seeds seeds = {"Chapter 12.5 - Lantern.cbz", "Vol.2 Ch.10 - North.cbz", "__MACOSX/1.jpg", "page10.jpg"};
		Result result;
		for (int i = 0; i < runs; i++)
		{
			std::string data = mutant(seeds, i, rng);
			timed("manga_names", i, result, [&]() {
				bp::chapterNumber(data);
				bp::volumeNumber(data);
				bp::isPageImage(data);
				std::vector<std::string> pages = {data, "page2.jpg", "page10.jpg", "__MACOSX/1.jpg"};
				bp::sortPages(pages);
				return true;
			});
		}
		summary("manga_names", runs, result);
	}
}

int main(int argc, char** argv)
{
	int runs = 3000;
	if (argc > 2)
	{
		std::fprintf(stderr, "usage: %s [iteration-count]\n", argv[0]);
		return 2;
	}
	if (argc == 2)
	{
		char* end = nullptr;
		long value = std::strtol(argv[1], &end, 10);
		if (!end || *end || value < 1 || value > 1000000)
		{
			std::fprintf(stderr, "iteration-count must be between 1 and 1000000\n");
			return 2;
		}
		runs = static_cast<int>(value);
	}
	std::string root = temporaryRoot();
	if (root.empty())
	{
		std::fprintf(stderr, "could not create a temporary directory under /tmp\n");
		return 1;
	}
	std::mt19937_64 rng(0x4D414E474146555AULL);
	try
	{
		runCbz(runs, rng, root);
		runComicInfo(runs, rng);
		runOpds(runs, rng);
		runKomga(runs, rng);
		runRepo(runs, rng);
		runMangaStore(runs, rng, root);
		runQueue(runs, rng, root);
		runPageCache(runs, rng, root);
		runDecode(runs, rng);
		runNames(runs, rng);
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "fuzz setup failed: %s\n", e.what());
		std::filesystem::remove_all(root);
		return 1;
	}
	std::filesystem::remove_all(root);
	std::puts("fuzz: done");
	return 0;
}
