// The reader's Secure DNS setting (docs/design/manga.md 9): what an RFC 8484 query looks like and what its answer
// says, and the small cache of what was learned. Pure: no socket and no clock, so the network part is only a POST
// and the tests can hand a packet in and a time. bpcore, tested in tests/core_test.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	constexpr uint16_t DOH_TYPE_A = 1, DOH_TYPE_AAAA = 28;

	// The wire query for host and type (RFC 8484's body): header, the name's labels, type, class IN. id comes back in
	// the answer and is checked there. Empty when the name can't be one (an empty or too long label, a name over 255
	// bytes): the caller reports that as a failed lookup instead of sending it.
	std::vector<uint8_t> dohQuery(const std::string& host, uint16_t type, uint16_t id);

	struct DohAnswer
	{
		enum Status { Ok, NoRecords, NameError, Truncated, Malformed };
		Status status = Malformed;
		std::vector<std::string> addresses;	// A and AAAA in the answer's order, the chain's own names only
		uint32_t ttl = 0;			// the shortest TTL of the records that led to them, seconds
		std::string error;			// one line for the log (empty for NoRecords)
	};

	// Reads one answer for host and type. CNAME chains are followed (names may be compressed), and the addresses are
	// taken at the chain's end, or beside a hop that carries them. NXDOMAIN is NameError, an answer with no address
	// record (NODATA) is NoRecords, and the truncation bit or a packet that doesn't parse is Truncated / Malformed.
	DohAnswer parseDohAnswer(const uint8_t* data, size_t size, uint16_t id, const std::string& host, uint16_t type);
	DohAnswer parseDohAnswer(const std::string& body, uint16_t id, const std::string& host, uint16_t type);

	// What was looked up in the last minutes, in memory only. The network part asks it first, so a grid's many
	// requests to one host cost one lookup; a TTL is capped at kMaxTtl so a site that moves is seen again soon.
	class DohCache
	{
	public:
		static constexpr uint32_t kMaxTtl = 300;	// a few minutes
		static constexpr size_t kMaxEntries = 64;

		// now is any monotonic clock in seconds (never the wall clock: a clock set back mustn't keep a stale name)
		bool get(const std::string& host, uint16_t type, double now, std::vector<std::string>& addresses) const;
		void put(const std::string& host, uint16_t type, const std::vector<std::string>& addresses, uint32_t ttl, double now);
		void clear() { entries.clear(); }
		size_t size() const { return entries.size(); }

	private:
		struct Entry
		{
			std::vector<std::string> addresses;
			double expires = 0;
		};
		std::map<std::pair<std::string, uint16_t>, Entry> entries;
	};
}
