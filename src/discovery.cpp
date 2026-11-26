#include "discovery.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <map>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <set>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace bp
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		int msSince(Clock::time_point t) { return (int)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count(); }

		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}
		bool iequals(const std::string& a, const std::string& b) { return lower(a) == lower(b); }
		bool endsWithI(const std::string& s, const std::string& suffix)
		{
			return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
		}
		std::string trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && isspace((unsigned char)s[a])) a++;
			while (b > a && isspace((unsigned char)s[b - 1])) b--;
			return s.substr(a, b - a);
		}
		std::string stripLocal(std::string s)
		{
			if (!s.empty() && s.back() == '.') s.pop_back();
			if (endsWithI(s, ".local")) s.resize(s.size() - 6);
			return s;
		}

		std::string ipStr(uint32_t hostOrder)
		{
			char buf[INET_ADDRSTRLEN] = "";
			in_addr a;
			a.s_addr = htonl(hostOrder);
			inet_ntop(AF_INET, &a, buf, sizeof buf);
			return buf;
		}
		uint32_t rd32(const std::string& b, size_t o)
		{
			return ((uint32_t)(uint8_t)b[o] << 24) | ((uint32_t)(uint8_t)b[o + 1] << 16) | ((uint32_t)(uint8_t)b[o + 2] << 8) | (uint8_t)b[o + 3];
		}

		void put16(std::string& s, unsigned v)
		{
			s += (char)(v >> 8);
			s += (char)(v & 255);
		}
		unsigned rd16(const std::string& b, size_t o) { return o + 2 <= b.size() ? ((uint8_t)b[o] << 8 | (uint8_t)b[o + 1]) : 0; }

		unsigned randomId()
		{
			static std::mt19937 rng{std::random_device{}()};
			return (unsigned)(rng() & 0xffff);
		}
		std::string randomUuid()
		{
			static std::mt19937 rng{std::random_device{}()};
			char buf[40];
			snprintf(buf, sizeof buf, "%08x-%04x-4%03x-a%03x-%08x%04x", (unsigned)rng(), (unsigned)(rng() & 0xffff), (unsigned)(rng() & 0xfff),
			         (unsigned)(rng() & 0xfff), (unsigned)rng(), (unsigned)(rng() & 0xffff));
			return buf;
		}

		// A DNS name with its compression pointers followed; every length and jump checked, since a packet from the LAN
		// can say anything. `next` is where the record's own bytes go on after the name.
		bool readName(const std::string& b, size_t off, std::string& out, size_t& next)
		{
			out.clear();
			next = 0;
			bool jumped = false;
			int hops = 0;
			size_t pos = off;
			for (int guard = 0; guard < 256; guard++)
			{
				if (pos >= b.size()) return false;
				unsigned l = (uint8_t)b[pos];
				if (l == 0)
				{
					if (!jumped) next = pos + 1;
					return true;
				}
				if ((l & 0xC0) == 0xC0)
				{
					if (pos + 1 >= b.size()) return false;
					size_t p = ((l & 0x3F) << 8) | (uint8_t)b[pos + 1];
					if (!jumped) next = pos + 2;
					jumped = true;
					if (++hops > 16 || p >= b.size()) return false;
					pos = p;
					continue;
				}
				if (l & 0xC0) return false;
				if (pos + 1 + l > b.size() || out.size() + l + 1 > 255) return false;
				if (!out.empty()) out += '.';
				out.append(b, pos + 1, l);
				pos += 1 + l;
			}
			return false;
		}

		struct Rec
		{
			std::string name;
			unsigned type = 0, cls = 0;
			size_t rd = 0, len = 0;
		};
		// the resource records of a DNS-format packet (NetBIOS uses the same layout); what parses is kept
		bool parseDns(const std::string& b, std::vector<Rec>& out, bool* response = nullptr)
		{
			if (b.size() < 12) return false;
			unsigned qd = rd16(b, 4), an = rd16(b, 6), ns = rd16(b, 8), ar = rd16(b, 10);
			if (response) *response = rd16(b, 2) & 0x8000;
			if (qd + an + ns + ar > 300) return false;
			size_t pos = 12;
			std::string name;
			size_t next;
			for (unsigned i = 0; i < qd; i++)
			{
				if (!readName(b, pos, name, next) || next + 4 > b.size()) return false;
				pos = next + 4;
			}
			for (unsigned i = 0; i < an + ns + ar; i++)
			{
				Rec r;
				if (!readName(b, pos, r.name, next) || next + 10 > b.size()) break;
				r.type = rd16(b, next);
				r.cls = rd16(b, next + 2) & 0x7fff;
				r.len = rd16(b, next + 8);
				r.rd = next + 10;
				if (r.rd + r.len > b.size()) break;
				pos = r.rd + r.len;
				out.push_back(r);
			}
			return true;
		}

		struct Iface
		{
			std::string name;
			uint32_t ip, mask; // host order
			bool mcast;
		};
		std::vector<Iface> listIfaces()
		{
			std::vector<Iface> v;
			ifaddrs* list = nullptr;
			if (getifaddrs(&list) != 0) return v;
			for (ifaddrs* a = list; a; a = a->ifa_next)
			{
				if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask) continue;
				unsigned f = a->ifa_flags;
				if (!(f & IFF_UP) || (f & (IFF_LOOPBACK | IFF_POINTOPOINT))) continue;
				Iface i;
				i.name = a->ifa_name;
				i.ip = ntohl(((sockaddr_in*)a->ifa_addr)->sin_addr.s_addr);
				i.mask = ntohl(((sockaddr_in*)a->ifa_netmask)->sin_addr.s_addr);
				i.mcast = f & IFF_MULTICAST;
				if (i.ip == 0) continue;
				v.push_back(i);
			}
			freeifaddrs(list);
			return v;
		}

		int udpSocket(bool broadcast)
		{
			int fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (fd < 0) return -1;
			fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			int on = 1;
			if (broadcast) setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
			sockaddr_in sa{};
			sa.sin_family = AF_INET;
			if (bind(fd, (sockaddr*)&sa, sizeof sa) != 0)
			{
				close(fd);
				return -1;
			}
			return fd;
		}
		void sendTo(int fd, uint32_t ip, int port, const std::string& data)
		{
			if (fd < 0) return;
			sockaddr_in sa{};
			sa.sin_family = AF_INET;
			sa.sin_port = htons((uint16_t)port);
			sa.sin_addr.s_addr = htonl(ip);
			sendto(fd, data.data(), data.size(), 0, (sockaddr*)&sa, sizeof sa); // a failed send (no route, full queue) is just a host not found
		}
		// multicast goes out of each interface in turn: one socket has only one default
		void sendMulticast(int fd, const std::vector<Iface>& ifs, uint32_t group, int port, const std::string& data)
		{
			if (fd < 0) return;
			for (const Iface& i : ifs)
			{
				if (!i.mcast) continue;
				in_addr a;
				a.s_addr = htonl(i.ip);
				setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &a, sizeof a);
				sendTo(fd, group, port, data);
			}
		}
		void sendBroadcast(int fd, const std::vector<Iface>& ifs, int port, const std::string& data)
		{
			for (const Iface& i : ifs) sendTo(fd, i.ip | ~i.mask, port, data);
		}
		const uint32_t MDNS_GROUP = 0xE00000FB, LLMNR_GROUP = 0xE00000FC, WSD_GROUP = 0xEFFFFFFA;

		template <class F>
		void drain(int fd, F&& handler)
		{
			if (fd < 0) return;
			char buf[8192];
			for (int i = 0; i < 64; i++)
			{
				sockaddr_in sa{};
				socklen_t sl = sizeof sa;
				ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (sockaddr*)&sa, &sl);
				if (n <= 0) break;
				handler(std::string(buf, (size_t)n), ntohl(sa.sin_addr.s_addr));
			}
		}

		// An HTTP POST with a 2 s budget, polling in slices so stop() isn't kept waiting.
		bool httpPost(uint32_t ip, int port, const std::string& path, const std::string& body, const std::atomic<bool>& quit, std::string& resp)
		{
			int fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd < 0) return false;
			fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
			fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
			int on = 1;
			setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
			auto t0 = Clock::now();
			auto wait = [&](short ev) {
				while (!quit && msSince(t0) < 2000)
				{
					pollfd p{fd, ev, 0};
					int r = poll(&p, 1, 100);
					if (r > 0) return true;
					if (r < 0 && errno != EINTR) return false;
				}
				return false;
			};
			bool ok = false;
			sockaddr_in sa{};
			sa.sin_family = AF_INET;
			sa.sin_port = htons((uint16_t)port);
			sa.sin_addr.s_addr = htonl(ip);
			int c = connect(fd, (sockaddr*)&sa, sizeof sa);
			if (c == 0 || (errno == EINPROGRESS && wait(POLLOUT)))
			{
				int err = 0;
				socklen_t el = sizeof err;
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
				if (err == 0)
				{
					std::string req = "POST " + path + " HTTP/1.1\r\nHost: " + ipStr(ip) + ":" + std::to_string(port) +
					                  "\r\nContent-Type: application/soap+xml\r\nContent-Length: " + std::to_string(body.size()) +
					                  "\r\nConnection: close\r\n\r\n" + body;
					size_t sent = 0;
					while (sent < req.size())
					{
						ssize_t n = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
						if (n > 0) sent += (size_t)n;
						else if (n < 0 && (errno == EAGAIN || errno == EINTR)) { if (!wait(POLLOUT)) break; }
						else break;
					}
					if (sent == req.size())
					{
						resp.clear();
						while (resp.size() < 131072 && wait(POLLIN))
						{
							char buf[4096];
							ssize_t n = recv(fd, buf, sizeof buf, 0);
							if (n > 0) resp.append(buf, (size_t)n);
							else if (n == 0 || (errno != EAGAIN && errno != EINTR)) break;
						}
						ok = !resp.empty();
					}
				}
			}
			close(fd);
			return ok;
		}

		// the text of the first element with this local name, whatever its namespace prefix
		bool xmlText(const std::string& x, const char* local, std::string& out, size_t from = 0)
		{
			size_t p = from;
			while ((p = x.find('<', p)) != std::string::npos)
			{
				size_t e = x.find_first_of(" \t\r\n/>", p + 1);
				if (e == std::string::npos) return false;
				char c = p + 1 < x.size() ? x[p + 1] : 0;
				if (c == '/' || c == '?' || c == '!')
				{
					p = e;
					continue;
				}
				std::string tag = x.substr(p + 1, e - p - 1);
				size_t colon = tag.rfind(':');
				if (colon != std::string::npos) tag = tag.substr(colon + 1);
				if (tag == local)
				{
					size_t gt = x.find('>', e);
					if (gt == std::string::npos) return false;
					if (x[gt - 1] == '/')
					{
						out.clear();
						return true;
					}
					size_t end = x.find('<', gt + 1);
					if (end == std::string::npos) return false;
					out = trim(x.substr(gt + 1, end - gt - 1));
					return true;
				}
				p = e;
			}
			return false;
		}

		const char* ORDER[] = {"NetBIOS", "mDNS", "WSD"};
		struct Entry
		{
			std::map<std::string, std::string> names; // by source
			std::string lastName, lastSource;
		};
		// NetBIOS and WSD carry the computer name Windows users know; mDNS only when it's all there is. When another
		// source spells the same name in mixed case ("MacBook-Pro" for "MACBOOK-PRO"), that spelling reads better.
		void pick(const Entry& e, std::string& name, std::string& source)
		{
			static const char* authority[] = {"NetBIOS", "WSD", "mDNS"};
			name.clear();
			for (const char* s : authority)
			{
				auto it = e.names.find(s);
				if (it != e.names.end())
				{
					name = it->second;
					break;
				}
			}
			for (const auto& kv : e.names)
			{
				if (!iequals(kv.second, name)) continue;
				bool allUpper = true;
				for (char c : kv.second)
					if (islower((unsigned char)c)) allUpper = false;
				if (!allUpper)
				{
					name = kv.second;
					break;
				}
			}
			source.clear();
			for (const char* s : ORDER)
				if (e.names.count(s)) source += (source.empty() ? "" : "+") + std::string(s);
		}

		// "http://192.168.1.5:5357/uuid/" -> ip, port, path; only IPv4 literals
		bool parseXAddr(const std::string& u, uint32_t& ip, int& port, std::string& path)
		{
			if (u.compare(0, 7, "http://") != 0) return false;
			size_t slash = u.find('/', 7);
			std::string hostport = u.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
			path = slash == std::string::npos ? "/" : u.substr(slash);
			size_t colon = hostport.find(':');
			port = 80;
			if (colon != std::string::npos)
			{
				port = atoi(hostport.c_str() + colon + 1);
				hostport.resize(colon);
			}
			in_addr a;
			if (port <= 0 || port > 65535 || inet_pton(AF_INET, hostport.c_str(), &a) != 1) return false;
			ip = ntohl(a.s_addr);
			return true;
		}

		struct GaResult
		{
			std::mutex m;
			bool done = false;
			std::string ip;
		};
	}

	namespace disc
	{
		std::string nbEncode(const std::string& name, int suffix, char pad)
		{
			std::string n = name.substr(0, 15);
			n.resize(15, pad);
			n += (char)suffix;
			std::string out;
			for (unsigned char c : n)
			{
				out += (char)('A' + (c >> 4));
				out += (char)('A' + (c & 15));
			}
			return out;
		}
		static std::string nbPacket(uint16_t id, unsigned flags, const std::string& enc, unsigned type)
		{
			std::string p;
			put16(p, id);
			put16(p, flags);
			put16(p, 1);
			put16(p, 0);
			put16(p, 0);
			put16(p, 0);
			p += (char)32;
			p += enc;
			p += '\0';
			put16(p, type);
			put16(p, 1);
			return p;
		}
		std::string nbStatQuery(uint16_t id) { return nbPacket(id, 0, nbEncode("*", 0, '\0'), 0x21); }
		std::string nbNameQuery(const std::string& name, uint16_t id)
		{
			std::string n = name;
			for (char& c : n) c = (char)toupper((unsigned char)c);
			return nbPacket(id, 0x0110, nbEncode(n, 0x20, ' '), 0x20);
		}
		bool parseNbStat(const std::string& pkt, std::string& name)
		{
			std::vector<Rec> recs;
			if (!parseDns(pkt, recs)) return false;
			for (const Rec& r : recs)
			{
				if (r.type != 0x21 || r.len < 1) continue;
				unsigned n = (uint8_t)pkt[r.rd];
				std::string server, station;
				for (unsigned i = 0; i < n && 1 + (i + 1) * 18 <= r.len; i++)
				{
					size_t o = r.rd + 1 + i * 18;
					unsigned suffix = (uint8_t)pkt[o + 15], flags = rd16(pkt, o + 16);
					if (flags & 0x8000) continue; // a group: the workgroup
					std::string nm(pkt, o, 15);
					nm.erase(std::remove(nm.begin(), nm.end(), '\0'), nm.end());
					nm = trim(nm);
					if (nm.empty()) continue;
					if (suffix == 0x20 && server.empty()) server = nm;
					if (suffix == 0x00 && station.empty()) station = nm;
				}
				name = !server.empty() ? server : station;
				return !name.empty();
			}
			return false;
		}
		bool parseNbNameReply(const std::string& pkt, std::string& ip)
		{
			std::vector<Rec> recs;
			if (!parseDns(pkt, recs)) return false;
			for (const Rec& r : recs)
			{
				if (r.type != 0x20 || r.len < 6) continue;
				ip = ipStr(rd32(pkt, r.rd + 2));
				return true;
			}
			return false;
		}

		std::string dnsQuery(const std::string& name, uint16_t type, uint16_t cls, uint16_t id)
		{
			std::string p;
			put16(p, id);
			put16(p, 0);
			put16(p, 1);
			put16(p, 0);
			put16(p, 0);
			put16(p, 0);
			size_t a = 0;
			while (a <= name.size())
			{
				size_t b = name.find('.', a);
				if (b == std::string::npos) b = name.size();
				size_t l = std::min<size_t>(b - a, 63);
				if (l > 0)
				{
					p += (char)l;
					p.append(name, a, l);
				}
				a = b + 1;
			}
			p += '\0';
			put16(p, type);
			put16(p, cls);
			return p;
		}
		bool parseDnsAddr(const std::string& pkt, const std::string& want, std::string& ip)
		{
			std::vector<Rec> recs;
			if (!parseDns(pkt, recs)) return false;
			for (const Rec& r : recs)
			{
				if (r.type != 1 || r.len != 4) continue;
				if (!want.empty() && !iequals(r.name, want)) continue;
				ip = ipStr(rd32(pkt, r.rd));
				return true;
			}
			return false;
		}
		void parseMdnsSmb(const std::string& pkt, const std::string& srcIp, std::vector<FoundHost>& out)
		{
			static const std::string SUFFIX = "._smb._tcp.local";
			std::vector<Rec> recs;
			bool response = false;
			if (!parseDns(pkt, recs, &response) || !response) return;
			std::map<std::string, std::string> srv, addr; // instance -> target host; host -> ip, all lowercase keys
			std::vector<std::string> instances;
			std::string s;
			size_t next;
			for (const Rec& r : recs)
			{
				if (r.type == 12 && iequals(r.name, "_smb._tcp.local") && readName(pkt, r.rd, s, next)) instances.push_back(s);
				else if (r.type == 33 && r.len >= 7 && endsWithI(r.name, SUFFIX) && readName(pkt, r.rd + 6, s, next)) srv[lower(r.name)] = s;
				else if (r.type == 1 && r.len == 4) addr[lower(r.name)] = ipStr(rd32(pkt, r.rd));
			}
			std::set<std::string> done;
			for (const auto& kv : srv)
			{
				auto a = addr.find(lower(kv.second));
				std::string ip = a != addr.end() ? a->second : srcIp;
				if (ip.empty() || stripLocal(kv.second).empty()) continue;
				out.push_back({stripLocal(kv.second), ip, "mDNS"});
				done.insert(kv.first);
			}
			// a PTR whose SRV didn't come with it: the instance's name, at the address that answered
			for (const std::string& inst : instances)
			{
				if (done.count(lower(inst)) || srcIp.empty() || !endsWithI(inst, SUFFIX)) continue;
				std::string nm = inst.substr(0, inst.size() - SUFFIX.size());
				if (!nm.empty()) out.push_back({nm, srcIp, "mDNS"});
			}
		}

		std::string calibreName(const std::string& instance)
		{
			static const std::string SUFFIX = "._calibre._tcp.local";
			std::string n = endsWithI(instance, SUFFIX) ? instance.substr(0, instance.size() - SUFFIX.size()) : instance;
			// "Books in calibre (on host port 8080)": the trailing "(on … port …)" is the host's, which the row shows as the address
			size_t at = n.rfind(" (on ");
			if (at != std::string::npos && n.find(" port ", at) != std::string::npos && n.back() == ')') n.resize(at);
			return trim(n);
		}
		std::string calibreUrl(const std::string& ip, int port, const std::string& path)
		{
			std::string p = path.empty() ? "/opds" : path;
			if (p[0] != '/') p = "/" + p;
			return "http://" + ip + ":" + std::to_string(port) + p;
		}
		void CalibreFinder::add(const std::string& pkt, const std::string& srcIp)
		{
			static const std::string SUFFIX = "._calibre._tcp.local";
			std::vector<Rec> recs;
			bool response = false;
			if (!parseDns(pkt, recs, &response) || !response) return;
			std::string s;
			size_t next;
			for (const Rec& r : recs)
			{
				if (r.type == 12 && iequals(r.name, "_calibre._tcp.local") && readName(pkt, r.rd, s, next) && endsWithI(s, SUFFIX))
				{
					insts[lower(s)];
					display[lower(s)] = s;
					from[lower(s)] = srcIp;
				}
				else if (r.type == 33 && r.len >= 7 && endsWithI(r.name, SUFFIX) && readName(pkt, r.rd + 6, s, next))
				{
					Inst& i = insts[lower(r.name)];
					display.emplace(lower(r.name), r.name);
					i.port = rd16(pkt, r.rd + 4);
					i.host = lower(s);
					from.emplace(lower(r.name), srcIp);
				}
				else if (r.type == 16 && endsWithI(r.name, SUFFIX))
				{
					// length-prefixed "key=value" strings
					size_t o = r.rd, end = r.rd + r.len;
					while (o < end)
					{
						size_t l = (uint8_t)pkt[o++];
						if (o + l > end) break;
						std::string kv(pkt, o, l);
						o += l;
						if (kv.compare(0, 5, "path=") == 0)
						{
							insts[lower(r.name)].path = kv.substr(5);
							display.emplace(lower(r.name), r.name);
						}
					}
				}
				else if (r.type == 1 && r.len == 4) addrs[lower(r.name)] = ipStr(rd32(pkt, r.rd));
			}
		}
		std::vector<std::string> CalibreFinder::needSrv() const
		{
			std::vector<std::string> v;
			for (const auto& kv : insts)
				if (kv.second.port == 0) v.push_back(display.at(kv.first));
			return v;
		}
		std::vector<FoundCatalog> CalibreFinder::result() const
		{
			std::vector<FoundCatalog> out;
			std::set<std::string> seen;
			for (const auto& kv : insts)
			{
				const Inst& i = kv.second;
				if (i.port <= 0) continue;
				auto a = addrs.find(i.host);
				auto f = from.find(kv.first);
				std::string ip = a != addrs.end() ? a->second : f != from.end() ? f->second : "";
				if (ip.empty()) continue;
				std::string url = calibreUrl(ip, i.port, i.path);
				if (!seen.insert(url).second) continue;
				std::string name = calibreName(display.at(kv.first));
				out.push_back({name.empty() ? "Calibre" : name, url});
			}
			return out;
		}

		static const char* WSD_NS =
		    "xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
		    "xmlns:wsd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" xmlns:wsdp=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\" "
		    "xmlns:pub=\"http://schemas.microsoft.com/windows/pub/2005/07\"";
		// laid out as christgau/wsdd's client does
		std::string wsdProbe(const std::string& msgId)
		{
			return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?><soap:Envelope ") + WSD_NS +
			       "><soap:Header><wsa:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
			       "<wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>"
			       "<wsa:MessageID>urn:uuid:" + msgId + "</wsa:MessageID></soap:Header><soap:Body><wsd:Probe>"
			       "<wsd:Types>wsdp:Device pub:Computer</wsd:Types></wsd:Probe></soap:Body></soap:Envelope>";
		}
		bool parseProbeMatch(const std::string& xml, std::string& xaddrs, std::string& endpoint)
		{
			if (xml.find("ProbeMatch") == std::string::npos) return false;
			if (!xmlText(xml, "XAddrs", xaddrs) || xaddrs.empty()) return false;
			endpoint.clear();
			size_t ep = xml.find("EndpointReference");
			xmlText(xml, "Address", endpoint, ep == std::string::npos ? 0 : ep);
			return true;
		}
		std::string wsdGet(const std::string& to, const std::string& msgId)
		{
			return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?><soap:Envelope ") + WSD_NS +
			       "><soap:Header><wsa:To>" + to + "</wsa:To>"
			       "<wsa:Action>http://schemas.xmlsoap.org/ws/2004/09/transfer/Get</wsa:Action>"
			       "<wsa:MessageID>urn:uuid:" + msgId + "</wsa:MessageID>"
			       "<wsa:ReplyTo><wsa:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:Address></wsa:ReplyTo>"
			       "</soap:Header><soap:Body/></soap:Envelope>";
		}
		std::string parseComputerName(const std::string& xml)
		{
			std::string v;
			if (!xmlText(xml, "Computer", v)) return "";
			size_t slash = v.find('/'); // "NAME/Workgroup:WORKGROUP"
			return trim(v.substr(0, slash));
		}
	}

	Discovery::~Discovery() { stop(); }

	void Discovery::stop()
	{
		quit = true;
		if (th.joinable()) th.join();
	}

	void Discovery::start(std::function<void(const FoundHost&)> onFound, int seconds)
	{
		stop();
		quit = false;
		busy = true;
		th = std::thread([this, onFound, seconds] { run(onFound, seconds); });
	}

	void Discovery::run(std::function<void(const FoundHost&)> onFound, int seconds)
	{
		int nb = -1, md = -1, ws = -1;
		try
		{
			std::vector<Iface> ifs = listIfaces();
			std::set<std::string> own;
			for (const Iface& i : ifs) own.insert(ipStr(i.ip));
			nb = udpSocket(true);
			md = udpSocket(false);
			ws = udpSocket(false);

			std::map<std::string, Entry> table;
			auto report = [&](const std::string& ip, const std::string& name, const char* source) {
				if (name.empty() || own.count(ip)) return;
				Entry& e = table[ip];
				e.names[source] = name;
				std::string best, src;
				pick(e, best, src);
				if (best == e.lastName && src == e.lastSource) return;
				e.lastName = best;
				e.lastSource = src;
				if (onFound) onFound({best, ip, src});
			};

			// the /24 around each interface (or its own subnet when that's smaller), not a subnet larger than /22
			std::vector<uint32_t> targets;
			std::set<uint32_t> seen;
			for (const Iface& i : ifs)
			{
				int prefix = 0;
				for (uint32_t m = i.mask; m & 0x80000000u; m <<= 1) prefix++;
				if (prefix < 22 || prefix >= 31) continue;
				uint32_t mask = prefix > 24 ? i.mask : 0xFFFFFF00u;
				uint32_t net = i.ip & mask, last = net | ~mask;
				for (uint32_t h = net + 1; h < last; h++)
					if (h != i.ip && !own.count(ipStr(h)) && seen.insert(h).second) targets.push_back(h);
			}
			const int SWEEP_PER_SEC = 200;
			int sweepMs = (int)(targets.size() * 1000 / SWEEP_PER_SEC);
			int totalMs = std::max(seconds * 1000, sweepMs + 1200);

			const int nbAt[] = {0, 1200}, mdAt[] = {0, 400, 900}, wsAt[] = {0, 1000};
			size_t nbSent = 0, mdSent = 0, wsSent = 0, swept = 0;
			std::set<std::string> fetched;
			auto t0 = Clock::now();
			std::string nbQuery = disc::nbStatQuery((uint16_t)randomId());
			std::string mdQuery = disc::dnsQuery("_smb._tcp.local", 12, 0x8001);

			while (!quit)
			{
				int el = msSince(t0);
				if (el >= totalMs) break;
				if (nbSent < 2 && el >= nbAt[nbSent])
				{
					sendBroadcast(nb, ifs, 137, nbQuery);
					nbSent++;
				}
				if (mdSent < 3 && el >= mdAt[mdSent])
				{
					sendMulticast(md, ifs, MDNS_GROUP, 5353, mdQuery);
					mdSent++;
				}
				if (wsSent < 2 && el >= wsAt[wsSent])
				{
					sendMulticast(ws, ifs, WSD_GROUP, 3702, disc::wsdProbe(randomUuid()));
					wsSent++;
				}
				size_t due = std::min<size_t>(targets.size(), (size_t)el * SWEEP_PER_SEC / 1000 + 1);
				for (; swept < due; swept++) sendTo(nb, targets[swept], 137, nbQuery);

				pollfd fds[3] = {{nb, POLLIN, 0}, {md, POLLIN, 0}, {ws, POLLIN, 0}};
				int r = poll(fds, 3, 20);
				if (r < 0 && errno != EINTR) break;
				if (r <= 0) continue;

				drain(nb, [&](const std::string& p, uint32_t from) {
					std::string name;
					if (disc::parseNbStat(p, name)) report(ipStr(from), name, "NetBIOS");
				});
				drain(md, [&](const std::string& p, uint32_t from) {
					std::vector<FoundHost> found;
					disc::parseMdnsSmb(p, ipStr(from), found);
					for (const FoundHost& h : found) report(h.ip, h.name, "mDNS");
				});
				drain(ws, [&](const std::string& p, uint32_t from) {
					std::string xaddrs, endpoint;
					if (!disc::parseProbeMatch(p, xaddrs, endpoint)) return;
					std::string src = ipStr(from);
					if (own.count(src) || !fetched.insert(src).second) return;
					// only the XAddr that points at the host that answered: one that points elsewhere isn't ours to follow
					size_t a = 0;
					while (a < xaddrs.size() && !quit)
					{
						size_t b = xaddrs.find_first_of(" \t\r\n", a);
						if (b == std::string::npos) b = xaddrs.size();
						std::string u = xaddrs.substr(a, b - a);
						a = b + 1;
						uint32_t ip;
						int port;
						std::string path, resp;
						if (u.empty() || !parseXAddr(u, ip, port, path) || ipStr(ip) != src) continue;
						std::string body = disc::wsdGet(endpoint.empty() ? u : endpoint, randomUuid());
						if (httpPost(ip, port, path, body, quit, resp))
						{
							report(src, disc::parseComputerName(resp), "WSD");
							break;
						}
					}
				});
			}
		}
		catch (...)
		{
		}
		for (int fd : {nb, md, ws})
			if (fd >= 0) close(fd);
		busy = false;
	}

	std::vector<FoundCatalog> findCalibreCatalogs(int ms)
	{
		disc::CalibreFinder finder;
		int md = -1;
		try
		{
			std::vector<Iface> ifs = listIfaces();
			md = udpSocket(false);
			const int at[] = {0, 500, 1200};
			size_t sent = 0;
			auto t0 = Clock::now();
			std::string q = disc::dnsQuery("_calibre._tcp.local", 12, 0x8001);
			std::set<std::string> asked;
			while (true)
			{
				int el = msSince(t0);
				if (el >= ms) break;
				if (sent < 3 && el >= at[sent])
				{
					sendMulticast(md, ifs, MDNS_GROUP, 5353, q);
					sent++;
				}
				pollfd fd = {md, POLLIN, 0};
				int r = poll(&fd, 1, 20);
				if (r < 0 && errno != EINTR) break;
				if (r <= 0) continue;
				drain(md, [&](const std::string& p, uint32_t from) {
					finder.add(p, ipStr(from));
					// a PTR that came without its SRV and TXT: ask for them by name
					for (const std::string& inst : finder.needSrv())
						if (asked.insert(inst).second)
						{
							sendMulticast(md, ifs, MDNS_GROUP, 5353, disc::dnsQuery(inst, 33, 0x8001));
							sendMulticast(md, ifs, MDNS_GROUP, 5353, disc::dnsQuery(inst, 16, 0x8001));
						}
				});
			}
		}
		catch (...)
		{
		}
		if (md >= 0) close(md);
		return finder.result();
	}

	std::string resolveHost(const std::string& rawName, int timeoutMs)
	{
		try
		{
			std::string name = trim(rawName);
			if (name.empty()) return "";
			in_addr lit;
			if (inet_pton(AF_INET, name.c_str(), &lit) == 1) return name;

			// the system resolver runs alongside, on a thread we may abandon: getaddrinfo can't be told to stop
			auto ga = std::make_shared<GaResult>();
			std::thread([ga, name] {
				addrinfo hints{}, *res = nullptr;
				hints.ai_family = AF_INET;
				hints.ai_socktype = SOCK_STREAM;
				std::string ip;
				if (getaddrinfo(name.c_str(), nullptr, &hints, &res) == 0 && res)
				{
					char buf[INET_ADDRSTRLEN] = "";
					inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, buf, sizeof buf);
					ip = buf;
				}
				if (res) freeaddrinfo(res);
				std::lock_guard<std::mutex> lock(ga->m);
				ga->ip = ip;
				ga->done = true;
			}).detach();

			bool isLocal = endsWithI(name, ".local");
			bool bare = name.find('.') == std::string::npos;
			std::vector<Iface> ifs = listIfaces();
			int nb = bare ? udpSocket(true) : -1, ll = bare ? udpSocket(false) : -1, md = (bare || isLocal) ? udpSocket(false) : -1;
			std::string mdName = isLocal ? name : name + ".local";
			std::string result;
			auto t0 = Clock::now();
			int sends = 0;
			while (result.empty() && msSince(t0) < timeoutMs)
			{
				int el = msSince(t0);
				if (sends < 2 && el >= sends * std::max(timeoutMs / 3, 1))
				{
					sends++;
					if (nb >= 0) sendBroadcast(nb, ifs, 137, disc::nbNameQuery(name, (uint16_t)randomId()));
					if (ll >= 0) sendMulticast(ll, ifs, LLMNR_GROUP, 5355, disc::dnsQuery(name, 1, 1, (uint16_t)randomId()));
					if (md >= 0) sendMulticast(md, ifs, MDNS_GROUP, 5353, disc::dnsQuery(mdName, 1, 0x8001));
				}
				pollfd fds[3] = {{nb, POLLIN, 0}, {ll, POLLIN, 0}, {md, POLLIN, 0}};
				poll(fds, 3, 50);
				std::string ip;
				drain(nb, [&](const std::string& p, uint32_t) { if (result.empty() && disc::parseNbNameReply(p, ip)) result = ip; });
				drain(ll, [&](const std::string& p, uint32_t) { if (result.empty() && disc::parseDnsAddr(p, name, ip)) result = ip; });
				drain(md, [&](const std::string& p, uint32_t) { if (result.empty() && disc::parseDnsAddr(p, mdName, ip)) result = ip; });
				if (result.empty())
				{
					std::lock_guard<std::mutex> lock(ga->m);
					if (ga->done && !ga->ip.empty()) result = ga->ip;
				}
			}
			for (int fd : {nb, ll, md})
				if (fd >= 0) close(fd);
			if (result.empty())
			{
				std::lock_guard<std::mutex> lock(ga->m);
				if (ga->done) result = ga->ip;
			}
			return result;
		}
		catch (...)
		{
			return "";
		}
	}
}
