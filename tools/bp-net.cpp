// bp-net: the network backends from a shell, for trying a server out and for the tests in DEVELOPMENT.md.
//
//   bp-net ls <host[:port]> <user|-guest> <password|-> <path>
//   bp-net size <host[:port]> <user|-guest> <password|-> <path>
//   bp-net cat <host[:port]> <user|-guest> <password|-> <path> <offset> <bytes>   (BP_OUT=file: the bytes too)
//   bp-net loop <host[:port]> <user|-guest> <password|-> <path> <chunk> <sleep-ms> (the whole file, chunk by chunk,
//            a pause between them: restart the server meanwhile to see the read get its connection back)
//   bp-net reads <host[:port]> <user|-guest> <password|-> <path> <offset:bytes,offset:bytes,...>  (one open file, the
//            reads one after the other, their bytes joined in BP_OUT: a seek pattern)
//   bp-net cancel-ls <host[:port]> <user|-guest> <password|-> <path> <ms>  (cancel() from another thread after ms,
//            then the same listing again on the same backend)
//   bp-net cancel-cat <host[:port]> <user|-guest> <password|-> <path> <wait-ms> <ms>  (the file opened, wait-ms to
//            stall the server, then a read of it all with cancel() from another thread after ms)
//
// <host> is an SMB server, or ftp://host[:port] / ftps://host[:port] for an FTP one (paths are the server's own).
//
// Exit status 0 when the call succeeded. Builds without CMake: see DEVELOPMENT.md.
#include "net.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace bp;

static const char* errorName(NetError e)
{
	switch (e)
	{
	case NetError::None: return "None";
	case NetError::NoNetwork: return "NoNetwork";
	case NetError::NotFound: return "NotFound";
	case NetError::Auth: return "Auth";
	case NetError::NoAccess: return "NoAccess";
	case NetError::Timeout: return "Timeout";
	case NetError::Gone: return "Gone";
	case NetError::Cancelled: return "Cancelled";
	case NetError::Other: return "Other";
	}
	return "?";
}

static int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// FNV-1a, enough to tell two byte ranges apart
static uint64_t hash(const char* p, size_t n)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 1099511628211ull;
	return h;
}

static int report(const NetResult& r)
{
	printf("error %s: %s\n", errorName(r.error), r.detail.c_str());
	return 1;
}

static int outcome(const NetResult& r)
{
	if (r.ok()) { printf("ok\n"); return 0; }
	return report(r);
}

static void usage()
{
	fprintf(stderr, "usage: bp-net ls|size|cat|loop|cancel-ls|cancel-cat|mkdir|rm|rmdir|mv|put <host[:port]> <user|-guest> <password|-> <path> [args]\n"
		"  mv: <to>; put: <local file>\n");
	exit(2);
}

int main(int argc, char** argv)
{
	if (argc < 6) usage();
	std::string cmd = argv[1], host = argv[2], user = argv[3], pass = argv[4], path = argv[5];
	NetServer s;
	s.type = NetType::Smb;
	if (host.compare(0, 6, "ftp://") == 0) { s.type = NetType::Ftp; host = host.substr(6); }
	else if (host.compare(0, 7, "ftps://") == 0) { s.type = NetType::Ftp; s.ftps = true; host = host.substr(7); }
	size_t colon = host.rfind(':');
	if (colon != std::string::npos && host.find(']') == std::string::npos && host.find(':') == colon)
	{
		s.port = atoi(host.c_str() + colon + 1);
		host = host.substr(0, colon);
	}
	s.host = host;
	s.guest = user == "-guest";
	if (!s.guest) s.user = user;
	if (pass == "-") pass = "";
	int timeoutMs = getenv("BP_TIMEOUT") ? atoi(getenv("BP_TIMEOUT")) : 8000;
	std::unique_ptr<NetBackend> b = makeBackend(s, pass, timeoutMs);
	if (!b) { fprintf(stderr, "no backend\n"); return 2; }
	int64_t t0 = nowMs();

	// writing
	if (cmd == "mkdir") return outcome(b->makeDir(path));
	if (cmd == "rm") return outcome(b->remove(path, false));
	if (cmd == "rmdir") return outcome(b->remove(path, true));
	if (cmd == "mv")
	{
		if (argc < 7) usage();
		return outcome(b->rename(path, argv[6]));
	}
	if (cmd == "put")
	{
		if (argc < 7) usage();
		FILE* in = fopen(argv[6], "rb");
		if (!in) { perror(argv[6]); return 2; }
		int64_t sent = 0;
		NetResult r = b->put(path, [&](char* buf, int64_t n) -> int64_t { int64_t got = (int64_t)fread(buf, 1, (size_t)n, in); sent += got; return got; });
		fclose(in);
		printf("put %lld bytes in %lld ms\n", (long long)sent, (long long)(nowMs() - t0));
		return outcome(r);
	}
	if (cmd == "ls" || cmd == "cancel-ls")
	{
		std::vector<NetEntry> v;
		std::thread canceller;
		if (cmd == "cancel-ls")
		{
			if (argc < 7) usage();
			int ms = atoi(argv[6]);
			canceller = std::thread([&b, ms] { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); b->cancel(); });
		}
		NetResult r = b->list(path, v);
		if (canceller.joinable()) canceller.join();
		printf("%s in %lld ms\n", r.ok() ? "listed" : errorName(r.error), (long long)(nowMs() - t0));
		if (!r.ok()) printf("error %s: %s\n", errorName(r.error), r.detail.c_str());
		if (cmd == "cancel-ls")
		{
			// a cancel with nothing under way must not touch the next call
			b->cancel();
			v.clear();
			t0 = nowMs();
			r = b->list(path, v);
			printf("again: %s in %lld ms\n", r.ok() ? "listed" : errorName(r.error), (long long)(nowMs() - t0));
			if (!r.ok()) return report(r);
		}
		else if (!r.ok()) return 1;
		for (const NetEntry& e : v) printf("%c %12lld  %s\n", e.dir ? 'd' : 'f', (long long)e.size, e.name.c_str());
		return 0;
	}

	NetResult r;
	std::unique_ptr<NetFile> f = b->open(path, r);
	if (!f) return report(r);
	if (cmd == "size")
	{
		printf("%lld\n", (long long)f->size());
		return 0;
	}
	if (cmd == "cat")
	{
		if (argc < 8) usage();
		int64_t offset = atoll(argv[6]), n = atoll(argv[7]);
		std::vector<char> buf((size_t)std::max<int64_t>(n, 1));
		int64_t got = f->read(buf.data(), n, offset);
		if (got < 0) return report(f->lastResult());
		printf("%lld bytes, fnv1a %016llx, size %lld, %lld ms\n", (long long)got, (unsigned long long)hash(buf.data(), (size_t)got),
			(long long)f->size(), (long long)(nowMs() - t0));
		if (const char* out = getenv("BP_OUT")) std::ofstream(out, std::ios::binary).write(buf.data(), got);
		return 0;
	}
	if (cmd == "reads")
	{
		if (argc < 7) usage();
		std::ofstream out(getenv("BP_OUT") ? getenv("BP_OUT") : "/dev/null", std::ios::binary);
		std::string spec = argv[6];
		size_t i = 0;
		while (i < spec.size())
		{
			size_t comma = spec.find(',', i);
			if (comma == std::string::npos) comma = spec.size();
			std::string one = spec.substr(i, comma - i);
			i = comma + 1;
			size_t colon = one.find(':');
			if (colon == std::string::npos) usage();
			int64_t offset = atoll(one.c_str()), n = atoll(one.c_str() + colon + 1);
			std::vector<char> buf((size_t)std::max<int64_t>(n, 1));
			int64_t t1 = nowMs();
			int64_t got = f->read(buf.data(), n, offset);
			if (got < 0) { printf("at %lld: ", (long long)offset); return report(f->lastResult()); }
			out.write(buf.data(), got);
			printf("%lld:%lld -> %lld bytes, %lld ms\n", (long long)offset, (long long)n, (long long)got, (long long)(nowMs() - t1));
		}
		return 0;
	}
	if (cmd == "loop")
	{
		if (argc < 8) usage();
		int64_t chunk = atoll(argv[6]);
		int sleepMs = atoi(argv[7]);
		std::vector<char> buf((size_t)chunk);
		std::ofstream out(getenv("BP_OUT") ? getenv("BP_OUT") : "/dev/null", std::ios::binary);
		int64_t offset = 0;
		uint64_t h = 1469598103934665603ull;
		while (offset < f->size())
		{
			int64_t t1 = nowMs();
			int64_t got = f->read(buf.data(), chunk, offset);
			if (got < 0) { printf("at %lld: ", (long long)offset); return report(f->lastResult()); }
			if (got == 0) break;
			for (int64_t i = 0; i < got; i++) h = (h ^ (unsigned char)buf[(size_t)i]) * 1099511628211ull;
			out.write(buf.data(), got);
			int64_t took = nowMs() - t1;
			if (took > 500) printf("chunk at %lld took %lld ms\n", (long long)offset, (long long)took);
			offset += got;
			std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
		}
		printf("%lld bytes, fnv1a %016llx, %lld ms\n", (long long)offset, (unsigned long long)h, (long long)(nowMs() - t0));
		return 0;
	}
	if (cmd == "cancel-cat")
	{
		if (argc < 8) usage();
		printf("opened\n");
		fflush(stdout);
		std::this_thread::sleep_for(std::chrono::milliseconds(atoi(argv[6])));
		int ms = atoi(argv[7]);
		std::thread canceller([&f, ms] { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); f->cancel(); });
		std::vector<char> buf((size_t)f->size());
		t0 = nowMs();
		int64_t got = f->read(buf.data(), f->size(), 0);
		canceller.join();
		printf("read %lld in %lld ms\n", (long long)got, (long long)(nowMs() - t0));
		if (got < 0) return report(f->lastResult());
		return 0;
	}
	usage();
	return 2;
}
