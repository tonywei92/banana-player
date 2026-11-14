// The browser's network work, on a thread of its own: listing a server's folder (and testing a connection) can take
// seconds or never answer, and the panel must go on drawing. One backend a server, kept for the next listing (one
// thread uses them: libsmb2's contexts aren't thread-safe). Each job carries a serial: an answer for a folder left
// since is dropped by the caller.
#pragma once
#include "net.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace bp
{
	struct NetListing
	{
		int serial = 0;
		std::string dir;		// the bpnet path listed
		bool test = false;		// a Test connection, not a listing
		NetResult result;
		std::vector<NetEntry> entries;
		std::vector<std::string> names;	// all the folder's files (subtitles, covers...): the player's sidecars
		// the account can write here (a hidden file made and taken away again, as nothing else says it for both
		// SMB and FTP); false too for an SMB server's list of shares, where nothing can be put
		bool writable = false;
	};

	class NetWorker
	{
	public:
		// done: on the worker's thread (forward it to the UI's)
		explicit NetWorker(std::function<void(NetListing*)> done) : done(std::move(done)) {}
		~NetWorker();
		// dir: a bpnet path of server's; the listing comes to done() with this serial
		void list(int serial, const NetServer& server, const std::string& password, const std::string& dir, bool probe = false);
		void test(int serial, const NetServer& server, const std::string& password);
		// whatever runs now returns Cancelled; queued jobs go
		void cancelAll();
		// a server changed or went: its kept backend is dropped (the next job connects anew)
		void forget(const std::string& serverId);

	private:
		struct Job
		{
			int serial;
			NetServer server;
			std::string password, dir;
			bool test;
			bool probe = false;	// try writing (the browser's folder: its file actions)
		};
		void run();
		std::function<void(NetListing*)> done;
		std::thread worker;
		std::mutex lock;
		std::condition_variable wake;
		std::deque<Job> jobs;
		std::map<std::string, std::unique_ptr<NetBackend>> backends;	// the worker's own
		std::map<std::string, std::string> passwordOf;					// what each was made with
		std::vector<std::string> forgotten;
		NetBackend* busy = nullptr;	// the one in a call now, for cancelAll
		bool stopping = false;
	};
}
