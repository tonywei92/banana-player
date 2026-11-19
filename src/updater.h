#pragma once
#include "update.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace bp
{
	// The built-in updater, for a player installed by hand on the Ports shelf (a Store install is the Store's to
	// update): asks GitHub for the latest release, downloads its zip with BananaOS's curl, checks it against the
	// release's .sha256, and unpacks it beside the installed folder as BananaPlayer.update. launch.sh puts it in place
	// as the player next starts (the old folder kept as BananaPlayer.old); "Restart now" is the player quitting with
	// RESTART_CODE, which launch.sh takes as "start again". All of it on a thread of its own: the network is slow, the
	// handheld often offline
	class Updater
	{
	public:
		enum class State { Off, Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };
		static constexpr int RESTART_CODE = 75;

		// pkgDir: the installed BananaPlayer folder (VERSION, bin/); "" where there's none (a build on a computer):
		// off. A folder of the Store's: off, the Store's to update
		void init(const std::string& pkgDir, const std::string& currentVersion);
		~Updater();

		void check();		// in the background; nothing while one runs
		void download();	// the release check() found
		void cancel();		// stop a download: the part files go, nothing was changed, and the release stays known
		State state() const { return st; }
		bool fromStore() const { return store; }
		std::string current() const { return currentVersion; }
		std::string available() const;	// the release's version, once one's found
		std::string message() const;	// why it failed
		int progress() const;			// the download's percent, -1 unknown

	private:
		void join();
		void fail(const std::string& why);
		std::string fetch(const std::string& url, bool& ok);
		bool fetchTo(const std::string& url, const std::string& path);
		std::string pkgDir, parentDir, currentVersion;
		bool store = false;
		std::atomic<State> st{State::Off};
		std::atomic<bool> cancelled{false};	// cancel(): the worker stops after its curl is killed
		mutable std::mutex lock;
		Release release;
		std::string why, partPath;
		std::thread worker;
	};
}
