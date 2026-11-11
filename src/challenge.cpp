#include "challenge.h"
#include <cctype>
#include <initializer_list>

namespace bp
{
	namespace
	{
		char lower(char c)
		{
			return (char)std::tolower((unsigned char)c);
		}

		std::string trimmed(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
			while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
			return s.substr(a, b - a);
		}

		bool equalsIgnoreCase(const std::string& a, const char* b)
		{
			size_t n = std::char_traits<char>::length(b);
			if (a.size() != n) return false;
			for (size_t i = 0; i < n; i++)
				if (lower(a[i]) != lower(b[i])) return false;
			return true;
		}

		bool containsIgnoreCase(std::string_view hay, std::string_view needle)
		{
			if (needle.empty() || hay.size() < needle.size()) return false;
			for (size_t i = 0; i + needle.size() <= hay.size(); i++)
			{
				size_t j = 0;
				while (j < needle.size() && lower(hay[i + j]) == lower(needle[j])) j++;
				if (j == needle.size()) return true;
			}
			return false;
		}

		// The last value a name has: a redirect chain earlier hop's header must not answer for the final answer.
		std::string headerValue(const HttpHeaders& headers, const char* name)
		{
			std::string value;
			for (const auto& h : headers)
				if (equalsIgnoreCase(h.first, name)) value = trimmed(h.second);
			return value;
		}

		bool hasBodyMarker(std::string_view body, std::initializer_list<const char*> markers)
		{
			for (const char* marker : markers)
				if (containsIgnoreCase(body, marker)) return true;
			return false;
		}
	}

	BrowserCheck detectBrowserCheck(int status, const HttpHeaders& headers, std::string_view bodyStart)
	{
		const bool refused = status == 403 || status == 503;
		// A Cloudflare managed challenge names itself in this header; the status alone is not the check.
		if (refused && equalsIgnoreCase(headerValue(headers, "cf-mitigated"), "challenge")) return BrowserCheck::Cloudflare;
		// Other challenge pages only say it in the page, and are served by Cloudflare's own front.
		const bool cloudflare = containsIgnoreCase(headerValue(headers, "server"), "cloudflare");
		if (cloudflare && hasBodyMarker(bodyStart, {"Just a moment", "cf_chl_opt", "challenge-platform"}))
			return BrowserCheck::Cloudflare;
		// DDoS-Guard's block page names its own server; its page text and cookie are the fallback when a hop hides it.
		const bool ddosGuard = containsIgnoreCase(headerValue(headers, "server"), "ddos-guard");
		if (refused && (ddosGuard || hasBodyMarker(bodyStart, {"DDoS-Guard", "__ddg"})))
			return BrowserCheck::DdosGuard;
		return BrowserCheck::None;
	}

	const char* browserCheckMessage(BrowserCheck check)
	{
		switch (check)
		{
		case BrowserCheck::Cloudflare:
			return "This source asks for a browser check (Cloudflare). Banana Player can't pass it yet.";
		case BrowserCheck::DdosGuard:
			return "This source asks for a browser check (DDoS-Guard). Banana Player can't pass it yet.";
		default:
			return "";
		}
	}
}
