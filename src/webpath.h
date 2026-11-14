// What the web dashboard may reach on the card: virtual paths, names and addresses (bpcore, no window).
// Security-critical: tests/core_test.cpp covers every refusal.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	struct WebRoot { std::string name, path; };

	// vpath is "<rootIndex>/<rel>" ("0", "0/" and "0/Music/a.mp3" are fine). Refused: "..", empty parts (a trailing
	// slash is the one exception), parts starting with ".", NUL, a bad index, and anything whose real path (symlinks
	// resolved) is outside realpath(root) or under a `deny` prefix (also resolved). The file must exist, unless
	// forCreate: then the parent must exist, `real` is realpath(parent) + "/" + the last part, and the last part has
	// to pass validName (and can't be the root itself). `real` is set only on success.
	bool resolveVirtual(const std::vector<WebRoot>& roots, const std::vector<std::string>& deny, const std::string& vpath,
		std::string& real, bool forCreate);
	// The virtual path of a real one ("0", "1/Music/a.mp3"); "" if it is outside every root.
	std::string toVirtual(const std::vector<WebRoot>& roots, const std::string& real);
	// toVirtual for a path the dashboard lists without being asked for it (Recent): "" also when it is under a `deny`
	// prefix or has a part starting with "." (resolveVirtual would refuse it). The file need not exist (a gone one).
	std::string servedVirtual(const std::vector<WebRoot>& roots, const std::vector<std::string>& deny, const std::string& real);
	// `real` (already resolved) is a `deny` prefix or inside one: what a walk through a folder must not reach
	bool deniedPath(const std::vector<std::string>& deny, const std::string& real);

	// 1-255 bytes; none of / \ < > : " | ? * or control characters; not blank; not "." or ".."
	bool validName(const std::string& name);

	// loopback, RFC 1918, link-local, unique local (fc00::/7), and IPv4-mapped IPv6 of those; a zone ("%wlan0") is ignored
	bool privateAddress(const std::string& ip);
	// Something a stranger's script may reach (design 5.4): false for everything privateAddress refuses, plus the
	// reserved, documentation, benchmarking, carrier-grade NAT, multicast and broadcast ranges, mapped/6to4 forms
	// whose IPv4 isn't public, and anything that isn't an IP literal. A zone ("%wlan0") is ignored.
	bool publicAddress(const std::string& ip);
	// An IP literal ("192.168.1.2", "192.168.1.2:8080", "[::1]:8080") or "localhost" (any case), with an optional
	// port. A name is refused, so a rebinding page can't reach the player.
	bool hostAllowed(const std::string& hostHeader);
	// wlan* 3, eth*/en* 2, others 1; lo, usb*, docker*, veth* 0 (never shown)
	int interfaceRank(const std::string& ifname);
}
