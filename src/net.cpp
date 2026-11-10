// Which backend serves a saved connection: SMB, FTP or DLNA.
#include "net.h"

namespace bp
{
	std::unique_ptr<NetBackend> makeSmbBackend(const NetServer& server, const std::string& password, int timeoutMs);
	std::unique_ptr<NetBackend> makeFtpBackend(const NetServer& server, const std::string& password, int timeoutMs);
	std::unique_ptr<NetBackend> makeDlnaBackend(const NetServer& server, const std::string& password, int timeoutMs);

	std::unique_ptr<NetBackend> makeBackend(const NetServer& server, const std::string& password, int timeoutMs)
	{
		if (server.type == NetType::Dlna) return makeDlnaBackend(server, password, timeoutMs);
		return server.type == NetType::Ftp ? makeFtpBackend(server, password, timeoutMs) : makeSmbBackend(server, password, timeoutMs);
	}
}
