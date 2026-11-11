#include "tarx.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

namespace bp
{
	namespace
	{
		constexpr size_t BLOCK = 512;
		constexpr uint64_t MAX_HEADER_DATA = 1 << 20;	// a GNU long name, a pax record set: never a page

		bool allZero(const char* p, size_t n)
		{
			for (size_t i = 0; i < n; i++)
				if (p[i]) return false;
			return true;
		}

		std::string field(const char* f, size_t n)
		{
			size_t len = 0;
			while (len < n && f[len]) len++;
			return std::string(f, len);
		}

		uint64_t parseOctal(const char* f, size_t n)
		{
			uint64_t v = 0;
			size_t i = 0;
			while (i < n && f[i] == ' ') i++;
			for (; i < n && f[i] >= '0' && f[i] <= '7'; i++) v = v * 8 + (uint64_t)(f[i] - '0');
			return v;
		}

		// GNU writes a size too big for the octal field as base-256: the first byte's high bit says so
		uint64_t parseSize(const char* f, size_t n)
		{
			if (n == 0) return 0;
			if ((unsigned char)f[0] & 0x80)
			{
				uint64_t v = 0;
				for (size_t i = 0; i < n; i++)
					v = (v << 8) | (uint64_t)(i == 0 ? ((unsigned char)f[0] & 0x7f) : (unsigned char)f[i]);
				return v;
			}
			return parseOctal(f, n);
		}

		bool checksumOk(const char* h)
		{
			unsigned sum = 0;
			for (size_t i = 0; i < BLOCK; i++)
				sum += (unsigned char)(i >= 148 && i < 156 ? ' ' : h[i]);
			return (uint64_t)sum == parseOctal(h + 148, 8);
		}

		bool makeDirs(const std::string& path, std::string& error)
		{
			if (path.empty()) return true;
			struct stat st;
			if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
			size_t slash = path.find_last_of('/');
			if (slash != std::string::npos && slash > 0 && !makeDirs(path.substr(0, slash), error)) return false;
			if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST)
			{
				error = "couldn't make " + path;
				return false;
			}
			return true;
		}

		// Every entry is a path under the folder asked for: no absolute path, no ".." step. A path that tries is
		// refused rather than clipped, so a broken or hostile archive never writes over the card.
		bool safePath(const std::string& raw, std::string& out)
		{
			if (raw.empty() || raw[0] == '/') return false;
			out.clear();
			size_t i = 0;
			while (i <= raw.size())
			{
				size_t j = raw.find('/', i);
				if (j == std::string::npos) j = raw.size();
				std::string part = raw.substr(i, j - i);
				if (!part.empty() && part != ".")
				{
					if (part == "..") return false;
					if (!out.empty()) out += '/';
					out += part;
				}
				i = j + 1;
			}
			return true;
		}

		bool skipPad(gzFile g, uint64_t size, std::string& error)
		{
			size_t pad = (size_t)((BLOCK - size % BLOCK) % BLOCK);
			char buf[BLOCK];
			if (pad && gzread(g, buf, (unsigned)pad) != (int)pad)
			{
				error = "the archive ended inside an entry";
				return false;
			}
			return true;
		}

		bool readSmall(gzFile g, uint64_t size, std::string& out, std::string& error)
		{
			if (size > MAX_HEADER_DATA || size > (uint64_t)INT32_MAX)
			{
				error = "a name field is too long";
				return false;
			}
			out.assign((size_t)size, '\0');
			if (size && gzread(g, &out[0], (unsigned)size) != (int)size)
			{
				error = "the archive ended inside a header";
				return false;
			}
			return skipPad(g, size, error);
		}

		bool skipData(gzFile g, uint64_t size, std::string& error)
		{
			char buf[16 * 1024];
			uint64_t left = size;
			while (left)
			{
				size_t take = (size_t)std::min<uint64_t>(left, sizeof buf);
				int n = gzread(g, buf, (unsigned)take);
				if (n <= 0)
				{
					error = "the archive ended inside an entry";
					return false;
				}
				left -= (uint64_t)n;
			}
			return skipPad(g, size, error);
		}

		bool writeFile(gzFile g, uint64_t size, const std::string& full, unsigned mode, std::string& error)
		{
			int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
			flags |= O_NOFOLLOW;
#endif
			int fd = open(full.c_str(), flags, 0600);
			if (fd < 0)
			{
				error = "couldn't write " + full;
				return false;
			}
			char buf[64 * 1024];
			uint64_t left = size;
			while (left)
			{
				size_t take = (size_t)std::min<uint64_t>(left, sizeof buf);
				int n = gzread(g, buf, (unsigned)take);
				if (n <= 0)
				{
					close(fd);
					error = "the archive ended inside " + full;
					return false;
				}
				size_t off = 0;
				while (off < (size_t)n)
				{
					ssize_t w = write(fd, buf + off, (size_t)n - off);
					if (w <= 0)
					{
						close(fd);
						error = "couldn't write " + full;
						return false;
					}
					off += (size_t)w;
				}
				left -= (uint64_t)n;
			}
			close(fd);
			if (chmod(full.c_str(), mode & 0777) != 0)
			{
				error = "couldn't set the mode of " + full;
				return false;
			}
			return skipPad(g, size, error);
		}

		// a pax extended header: its records are "<len> key=value\n", the path overriding the one in the next header
		void parsePax(const std::string& data, std::string& path)
		{
			size_t i = 0;
			while (i < data.size())
			{
				size_t sp = data.find(' ', i);
				if (sp == std::string::npos) break;
				long len = atol(data.c_str() + i);
				if (len <= 0 || i + (size_t)len > data.size()) break;
				std::string rec = data.substr(sp + 1, i + (size_t)len - (sp + 1));
				size_t eq = rec.find('=');
				if (eq != std::string::npos)
				{
					std::string key = rec.substr(0, eq), val = rec.substr(eq + 1);
					while (!val.empty() && (val.back() == '\n' || val.back() == '\0')) val.pop_back();
					if (key == "path") path = val;
				}
				i += (size_t)len;
			}
		}
	}

	bool extractTarGz(const std::string& archivePath, const std::string& destDir, std::string& error)
	{
		error.clear();
		gzFile g = gzopen(archivePath.c_str(), "rb");
		if (!g)
		{
			error = "couldn't open " + archivePath;
			return false;
		}
		if (!makeDirs(destDir, error))
		{
			gzclose(g);
			return false;
		}
		std::string pendingName;
		std::vector<std::pair<std::string, unsigned>> dirModes;
		bool ok = true;
		char h[BLOCK];
		while (ok)
		{
			int got = gzread(g, h, BLOCK);
			if (got == 0)
			{
				error = "the archive has no end marker";
				ok = false;
				break;
			}
			if (got != (int)BLOCK)
			{
				error = "the archive ended inside a header";
				ok = false;
				break;
			}
			if (allZero(h, BLOCK)) break;
			if (!checksumOk(h))
			{
				error = "a header's checksum doesn't match";
				ok = false;
				break;
			}

			char type = h[156];
			uint64_t size = parseSize(h + 124, 12);
			unsigned mode = (unsigned)parseOctal(h + 100, 8);

			// GNU long names and pax records say what the next header's own fields can't hold
			if (type == 'L' || type == 'K' || type == 'x' || type == 'g')
			{
				std::string data;
				if (!readSmall(g, size, data, error))
				{
					ok = false;
					break;
				}
				if (type == 'L') pendingName.assign(data.c_str());
				else if (type == 'x') parsePax(data, pendingName);
				continue;
			}

			std::string headerName = field(h, 100);
			std::string prefix = field(h + 345, 155);
			if (!prefix.empty()) headerName = prefix + "/" + headerName;
			std::string name = pendingName.empty() ? headerName : pendingName;
			pendingName.clear();

			std::string rel;
			if (!safePath(name, rel))
			{
				error = "an entry's path leads outside " + destDir;
				ok = false;
				break;
			}
			if (rel.empty())
			{
				// the archive's own "." entry: the folder already exists
				ok = skipData(g, size, error);
				continue;
			}
			std::string full = destDir + "/" + rel;
			if (type == '5')
			{
				ok = makeDirs(full, error) && skipData(g, size, error);
				if (ok) dirModes.push_back({full, mode});
			}
			else if (type == '\0' || type == '0' || type == '7')
			{
				std::string parent = full.substr(0, full.find_last_of('/'));
				ok = makeDirs(parent, error) && writeFile(g, size, full, mode, error);
			}
			else
			{
				// symlinks, hard links, devices and fifos: consumed, never made (the card is exFAT)
				ok = skipData(g, size, error);
			}
		}
		gzclose(g);
		if (!ok) return false;
		// the modes last: a folder read-only in the archive could not have taken the files after it
		for (size_t i = dirModes.size(); i-- > 0;)
			chmod(dirModes[i].first.c_str(), dirModes[i].second & 0777);
		return true;
	}
}
