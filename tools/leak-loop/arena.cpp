// A model of the reader's allocations under glibc: two decode threads each make a page's file bytes (300 KB) and
// its RGBA (1400x2000: 11.2 MB), hand the RGBA to the main thread, which keeps a few (the 32 MB budget) and frees
// the rest as the view moves; small allocations go on meanwhile (the Ui's strings). A "chapter" is 8 pages; after
// each the reader "closes" (everything freed) and VmRSS is printed.
// arena [cycles] [trim]   (MALLOC_MMAP_THRESHOLD_=1048576 in the environment pins the mmap threshold)
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <malloc.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static long rssKb()
{
	FILE* f = fopen("/proc/self/status", "r");
	char line[256];
	long kb = 0;
	while (f && fgets(line, sizeof line, f))
		if (!strncmp(line, "VmRSS:", 6)) kb = atol(line + 6);
	if (f) fclose(f);
	return kb;
}

int main(int argc, char** argv)
{
	int cycles = argc > 1 ? atoi(argv[1]) : 20;
	bool trim = argc > 2 && !strcmp(argv[2], "trim");
	std::mutex m;
	std::condition_variable cv;
	std::deque<int> jobs;
	std::deque<std::shared_ptr<std::vector<unsigned char>>> done;
	bool quit = false;
	std::vector<std::thread> threads;
	for (int k = 0; k < 2; k++)
		threads.emplace_back([&] {
			for (;;)
			{
				int j;
				{
					std::unique_lock<std::mutex> l(m);
					cv.wait(l, [&] { return quit || !jobs.empty(); });
					if (quit) return;
					j = jobs.front();
					jobs.pop_front();
				}
				std::vector<unsigned char> bytes(300 * 1024, 1);	// the page's file
				// the decoder's frame (YUV 4:2:0) and the scaled RGBA; pages differ a little in size, as real ones do
				size_t px = (size_t)1400 * (1900 + (j * 37) % 200);
				std::vector<unsigned char> yuv(px * 3 / 2, 2);
				auto rgba = std::make_shared<std::vector<unsigned char>>(px * 4, 3);
				std::lock_guard<std::mutex> l(m);
				done.push_back(rgba);
				cv.notify_all();
			}
		});
	std::vector<std::string> small;	// what the Ui allocates meanwhile, some of it kept (a cache)
	printf("start rss %ld KB\n", rssKb());
	for (int c = 1; c <= cycles; c++)
	{
		std::deque<std::shared_ptr<std::vector<unsigned char>>> kept;
		for (int page = 0; page < 8; page++)
		{
			{
				std::lock_guard<std::mutex> l(m);
				jobs.push_back(c * 8 + page);
			}
			cv.notify_all();
			std::shared_ptr<std::vector<unsigned char>> got;
			{
				std::unique_lock<std::mutex> l(m);
				cv.wait(l, [&] { return !done.empty(); });
				got = done.front();
				done.pop_front();
			}
			kept.push_back(got);
			while (kept.size() > 3) kept.pop_front();	// the budget
			for (int i = 0; i < 200; i++) small.push_back(std::string(40 + (i * 7) % 300, 'x'));
			if (small.size() > 4000) small.erase(small.begin(), small.begin() + 2000);
		}
		kept.clear();	// the reader closes
		if (trim) malloc_trim(0);
		printf("cycle %2d closed rss %ld KB\n", c, rssKb());
	}
	{
		std::lock_guard<std::mutex> l(m);
		quit = true;
	}
	cv.notify_all();
	for (auto& t : threads) t.join();
	return 0;
}
