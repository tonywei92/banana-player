// Network folders and files (SMB, FTP): what the browser lists and what mpv reads, behind one interface. A backend
// is for one server and one thread at a time (libsmb2's contexts aren't thread-safe): the browser's worker lists with
// one, and each file mpv opens gets its own (open, from mpv's demuxer thread).
#pragma once
#include "servers.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	struct NetEntry
	{
		std::string name;
		bool dir = false;
		int64_t size = -1;	// -1: not known
	};

	// What went wrong, in a word the UI turns into a sentence ("Can't find MY-PC.", "Wrong user or password."...)
	enum class NetError { None, NoNetwork, NotFound, Auth, NoAccess, Timeout, Gone, Cancelled, Other };

	struct NetResult
	{
		NetError error = NetError::None;
		std::string detail;	// for the log (never a password)
		bool ok() const { return error == NetError::None; }
	};

	// An open file, read at any offset. read() fills buf with up to n bytes at offset (fewer at the end, 0 past
	// it); < 0 on a failure, with lastResult() saying why. cancel() may be called from another thread and makes a
	// waiting read return at once (Next, quit)
	class NetFile
	{
	public:
		virtual ~NetFile() = default;
		virtual int64_t read(char* buf, int64_t n, int64_t offset) = 0;
		virtual int64_t size() const = 0;
		virtual void cancel() = 0;
		virtual NetResult lastResult() const = 0;
	};

	class NetBackend
	{
	public:
		virtual ~NetBackend() = default;
		// logs in (a test: Test connection); the other calls connect by themselves
		virtual NetResult connect() = 0;
		// a folder's entries, "." and ".." left out. SMB: "/" lists the server's disk shares (as folders), "/Share/A/B"
		// a folder in one; FTP: paths as the server has them
		virtual NetResult list(const std::string& path, std::vector<NetEntry>& out) = 0;
		virtual std::unique_ptr<NetFile> open(const std::string& path, NetResult& result) = 0;
		virtual void cancel() = 0;	// from another thread: a waiting call returns Cancelled

		// Writing (Files' Copy, Move, Rename, Delete on a network folder). Paths as list() takes them. None of them
		// replaces what is there: a name taken is an error (NoAccess on a folder the account can't write to)
		virtual NetResult makeDir(const std::string& path) = 0;
		virtual NetResult remove(const std::string& path, bool dir) = 0;	// a file, or an empty folder
		virtual NetResult rename(const std::string& from, const std::string& to) = 0;	// within one share (SMB)
		// a new file, its bytes from more(buf, n): how many it put in buf, 0 at the end, < 0 to stop (cancelled, or a
		// read that failed: the file is then removed). A file already there isn't written over
		virtual NetResult put(const std::string& path, const std::function<int64_t(char*, int64_t)>& more) = 0;
	};

	// password: the plain one (opened from the server's secret by the caller); timeoutMs bounds each wait
	std::unique_ptr<NetBackend> makeBackend(const NetServer& server, const std::string& password, int timeoutMs = 8000);
}
