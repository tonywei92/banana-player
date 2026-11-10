// Saved network connections: servers.conf in the state folder.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	enum class NetType { Smb, Ftp, Dlna };

	struct NetServer
	{
		std::string id, name, host;
		int port = 0;			// 0: the type's default
		NetType type = NetType::Smb;
		bool guest = false;
		std::string user;
		std::string secret;		// the sealed password (secret.h), opaque here
		std::string share;		// SMB: the share to open first
		std::string folder;		// where to start inside it
		bool ftps = false;
		long long lastUsed = 0;
	};

	int defaultPort(NetType t);
	const char* netTypeLabel(NetType t);
	// the name a connection goes by when it's given none (the form's placeholder): the share and the server ("Media on
	// TONY-PC"), the server alone, its port when it isn't the usual one
	std::string automaticServerName(const NetServer& s);
	// its own name, else the automatic one
	std::string serverName(const NetServer& s);

	std::string serializeServers(const std::vector<NetServer>& v);
	std::vector<NetServer> parseServers(const std::string& text);
}
