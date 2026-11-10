#include "netresolve.h"
#include "discovery.h"
#include <arpa/inet.h>
#include <chrono>
#include <map>
#include <mutex>

namespace bp
{
	std::string hostAddress(const std::string& host)
	{
		in_addr a;
		if (host.find("://") != std::string::npos) return host;	// a URL (DLNA's description address): curl resolves its host, and asking for the whole URL by name only waited out the timeout
		if (host.empty() || inet_pton(AF_INET, host.c_str(), &a) == 1) return host;
		static std::mutex lock;
		static std::map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point>> known;
		auto now = std::chrono::steady_clock::now();
		{
			std::lock_guard<std::mutex> g(lock);
			auto it = known.find(host);
			if (it != known.end() && now - it->second.second < std::chrono::minutes(5)) return it->second.first;
		}
		std::string ip = resolveHost(host, 2000);
		if (ip.empty()) return host;	// (not kept: the next try asks again)
		std::lock_guard<std::mutex> g(lock);
		known[host] = {ip, now};
		return ip;
	}
}
