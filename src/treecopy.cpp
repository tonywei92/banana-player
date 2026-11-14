#include "treecopy.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <random>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#ifdef __linux__
#include <sys/syscall.h>
#endif

namespace bp
{
	namespace
	{
		std::string dirOf(const std::string& p)
		{
			size_t i = p.rfind('/');
			if (i == std::string::npos) return ".";
			return i == 0 ? "/" : p.substr(0, i);
		}

		std::string tempName()
		{
			static std::atomic<unsigned> seq{0};
			static const unsigned long long seed = std::random_device{}() ^ (unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count();
			char b[48];
			snprintf(b, sizeof b, ".bp-copy-%llx-%u", seed & 0xffffffffffull, seq.fetch_add(1));
			return b;
		}

		bool scan(const std::string& path, const std::string& rel, TreeInfo& info, const std::function<bool(const std::string&)>& refuse)
		{
			struct stat st;
			if (lstat(path.c_str(), &st) != 0)
			{
				info.bad = rel;
				return false;
			}
			if (S_ISREG(st.st_mode))
			{
				info.files++;
				info.bytes += (uint64_t)st.st_size;
				if ((uint64_t)st.st_size > info.biggest) info.biggest = (uint64_t)st.st_size;
				return true;
			}
			if (!S_ISDIR(st.st_mode) || (refuse && refuse(path)))
			{
				info.bad = rel;
				return false;
			}
			DIR* d = opendir(path.c_str());
			if (!d)
			{
				info.bad = rel;
				return false;
			}
			bool ok = true;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n == "." || n == "..") continue;
				if (!scan(path + "/" + n, rel.empty() ? n : rel + "/" + n, info, refuse))
				{
					ok = false;
					break;
				}
			}
			closedir(d);
			return ok;
		}

		int copyFileTo(const std::string& from, const std::string& to, const std::string& rel, const CopyProgress& progress)
		{
			int in = open(from.c_str(), O_RDONLY);
			if (in < 0) return errno;
			struct stat st;
			if (fstat(in, &st) != 0)
			{
				int e = errno;
				close(in);
				return e;
			}
			int out = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
			if (out < 0)
			{
				int e = errno;
				close(in);
				return e;
			}
			std::vector<char> buf(1024 * 1024);
			int err = 0;
			for (;;)
			{
				ssize_t n = read(in, buf.data(), buf.size());
				if (n < 0)
				{
					if (errno == EINTR) continue;
					err = errno;
					break;
				}
				if (n == 0) break;
				for (ssize_t off = 0; off < n && !err;)
				{
					ssize_t w = write(out, buf.data() + off, (size_t)(n - off));
					if (w < 0)
					{
						if (errno != EINTR) err = errno;
					}
					else off += w;
				}
				if (err) break;
				if (progress && !progress((uint64_t)n, rel))
				{
					err = ECANCELED;
					break;
				}
			}
			close(in);
			// on the card before a move takes the original away
			if (!err && fsync(out) != 0) err = errno;
			if (!err)
			{
				// the file's own date (the dashboard lists it), as a move keeps it
				struct timeval tv[2];
				tv[0].tv_sec = st.st_atime;
				tv[0].tv_usec = 0;
				tv[1].tv_sec = st.st_mtime;
				tv[1].tv_usec = 0;
				futimes(out, tv);
			}
			if (close(out) != 0 && !err) err = errno;
			return err;
		}

		int copyInto(const std::string& from, const std::string& to, const std::string& rel, const CopyProgress& progress)
		{
			struct stat st;
			if (lstat(from.c_str(), &st) != 0) return errno;
			if (S_ISREG(st.st_mode)) return copyFileTo(from, to, rel, progress);
			if (!S_ISDIR(st.st_mode)) return EINVAL;	// a link or a device: scanTree turns these down first
			if (mkdir(to.c_str(), 0755) != 0) return errno;
			DIR* d = opendir(from.c_str());
			if (!d) return errno;
			int err = 0;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n == "." || n == "..") continue;
				err = copyInto(from + "/" + n, to + "/" + n, rel.empty() ? n : rel + "/" + n, progress);
				if (err) break;
			}
			closedir(d);
			if (!err)
			{
				struct timeval tv[2];
				tv[0].tv_sec = st.st_atime;
				tv[0].tv_usec = 0;
				tv[1].tv_sec = st.st_mtime;
				tv[1].tv_usec = 0;
				utimes(to.c_str(), tv);
			}
			return err;
		}
	}

	bool scanTree(const std::string& path, TreeInfo& info, const std::function<bool(const std::string&)>& refuse)
	{
		info = TreeInfo();
		return scan(path, "", info, refuse);
	}

	int copyTree(const std::string& from, const std::string& to, const CopyProgress& progress)
	{
		struct stat st;
		if (lstat(to.c_str(), &st) == 0) return EEXIST;
		std::string tmp = dirOf(to) + "/" + tempName();
		struct stat fs;
		if (lstat(from.c_str(), &fs) != 0) return errno;
		std::string top = S_ISDIR(fs.st_mode) ? "" : from.substr(from.rfind('/') + 1);
		int err = copyInto(from, tmp, top, progress);
		if (!err) err = renameNoReplace(tmp, to);
		if (err) removeTree(tmp);
		return err;
	}

	void removeTree(const std::string& path)
	{
		struct stat st;
		if (lstat(path.c_str(), &st) != 0) return;
		if (!S_ISDIR(st.st_mode))
		{
			unlink(path.c_str());
			return;
		}
		if (DIR* d = opendir(path.c_str()))
		{
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n == "." || n == "..") continue;
				removeTree(path + "/" + n);
			}
			closedir(d);
		}
		rmdir(path.c_str());
	}

	int renameNoReplace(const std::string& from, const std::string& to)
	{
		int r = -1;
#if defined(__linux__) && defined(SYS_renameat2)
		r = (int)syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1u /* RENAME_NOREPLACE */);
		if (r == 0) return 0;
		if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP) return errno;
#elif defined(__APPLE__) && defined(RENAME_EXCL)
		r = renamex_np(from.c_str(), to.c_str(), RENAME_EXCL);
		if (r == 0) return 0;
		if (errno != ENOTSUP && errno != EINVAL) return errno;
#endif
		(void)r;
		// no atomic flag here (vfat, say): a hard link fails if the name is taken (a folder can't be linked: EPERM)
		if (link(from.c_str(), to.c_str()) == 0)
		{
			unlink(from.c_str());
			return 0;
		}
		if (errno == EEXIST) return EEXIST;
		if (errno == EXDEV) return EXDEV;
		// no hard links either: the check and the rename aren't atomic, but it's the best this filesystem allows
		struct stat st;
		if (lstat(to.c_str(), &st) == 0) return EEXIST;
		return rename(from.c_str(), to.c_str()) == 0 ? 0 : errno;
	}

	uint64_t trimCacheFolder(const std::string& dir, uint64_t budget, const std::string& keep)
	{
		struct Item { std::string path; uint64_t bytes; int64_t at; };
		std::vector<Item> items;
		uint64_t total = 0;
		if (DIR* d = opendir(dir.c_str()))
		{
			while (dirent* e = readdir(d))
			{
				std::string name = e->d_name;
				if (name == "." || name == "..") continue;
				std::string path = dir + "/" + name;
				struct stat st;
				if (lstat(path.c_str(), &st) != 0) continue;
				TreeInfo info;
				scanTree(path, info);
				items.push_back({path, info.bytes, (int64_t)st.st_mtime});
				total += info.bytes;
			}
			closedir(d);
		}
		std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
			return a.at != b.at ? a.at < b.at : a.path < b.path;
		});
		for (const Item& it : items)
		{
			if (total <= budget) break;
			if (!keep.empty() && (it.path == keep || keep.compare(0, it.path.size() + 1, it.path + "/") == 0)) continue;
			removeTree(it.path);
			total -= std::min(total, it.bytes);
		}
		return total;
	}
}
