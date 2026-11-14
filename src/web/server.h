// The web dashboard's server: cpp-httplib on a thread of its own, the pre-routing gate (private addresses, Host,
// CSRF, the session cookie), the PIN sign-in, the live updates (SSE) and the bridge to the main thread.
//
// Made only when the dashboard is turned on, on the heap, and never destroyed (like the Updater): stop() ends what it
// can in a second, and whatever's still held up (a slow client mid-write) ends with the process, not before it.
//
// Threads: every handler runs on a pool thread. Handlers never touch Player or Ui: what needs them goes through
// call(), which runs a function on the main thread (an SDL user event) and waits for its answer. Filesystem work stays
// on the pool threads; after a change, post(WebCall::Kind::FsChanged, dir) so the bottom panel re-lists that folder.
#pragma once

#include "webpath.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httplib
{
	class Server;
	struct Request;
	struct Response;
}

namespace bp
{
	class Player;
	class Ui;

	// runs on the main thread; returns the JSON answer (WebCall::setStatus for one that isn't 200)
	using WebFn = std::function<std::string(Player&, Ui&)>;

	// One trip to the main thread: an SDL user event of WebConfig::eventType, `code` = (int)kind,
	// data1 = new std::shared_ptr<WebCall> (main takes it with WebCall::take, which frees the holder). A Call waits
	// for its answer; the other kinds are news for the main thread, with nobody waiting
	struct WebCall
	{
		enum class Kind
		{
			Call,				// run fn (WebCall::run)
			FsChanged,			// arg: the real folder whose contents changed (re-list it if the browser shows it)
			ClientConnected,	// a browser signed in (the toast)
			PinRotated,			// too many wrong PINs: a new one, every browser signed out (the toast)
			ClientsChanged,		// an SSE stream opened or closed: clients() changed; with clients, publish() a fresh snapshot
		};
		Kind kind = Kind::Call;
		std::string arg;
		WebFn fn;

		// main thread, Kind::Call: runs fn unless the server already gave up on it (timed out, stopping), and hands
		// the answer back
		void run(Player& p, Ui& u);
		// inside a WebFn: the answer's HTTP status (409 for a stale playlist rev, say); 200 if not called
		static void setStatus(int status);
		// main thread: the call an event carries (frees the heap holder in data1)
		static std::shared_ptr<WebCall> take(void* data1);

		// between run() and the waiting server thread
		std::mutex m;
		std::condition_variable cv;
		bool started = false, done = false, abandoned = false;
		std::string result;
		int status = 200;
	};

	struct WebConfig
	{
		std::string stateDir;			// web.conf (the PIN), web-sessions.tsv; also never served
		std::vector<WebRoot> roots;		// the folders the web sees (the browser's roots minus "/"), as "<index>/<rel>"
		std::vector<std::string> deny;	// more folders never served; the server adds stateDir, /data/home,
										// /data/banana and /data/store
		uint32_t eventType = 0;			// the SDL user event main.cpp registered for the bridge
		int port = 8080;				// the first tried (then the next 9, then any); env BP_WEB_PORT overrides
		std::string webDir;				// serve the app from this folder, not the binary; env BP_WEB_DIR overrides
		WebFn snapshot;					// GET /api/state: the snapshot now (else the last published one)
	};

	class WebServer
	{
	public:
		enum class State { Off, Running, Failed };

		// main thread. false: no port would bind (state() Failed, error() says why). The PIN: env BP_WEB_PIN if valid,
		// else web.conf's, else a new one (saved). Routes: the gate, auth, events and state here, then
		// registerFileRoutes, registerPlayerRoutes, registerPlaylistRoutes
		bool start(const WebConfig& cfg);
		// main thread: every SSE stream is told why (`event: bye`, goodbye) and given 300 ms to close, then waiting calls
		// get 503 at once, the streams that are left end, the listener stops; joins for at most 1 s more. reason:
		// "quit" (the player closes, a restart for an update too) or "off" (the dashboard turned off)
		void stop(const std::string& reason = "quit");
		// any thread: every open SSE stream sends `event: bye` {"reason":…} and ends ("pin": a new PIN signed everyone
		// out). A page tells the handheld's going from a network that dropped by it
		void goodbye(const std::string& reason);
		// this process's random id, sent as X-BP-Boot by GET /api/ping: a different one is a player started again
		static const std::string& bootId();
		State state() const { return st; }
		bool running() const { return st == State::Running; }
		bool stopping() const { return quitting; }
		std::string error() const;
		int port() const { return boundPort; }
		std::string pin() const;
		// a new PIN (saved) and every session revoked: their SSE streams end, their next request is a 401
		std::string newPin();
		// open SSE streams: the browsers watching
		int clients() const { return streams; }
		// that tab's stream is open (its ?tab= id, any session); "" asks for any stream at all
		bool tabConnected(const std::string& tab) const;
		// that tab said goodbye as it closed (POST /api/bye) and hasn't connected since
		bool tabSaidBye(const std::string& tab) const;
		// the pages connected now that can play (their streams' ?play=, on unless 0), in the order they came, each with
		// the name it gave (?name=, "Chrome on Mac"; none: "Browser"; a second of the same name gets " 2"). Only the
		// handheld shows these (START › Playing › Play on)
		struct Tab { std::string id, name; };
		std::vector<Tab> playTabs() const;
		// a tab's name as playTabs gives it (one gone a moment: the name it had), "Browser" if never heard of
		std::string tabName(const std::string& tab) const;
		// the browser's roots changed (a card in or out)
		void setRoots(const std::vector<WebRoot>& roots);
		std::vector<WebRoot> roots() const;

		// main thread: the state snapshot (webstate's toJson); every SSE stream sends it as `event: state`
		void publish(const std::string& json);
		// any thread: an event for every SSE stream: "fs" {"dir":…}, "playlists" {"serial":…}, "recent" {…},
		// "bye" {"reason":"sleep"} (the lid closed: the stream stays open), "awake" {} (back from it). bye and awake are
		// news of the moment: a stream that connects later never gets them replayed (Last-Event-ID)
		void notify(const std::string& event, const std::string& dataJson);

		// the bridge, any server thread: runs fn on the main thread and waits up to timeoutMs. status: fn's, or 503
		// (timed out, stopping, the event queue full) with an {"error":…} body
		std::string call(WebFn fn, int timeoutMs, int& status);
		// news for the main thread, nobody waiting (FsChanged, ClientConnected, PinRotated, ClientsChanged)
		void post(WebCall::Kind kind, const std::string& arg = "");

		// for the route files: register on http() inside register*Routes (start() calls them before it listens).
		// Every /api/* route but /api/login has been through the gate: private address, allowed Host, X-BP and
		// Origin on non-GET, a valid session. A route needs no auth check of its own
		httplib::Server& http() { return *svr; }
		// a valid session cookie (the gate already checked it for /api/*; this answers 401 if not)
		bool authed(const httplib::Request& req, httplib::Response& res);
		static std::string clientIp(const httplib::Request& req);
		// a JSON answer, not cached
		static void json(httplib::Response& res, const std::string& body, int status = 200);
		// {"error": message}
		static void fail(httplib::Response& res, int status, const std::string& message);
		// a virtual path ("<root>/<rel>") to a real one under the roots, never in a denied folder; forCreate: the
		// last part may not exist yet (its parent is checked). false: a 404 already answered
		bool resolve(const std::string& vpath, std::string& real, bool forCreate, httplib::Response& res) const;
		std::string toVirtual(const std::string& real) const;
		// toVirtual, but "" for what the web never serves (a denied folder, a hidden part): for lists it makes itself
		std::string servedVirtual(const std::string& real) const;
		// a real path (resolved) in a denied folder: a copy walking through a folder stops there
		bool denies(const std::string& real) const;

	private:
		struct Event { uint64_t id; std::string name, data; };
		bool gate(const httplib::Request& req, httplib::Response& res);
		bool serveStatic(const httplib::Request& req, httplib::Response& res);
		void routes();
		void login(const httplib::Request& req, httplib::Response& res);
		void logout(const httplib::Request& req, httplib::Response& res);
		void events(const httplib::Request& req, httplib::Response& res);
		bool sessionValid(const std::string& hash);
		void saveSessions();
		void savePin();
		void wakeCalls();

		WebConfig cfg;
		httplib::Server* svr = nullptr;
		std::thread listener;
		std::function<bool(int)> listenEnded;	// waits up to the ms given for the listener to end
		std::atomic<State> st{State::Off};
		std::atomic<bool> quitting{false};
		std::atomic<int> boundPort{0};
		std::atomic<int> streams{0};
		int keepaliveS = 15;
		std::string webVersion;			// X-BP-Version: the app's files as served, hashed at start

		mutable std::mutex lock;		// pin, sessions, PIN gate, roots, error
		std::string curPin, why;
		std::vector<std::string> denied;
		struct Auth;
		std::shared_ptr<Auth> auth;		// SessionStore + PinGate (webauth.h stays out of this header)

		// the SSE hub: one counter for every event's id (Last-Event-ID), the latest state, the last few events
		mutable std::mutex hubLock;
		std::condition_variable hubCv;
		uint64_t lastId = 0, stateId = 0;
		uint64_t authEpoch = 0;			// a session revoked: streams check theirs
		int generation = 0;				// a start, stop or goodbye: the streams of before end
		std::string byeReason;			// why the last goodbye: what those streams say as they end
		// each tab's stream (session + the tab's own id): a tab that reconnects (a reload, a dropped Wi-Fi) ends its
		// old stream at once instead of holding a thread till the next ping finds it gone
		std::map<std::string, uint64_t> tabStreams;	// (a token of 0: the page said goodbye, its stream ending)
		std::map<std::string, std::string> tabPage;	// the page load each tab's stream came from (?pg=): a goodbye names it
		std::set<std::string> byeTabs;	// tabs (their own ids) that said goodbye, till they connect again
		// each tab's name and whether it can play, from its last stream (kept a while after it goes: its name stays known)
		struct TabInfo { std::string name; bool play = true; uint64_t since = 0; };
		std::map<std::string, TabInfo> tabInfo;
		bool tabUpLocked(const std::string& tab) const;
		uint64_t streamSeq = 0;
		std::string stateJson;
		std::deque<Event> recent;

		std::mutex callsLock;
		std::vector<std::weak_ptr<WebCall>> calls;
	};

	// in the route files; start() calls each before it listens
	void registerFileRoutes(WebServer& ws);		// api_files.cpp (B2)
	void registerPlayerRoutes(WebServer& ws);	// api_player.cpp (B3)
	void registerPlaylistRoutes(WebServer& ws);	// api_playlists.cpp (B3)
	void registerMangaRoutes(WebServer& ws);	// api_manga.cpp (section 8)

	// any server thread (api_player.cpp): the handheld stops if it plays real, or a file inside it (a folder), as the
	// web's Stop does (back to the browser); false if it didn't (it plays something else, or nothing). Waits for the
	// main thread (2 s at most): for "Stop and delete", before the file goes
	bool stopIfPlaying(WebServer& ws, const std::string& real);
}
