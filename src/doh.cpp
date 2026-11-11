#include "doh.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <limits>

namespace bp
{
	namespace
	{
		uint16_t u16(const uint8_t* p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }

		uint32_t u32(const uint8_t* p)
		{
			return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
		}

		// A name as a key and as the question writes it back: lowercase, without the root's dot.
		std::string lowerName(const std::string& in)
		{
			std::string out = in;
			if (!out.empty() && out.back() == '.') out.pop_back();
			for (char& c : out) c = (char)std::tolower((unsigned char)c);
			return out;
		}

		// A name in the message, compression pointers followed, its labels lowercased and joined by dots. end is
		// where the name at off ends in the message: a pointer counts as its two bytes, not what it points at.
		bool readName(const uint8_t* d, size_t n, size_t off, std::string& out, size_t& end)
		{
			out.clear();
			size_t at = off;
			bool jumped = false;
			int hops = 0;
			for (;;)
			{
				if (at >= n) return false;
				uint8_t len = d[at];
				if ((len & 0xC0) == 0xC0)
				{
					if (at + 1 >= n) return false;
					if (!jumped) end = at + 2;
					jumped = true;
					size_t to = ((size_t)(len & 0x3F) << 8) | d[at + 1];
					// A pointer only ever goes back in a well-formed answer; the hop count ends a loop either way.
					if (to >= n || ++hops > 32) return false;
					at = to;
					continue;
				}
				if ((len & 0xC0) != 0) return false;	// a label form a name doesn't use
				at++;
				if (len == 0) break;
				if (at + len > n) return false;
				if (!out.empty()) out += '.';
				for (size_t i = 0; i < len; i++) out += (char)std::tolower((unsigned char)d[at + i]);
				at += len;
				if (out.size() > 255) return false;
			}
			if (!jumped) end = at;
			return true;
		}

		std::string addressText(uint16_t type, const uint8_t* p)
		{
			char buf[INET6_ADDRSTRLEN] = {};
			if (type == DOH_TYPE_A) return inet_ntop(AF_INET, p, buf, sizeof buf) ? buf : "";
			return inet_ntop(AF_INET6, p, buf, sizeof buf) ? buf : "";
		}
	}

	std::vector<uint8_t> dohQuery(const std::string& hostIn, uint16_t type, uint16_t id)
	{
		std::string host = hostIn;
		if (!host.empty() && host.back() == '.') host.pop_back();
		if (host.empty()) return {};
		std::vector<uint8_t> q;
		q.push_back((uint8_t)(id >> 8));
		q.push_back((uint8_t)id);
		q.push_back(0x01);	// RD: the resolver does the walking
		q.push_back(0x00);
		q.push_back(0x00); q.push_back(0x01);	// one question
		for (int i = 0; i < 6; i++) q.push_back(0);	// no answer, authority or additional records
		size_t start = 0;
		for (;;)
		{
			size_t dot = host.find('.', start);
			size_t len = (dot == std::string::npos ? host.size() : dot) - start;
			// an empty label comes from two dots, or a leading one: never a name
			if (len == 0 || len > 63) return {};
			q.push_back((uint8_t)len);
			q.insert(q.end(), host.begin() + start, host.begin() + start + len);
			if (dot == std::string::npos) break;
			start = dot + 1;
		}
		q.push_back(0);	// the root
		// RFC 1035's limit for a name, its length bytes and the root included
		if (q.size() > 12 + 255) return {};
		q.push_back((uint8_t)(type >> 8));
		q.push_back((uint8_t)type);
		q.push_back(0x00); q.push_back(0x01);	// class IN
		return q;
	}

	DohAnswer parseDohAnswer(const uint8_t* data, size_t size, uint16_t id, const std::string& hostIn, uint16_t type)
	{
		DohAnswer out;
		auto bad = [&out](DohAnswer::Status st, const char* why) {
			out.status = st;
			out.error = why;
			return out;
		};
		if (!data || size < 12) return bad(DohAnswer::Malformed, "the answer is shorter than a DNS header");
		if (u16(data) != id) return bad(DohAnswer::Malformed, "the answer is for another query");
		uint16_t flags = u16(data + 2);
		if ((flags & 0x8000) == 0) return bad(DohAnswer::Malformed, "the answer is not a response");
		if (flags & 0x0200) return bad(DohAnswer::Truncated, "the answer was cut short");
		int rcode = flags & 0x0F;
		if (rcode == 3) return bad(DohAnswer::NameError, "the name does not exist");
		if (rcode != 0) return bad(DohAnswer::Malformed, "the name server refused the query");
		uint16_t qd = u16(data + 4), an = u16(data + 6), ns = u16(data + 8), ar = u16(data + 10);
		std::string asked = lowerName(hostIn);
		size_t at = 12;
		std::string qname;
		for (uint16_t i = 0; i < qd; i++)
		{
			std::string name;
			size_t end = 0;
			if (!readName(data, size, at, name, end) || end + 4 > size)
				return bad(DohAnswer::Malformed, "the answer's question is broken");
			uint16_t qt = u16(data + end);
			if (i == 0)
			{
				qname = name;
				if (qt != type) return bad(DohAnswer::Malformed, "the answer is for another kind of record");
			}
			at = end + 4;
		}
		if (!qname.empty() && qname != asked)
			return bad(DohAnswer::Malformed, "the answer is for another name");
		struct Record
		{
			std::string owner, target, address;
			uint16_t type = 0;
			uint32_t ttl = 0;
		};
		std::vector<Record> records;
		size_t total = (size_t)an + ns + ar;
		for (size_t i = 0; i < total; i++)
		{
			Record rec;
			size_t end = 0;
			if (!readName(data, size, at, rec.owner, end) || end + 10 > size)
				return bad(DohAnswer::Malformed, "a record is broken");
			rec.type = u16(data + end);
			uint16_t klass = u16(data + end + 2);
			rec.ttl = u32(data + end + 4);
			uint16_t rdlen = u16(data + end + 8);
			size_t rd = end + 10;
			if (rd + rdlen > size) return bad(DohAnswer::Malformed, "a record is cut short");
			if (klass == 1)
			{
				if (rec.type == 5)
				{
					size_t targetEnd = 0;
					if (!readName(data, size, rd, rec.target, targetEnd))
						return bad(DohAnswer::Malformed, "a CNAME's name is broken");
				}
				else if (rec.type == DOH_TYPE_A || rec.type == DOH_TYPE_AAAA)
				{
					size_t want = rec.type == DOH_TYPE_A ? 4 : 16;
					if (rdlen != want) return bad(DohAnswer::Malformed, "an address record is the wrong length");
					rec.address = addressText(rec.type, data + rd);
					if (rec.address.empty()) return bad(DohAnswer::Malformed, "an address record can't be read");
				}
			}
			records.push_back(std::move(rec));
			at = rd + rdlen;
		}
		// Follow the CNAME chain from the name asked about, and take the address records at every name on it (a
		// server may put the address beside its CNAME as well as at the end).
		std::string cur = asked.empty() ? qname : asked;
		std::vector<std::string> chain{cur};
		uint32_t ttl = std::numeric_limits<uint32_t>::max();
		for (int hop = 0; hop < 8; hop++)
		{
			const Record* cname = nullptr;
			for (const Record& r : records)
				if (r.type == 5 && r.owner == cur)
				{
					cname = &r;
					break;
				}
			if (!cname || cname->target.empty() || cname->target == cur) break;
			if (std::find(chain.begin(), chain.end(), cname->target) != chain.end())
				return bad(DohAnswer::Malformed, "the answer's names loop");
			ttl = std::min(ttl, cname->ttl);
			cur = cname->target;
			chain.push_back(cur);
		}
		for (const std::string& name : chain)
		{
			for (const Record& r : records)
			{
				if (r.owner != name || r.type != type || r.address.empty()) continue;
				out.addresses.push_back(r.address);
				ttl = std::min(ttl, r.ttl);
			}
		}
		if (out.addresses.empty())
		{
			out.status = DohAnswer::NoRecords;
			return out;
		}
		out.status = DohAnswer::Ok;
		out.ttl = ttl == std::numeric_limits<uint32_t>::max() ? 0 : ttl;
		return out;
	}

	DohAnswer parseDohAnswer(const std::string& body, uint16_t id, const std::string& host, uint16_t type)
	{
		return parseDohAnswer((const uint8_t*)body.data(), body.size(), id, host, type);
	}

	bool DohCache::get(const std::string& host, uint16_t type, double now, std::vector<std::string>& addresses) const
	{
		auto it = entries.find({lowerName(host), type});
		if (it == entries.end() || !(now < it->second.expires)) return false;
		addresses = it->second.addresses;
		return true;
	}

	void DohCache::put(const std::string& host, uint16_t type, const std::vector<std::string>& addresses, uint32_t ttl,
		double now)
	{
		if (addresses.empty()) return;
		uint32_t kept = std::min(ttl, kMaxTtl);
		if (kept == 0) return;	// a TTL of nought means just this answer, not a kept one
		std::pair<std::string, uint16_t> key{lowerName(host), type};
		if (entries.find(key) == entries.end() && entries.size() >= kMaxEntries)
		{
			auto oldest = entries.begin();
			for (auto it = entries.begin(); it != entries.end(); ++it)
				if (it->second.expires < oldest->second.expires) oldest = it;
			entries.erase(oldest);
		}
		Entry entry;
		entry.addresses = addresses;
		entry.expires = now + (double)kept;
		entries[key] = std::move(entry);
	}
}
