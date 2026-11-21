#include "updater.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	static const char* LATEST = "https://api.github.com/repos/tonywei92/banana-player/releases/latest";

	// a word for the shell, quoted: a path with spaces ("Banana Player"), a URL
	static std::string quote(const std::string& s)
	{
		std::string out = "'";
		for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
		return out + "'";
	}

	static bool isDir(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	}

	static std::string readText(const std::string& path)
	{
		std::ifstream in(path);
		std::stringstream s;
		s << in.rdbuf();
		std::string t = s.str();
		while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
		return t;
	}

	void Updater::init(const std::string& dir, const std::string& version)
	{
		pkgDir = dir;
		currentVersion = version;
		size_t slash = dir.rfind('/');
		parentDir = slash == std::string::npos ? "." : dir.substr(0, slash);
		// the Store's apps live under /data/store (BananaOS) or ROCKNIX's /storage/.config/rocknixds
		store = dir.rfind("/data/store/", 0) == 0 || dir.find("/.config/rocknixds/") != std::string::npos;
		st = dir.empty() || store ? State::Off : State::Idle;
		if (st == State::Idle)
		{
			// what a download cut short (the player quit during it) left
			unlink((parentDir + "/BananaPlayer-update.zip.part").c_str());
			unlink((parentDir + "/BananaPlayer-update.zip.part.sha256").c_str());
			system(("rm -rf " + quote(parentDir + "/BananaPlayer.update.part")).c_str());
		}
		// one downloaded before and not yet in place (launch.sh puts it there at the next start)
		std::string ready = parentDir + "/BananaPlayer.update/BananaPlayer";
		if (st == State::Idle && isDir(ready) && compareVersions(readText(ready + "/VERSION"), version) > 0)
		{
			release.version = readText(ready + "/VERSION");
			st = State::Ready;
		}
	}

	Updater::~Updater()
	{
		join();
	}

	void Updater::join()
	{
		if (worker.joinable()) worker.join();
	}

	void Updater::fail(const std::string& w)
	{
		std::lock_guard<std::mutex> g(lock);
		why = w;
		st = State::Failed;
		fprintf(stderr, "update: %s\n", w.c_str());
	}

	std::string Updater::available() const
	{
		std::lock_guard<std::mutex> g(lock);
		return release.version;
	}

	std::string Updater::message() const
	{
		std::lock_guard<std::mutex> g(lock);
		return why;
	}

	int Updater::progress() const
	{
		if (st != State::Downloading) return st == State::Ready ? 100 : -1;
		std::string path;
		int64_t total;
		{
			std::lock_guard<std::mutex> g(lock);
			path = partPath;
			total = release.zipSize;
		}
		struct stat s;
		if (total <= 0 || stat(path.c_str(), &s) != 0) return -1;
		return (int)std::min<int64_t>(99, s.st_size * 100 / total);
	}

	std::string Updater::fetch(const std::string& url, bool& ok)
	{
		// BananaOS's curl (with its CA certificates); GitHub's API wants a User-Agent
		std::string cmd = "curl -fsSL --max-time 20 -H 'Accept: application/vnd.github+json' -A banana-player " + quote(url) + " 2>/dev/null";
		std::string out;
		ok = false;
		if (FILE* p = popen(cmd.c_str(), "r"))
		{
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
			ok = pclose(p) == 0;
		}
		return out;
	}

	bool Updater::fetchTo(const std::string& url, const std::string& path)
	{
		std::string cmd = "curl -fsSL --max-time 900 -o " + quote(path) + " " + quote(url) + " 2>/dev/null";
		return system(cmd.c_str()) == 0;
	}

	void Updater::check()
	{
		State s = st;
		if (s == State::Off || s == State::Checking || s == State::Downloading || s == State::Ready) return;
		join();
		st = State::Checking;
		worker = std::thread([this] {
			const char* env = getenv("BP_UPDATE_URL");	// tests: a file:// answer
			bool ok;
			std::string json = fetch(env ? env : LATEST, ok);
			Release r;
			if (!ok) { fail("No connection (is Wi-Fi on?)"); return; }
			if (!parseRelease(json, r)) { fail("GitHub's answer had no package"); return; }
			std::lock_guard<std::mutex> g(lock);
			release = r;
			st = compareVersions(r.version, currentVersion) > 0 ? State::Available : State::UpToDate;
			fprintf(stderr, "update: latest %s, this %s\n", r.version.c_str(), currentVersion.c_str());
		});
	}

	void Updater::cancel()
	{
		if (st != State::Downloading) return;
		cancelled = true;
		// the curl fetching the package carries the part file's path in its command line (and nothing else on the
		// handheld does): killing it returns from system() at once, and the worker cleans up and stops
		std::string path;
		{
			std::lock_guard<std::mutex> g(lock);
			path = partPath;
		}
		if (!path.empty()) system(("pkill -f " + quote(path) + " >/dev/null 2>&1").c_str());
	}

	void Updater::download()
	{
		if (st != State::Available && !(st == State::Failed && !available().empty())) return;
		join();
		cancelled = false;
		st = State::Downloading;
		Release r;
		{
			std::lock_guard<std::mutex> g(lock);
			r = release;
			partPath = parentDir + "/BananaPlayer-update.zip.part";
		}
		worker = std::thread([this, r] {
			std::string zip = parentDir + "/BananaPlayer-update.zip.part", sha = zip + ".sha256";
			std::string tmp = parentDir + "/BananaPlayer.update.part", ready = parentDir + "/BananaPlayer.update";
			auto clean = [&] {
				unlink(zip.c_str());
				unlink(sha.c_str());
				system(("rm -rf " + quote(tmp)).c_str());
			};
			clean();
			if (r.shaUrl.empty()) { fail("The release has no checksum"); return; }
			// cancelled: clean up and go back to idle. Not cancelled: nothing is touched (the files fetched so far are
			// the next step's: cleaning here threw the checksum away, and every update said "damaged")
			auto stopped = [&] {
				if (!cancelled) return false;
				clean();
				std::lock_guard<std::mutex> g(lock);
				why.clear();
				st = State::Idle;
				return true;
			};
			if (!fetchTo(r.shaUrl, sha)) { if (stopped()) return; clean(); fail("The download stopped (is Wi-Fi on?)"); return; }
			if (stopped()) return;
			if (!fetchTo(r.zipUrl, zip))
			{
				if (stopped()) return;
				clean();
				fail("The download stopped (is Wi-Fi on?)");
				return;
			}
			if (stopped()) return;
			// the zip as published, or nothing changes
			std::string want = parseSha256File(readText(sha)), got = sha256File(zip);
			if (want.empty() || got != want) { clean(); fail("The download was damaged; nothing was changed"); return; }
			mkdir(tmp.c_str(), 0755);
			// BananaOS's unzip, else Python's (it keeps no file modes: set below)
			std::string unzip = "unzip -q -o " + quote(zip) + " -d " + quote(tmp) + " >/dev/null 2>&1 || python3 -m zipfile -e " +
				quote(zip) + " " + quote(tmp) + " >/dev/null 2>&1";
			std::string bin = tmp + "/BananaPlayer/bin/banana-player";
			if (system(unzip.c_str()) != 0 || access(bin.c_str(), F_OK) != 0) { clean(); fail("The update couldn't be unpacked"); return; }
			chmod(bin.c_str(), 0755);
			chmod((tmp + "/BananaPlayer/launch.sh").c_str(), 0755);
			chmod((tmp + "/Banana Player.sh").c_str(), 0755);
			if (readText(tmp + "/BananaPlayer/VERSION") != r.version) { clean(); fail("The update isn't the version it said"); return; }
			// ready for launch.sh: one rename, so it's whole or not there
			system(("rm -rf " + quote(ready)).c_str());
			if (rename(tmp.c_str(), ready.c_str()) != 0) { clean(); fail("The update couldn't be put beside the player"); return; }
			unlink(zip.c_str());
			unlink(sha.c_str());
			st = State::Ready;
			fprintf(stderr, "update: %s ready in %s\n", r.version.c_str(), ready.c_str());
		});
	}
}
