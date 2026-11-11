#pragma once
// Java extensions for the manga reader: Tachiyomi / Mihon extension .jar files (a Keiyoushi-style repository's
// index.pb), run the way Tachimanga runs them on iOS — compiled Kotlin with the Tachiyomi source API around it. Here
// that is Suwayomi-Server on a Java 21 runtime, an optional download kept on the card, started only while a Java
// source is in use and stopped when it isn't (the handheld has 1 GB of RAM: it takes about 400 MB while it runs), and
// reached on 127.0.0.1 only. Measured on the RG DS: 25 s to start, a site's list in about 3 s, a page in about 2 s.
//
// Two halves: JavaExtHost (this file: the download, the process, its life) and SuwayomiClient (suwayomi.h: the
// GraphQL requests and the answers, pure, in bpcore). The Ui reaches the host through MangaNet, on the main thread
// only; the host's own threads (the download, the process watch) never touch the Ui: they set state and push the
// wake event main registered, as every other worker does.
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	// What the add-on downloads, pinned: a different file is refused (its SHA-256 checked before it's unpacked or run)
	struct JavaExtPins
	{
		// IBM Semeru 21 (OpenJ9) JRE for Linux aarch64 (58 MB .tar.gz; its legal/ folder is symlinks: skipped, the card
		// is exFAT). OpenJ9, not HotSpot: the server holds about 300 MB on it where HotSpot held about 370 MB, the
		// difference between the engine living through a chapter being read and the kernel killing it (2026-11-12)
		static constexpr const char* JRE_URL =
			"https://github.com/ibmruntimes/semeru21-binaries/releases/download/jdk-21.0.13.0/"
			"ibm-semeru-open-jre_aarch64_linux_21.0.13.0.tar.gz";
		static constexpr const char* JRE_SHA256 = "8086556c0022df57428d32c1b62d5c03a105bd3ff37d5a866444ac2603e68105";
		static constexpr int64_t JRE_BYTES = 57797374;
		// Suwayomi-Server v2.4.2366 (183 MB)
		static constexpr const char* SERVER_URL =
			"https://github.com/Suwayomi/Suwayomi-Server/releases/download/v2.4.2366/Suwayomi-Server-v2.4.2366.jar";
		static constexpr const char* SERVER_SHA256 = "af9feb20af9d7ebe9e30769e6c6ebc7fc7ab1c447388d42e0280b1da5fe07bfd";
		static constexpr int64_t SERVER_BYTES = 182952426;
	};

	class JavaExtHost
	{
	public:
		JavaExtHost();
		~JavaExtHost();	// never waits on the process: its watcher holds the host's state (MangaNet is never destroyed)
		JavaExtHost(const JavaExtHost&) = delete;
		JavaExtHost& operator=(const JavaExtHost&) = delete;
		enum class State
		{
			NotInstalled,	// nothing on the card
			Downloading,	// progress(): bytes so far of the total
			Installed,		// on the card, not running
			Starting,		// the process is up, its API not answering yet (about 25 s)
			Running,		// answering on baseUrl()
			Failed,			// error() says why (a download, a checksum, the process ended)
		};

		// the wake event main registered; the state folder (the add-on lives in <state>/manga/java)
		void configure(Uint32 wakeEvent, const std::string& stateDir);
		State state() const;
		// a stop is on its way (asked for, or the idle one): state() says Starting or Running until the process has gone
		bool stopping() const;
		std::string error() const;
		// while Downloading: bytes done and the total of both files
		void progress(int64_t& done, int64_t& total) const;
		// while Downloading (an install or the runtime upgrade): what it's doing now. Between the two downloads the
		// runtime is unpacked, thousands of small files written with no bytes coming: said, not a bar standing still
		enum class Step { Runtime, Unpacking, Server };
		Step step() const;
		// how much the add-on takes on the card once installed (0: not installed)
		int64_t installedBytes() const;

		// An install from before 0.7.4 runs the server on HotSpot (Temurin): the lighter OpenJ9 runtime replaces it.
		// runtimeOld(): such an install (one stat when the folder is looked at, kept). upgradeRuntime() downloads the
		// new runtime beside the old one while the engine may run; it takes the old one's place at the engine's next
		// start (the old one deleted in the background). upgrade(): its progress (progress() gives the bytes)
		bool runtimeOld() const;
		enum class Upgrade { None, Downloading, Ready, Failed };
		Upgrade upgrade() const;
		std::string upgradeError() const;
		void upgradeRuntime();
		void cancelUpgrade();

		// Install: both files downloaded (resumable, checked), the runtime unpacked; Remove: everything deleted
		// (stopping it first). Both return at once; state() follows.
		void install();
		void cancelInstall();
		void remove();

		// The reader wants a Java source: started if installed and not running (a no-op while Starting/Running).
		// Each call also counts as use: after IDLE_STOP_MS without one, the process is stopped to give its memory back.
		void want();
		static constexpr int64_t IDLE_STOP_MS = 5 * 60 * 1000;
		// main's loop, every pass: the idle stop, the process watched (ended: Failed with its last log line)
		void pump(int64_t nowMs);
		// the extension turned off, or quitting: the process ended now (SIGTERM, then SIGKILL after 3 s, never waited
		// on by the main thread)
		void stop();

		// ---- the GraphQL channel (one request at a time, its own thread and easy handles) ----
		// A POST to <baseUrl>/api/graphql with Content-Type application/json, on 127.0.0.1 only. The serial is
		// returned at once (0: the add-on isn't installed); the answer comes to takeGraphQl() on the main thread,
		// and each answer pushes the wake event configure() named. Using it counts as want(): a call while the
		// server is Starting waits for it, so the caller can ask as soon as a Java source is opened.
		struct GraphQlResult
		{
			uint64_t serial = 0;
			int status = 0;
			std::string body, error;	// error: a transport failure, or a non-200 answer
		};
		uint64_t graphql(const std::string& requestBody);
		// Forget a request: a queued one is dropped, a running one aborted; a cancelled answer is never returned.
		void cancelGraphQl(uint64_t serial);
		std::vector<GraphQlResult> takeGraphQl();

		// Secure DNS (Manga settings): the Java side resolves through Cloudflare over HTTPS from its next start on
		void setSecureDns(bool on);
		// "http://127.0.0.1:<port>" while Running, else ""; the port is chosen free at each start
		std::string baseUrl() const;
		// grows when anything the Ui shows changed (state, progress)
		uint64_t serial() const;

	private:
		struct Impl;
		std::shared_ptr<Impl> impl;	// the threads hold it too: a quit never frees state under them
	};
}
