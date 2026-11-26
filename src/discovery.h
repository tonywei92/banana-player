#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace bp
{
	// Finding the SMB servers on the LAN by name, with plain sockets: BananaOS has no avahi, nmbd or wsdd. IPv4 only.
	// NetBIOS (UDP 137) finds Windows PCs and NAS boxes, mDNS finds Macs and NAS boxes, WS-Discovery finds Windows 10/11
	// (which no longer answers NetBIOS by default).

	struct FoundHost
	{
		std::string name, ip;
		std::string source; // "NetBIOS", "mDNS", "WSD"; several joined by "+"
	};

	// An OPDS catalog found on the LAN (Calibre's content server announces itself over mDNS)
	struct FoundCatalog
	{
		std::string name, url;
	};

	class Discovery
	{
	public:
		Discovery() = default;
		Discovery(const Discovery&) = delete;
		Discovery& operator=(const Discovery&) = delete;
		~Discovery();

		// Runs on its own thread for `seconds` (longer when a big subnet takes longer to sweep). onFound is called
		// on that thread, once per host and again when a better name arrives; it must not call start() or stop().
		void start(std::function<void(const FoundHost&)> onFound, int seconds = 4);
		// returns within about 100 ms
		void stop();
		bool running() const { return busy; }

	private:
		void run(std::function<void(const FoundHost&)> onFound, int seconds);
		std::thread th;
		std::atomic<bool> quit{false}, busy{false};
	};

	// Asks the LAN for Calibre's catalogs on this thread, for about `ms` (3 s by default); the caller's thread should be
	// a worker. Never throws; returns what answered, once each.
	std::vector<FoundCatalog> findCalibreCatalogs(int ms = 3000);

	// An IPv4 address for a literal, "x.local" or a bare name like "MY-PC" (NetBIOS, LLMNR and mDNS at once, then the
	// system resolver); "" when nothing answers within timeoutMs.
	std::string resolveHost(const std::string& name, int timeoutMs = 1500);

	// The packet builders and parsers, public so the self-test can feed them canned bytes. Packets are std::strings.
	namespace disc
	{
		std::string nbEncode(const std::string& name, int suffix, char pad);
		std::string nbStatQuery(uint16_t id);
		std::string nbNameQuery(const std::string& name, uint16_t id);
		// the computer name from a node-status reply: the unique 0x20 name, else the unique 0x00 one
		bool parseNbStat(const std::string& pkt, std::string& name);
		bool parseNbNameReply(const std::string& pkt, std::string& ip);

		std::string dnsQuery(const std::string& name, uint16_t type, uint16_t cls, uint16_t id = 0);
		// the first A record (for `want`, case-insensitively, when it isn't empty)
		bool parseDnsAddr(const std::string& pkt, const std::string& want, std::string& ip);
		// the SMB servers a response lists; srcIp stands in for an address the response doesn't carry
		void parseMdnsSmb(const std::string& pkt, const std::string& srcIp, std::vector<FoundHost>& out);

		// Calibre's content server (_calibre._tcp): the OPDS catalogs a response lists. Answers can arrive split (the PTR
		// first, its SRV/TXT/A after), so add() each packet and ask result() at the end.
		class CalibreFinder
		{
		public:
			void add(const std::string& pkt, const std::string& srcIp);
			std::vector<FoundCatalog> result() const;
			std::vector<std::string> needSrv() const; // instances a PTR named whose SRV hasn't come (to ask for)
		private:
			struct Inst
			{
				std::string host, path;
				int port = 0;
			};
			std::map<std::string, Inst> insts;	// lowercase instance -> what is known
			std::map<std::string, std::string> display;	// lowercase instance -> its own spelling
			std::map<std::string, std::string> addrs;	// lowercase host -> IPv4
			std::map<std::string, std::string> from;	// lowercase instance -> the address that answered
		};
		// "Books in calibre (on 192 port 8080)" -> "Books in calibre"; the instance's label less ._calibre._tcp.local
		std::string calibreName(const std::string& instance);
		// http://ip:port/path ("/opds" when the TXT gave none)
		std::string calibreUrl(const std::string& ip, int port, const std::string& path);

		std::string wsdProbe(const std::string& msgId);
		bool parseProbeMatch(const std::string& xml, std::string& xaddrs, std::string& endpoint);
		std::string wsdGet(const std::string& to, const std::string& msgId);
		std::string parseComputerName(const std::string& xml);
	}
}
