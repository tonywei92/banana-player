#include "servers.h"
#include <cstdlib>

namespace bp
{
	int defaultPort(NetType t) { return t == NetType::Ftp ? 21 : t == NetType::Dlna ? 0 : 445; }
	const char* netTypeLabel(NetType t) { return t == NetType::Ftp ? "FTP" : t == NetType::Dlna ? "DLNA" : "SMB"; }

	std::string automaticServerName(const NetServer& s)
	{
		std::string host = s.host.empty() ? std::string(netTypeLabel(s.type)) + " server" : s.host;
		if (s.port > 0 && s.port != defaultPort(s.type)) host += ":" + std::to_string(s.port);
		return s.share.empty() ? host : s.share + " on " + host;
	}

	std::string serverName(const NetServer& s)
	{
		size_t a = s.name.find_first_not_of(" \t");
		return a == std::string::npos ? automaticServerName(s) : s.name;
	}

	// One line per value: backslash, newlines and tabs are escaped, and edge spaces
	// (the reader trims) as \s.
	static std::string esc(const std::string& s)
	{
		std::string out;
		for (size_t i = 0; i < s.size(); i++)
		{
			char c = s[i];
			if (c == '\\') out += "\\\\";
			else if (c == '\n') out += "\\n";
			else if (c == '\r') out += "\\r";
			else if (c == '\t') out += "\\t";
			else if (c == ' ' && (i == 0 || i + 1 == s.size())) out += "\\s";
			else out += c;
		}
		return out;
	}

	static std::string unesc(const std::string& s)
	{
		std::string out;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] == '\\' && i + 1 < s.size())
			{
				char c = s[++i];
				out += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c == 's' ? ' ' : c;
			}
			else out += s[i];
		}
		return out;
	}

	static std::string trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}

	std::string serializeServers(const std::vector<NetServer>& v)
	{
		std::string out;
		for (auto& s : v)
		{
			out += "[server " + s.id + "]\n";
			out += "name = " + esc(s.name) + "\n";
			out += std::string("type = ") + (s.type == NetType::Ftp ? "ftp" : s.type == NetType::Dlna ? "dlna" : "smb") + "\n";
			out += "host = " + esc(s.host) + "\n";
			out += "port = " + std::to_string(s.port) + "\n";
			out += std::string("guest = ") + (s.guest ? "1" : "0") + "\n";
			out += "user = " + esc(s.user) + "\n";
			out += "secret = " + esc(s.secret) + "\n";
			out += "share = " + esc(s.share) + "\n";
			out += "folder = " + esc(s.folder) + "\n";
			out += std::string("ftps = ") + (s.ftps ? "1" : "0") + "\n";
			out += "lastUsed = " + std::to_string(s.lastUsed) + "\n\n";
		}
		return out;
	}

	std::vector<NetServer> parseServers(const std::string& text)
	{
		std::vector<NetServer> out;
		NetServer cur;
		bool in = false;
		auto flush = [&] { if (in && !cur.id.empty()) out.push_back(cur); };
		size_t i = 0;
		while (i < text.size())
		{
			size_t e = text.find('\n', i);
			if (e == std::string::npos) e = text.size();
			std::string line = trim(text.substr(i, e - i));
			i = e + 1;
			if (line.empty() || line[0] == '#') continue;
			if (line[0] == '[')
			{
				flush();
				cur = NetServer();
				in = line.compare(0, 8, "[server ") == 0 && line.back() == ']';
				if (in) cur.id = trim(line.substr(8, line.size() - 9));
				continue;
			}
			size_t eq = line.find('=');
			if (!in || eq == std::string::npos) continue;
			std::string k = trim(line.substr(0, eq)), v = unesc(trim(line.substr(eq + 1)));
			if (k == "name") cur.name = v;
			else if (k == "type") cur.type = v == "ftp" ? NetType::Ftp : v == "dlna" ? NetType::Dlna : NetType::Smb;
			else if (k == "host") cur.host = v;
			else if (k == "port") cur.port = atoi(v.c_str());
			else if (k == "guest") cur.guest = v == "1";
			else if (k == "user") cur.user = v;
			else if (k == "secret") cur.secret = v;
			else if (k == "share") cur.share = v;
			else if (k == "folder") cur.folder = v;
			else if (k == "ftps") cur.ftps = v == "1";
			else if (k == "lastUsed") cur.lastUsed = atoll(v.c_str());
		}
		flush();
		return out;
	}
}
