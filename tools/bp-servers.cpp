// bp-servers: saved connections in a state folder, from a terminal (tests, and before the on-screen screens exist)
//   bp-servers STATE_DIR list
//   bp-servers STATE_DIR add smb|ftp NAME HOST PORT USER|-guest PASSWORD [SHARE [FOLDER]]
//   bp-servers STATE_DIR remove ID
//   bp-servers STATE_DIR manga-list
//   bp-servers STATE_DIR manga-add komga|kavita|opds NAME URL USER|- PASSWORD|-   (the manga reader's comic servers;
//                                       a Kavita URL may carry its API key: …/api/opds/<key>, sealed like a password)
#include "mangaservers.h"
#include "netstore.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace bp;

int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: bp-servers STATE_DIR list|add|remove ...\n"); return 2; }
	std::string cmd = argv[2];
	if (cmd == "manga-list" || cmd == "manga-add")
	{
		MangaServerStore ms;
		ms.load(argv[1]);
		if (cmd == "manga-list")
		{
			for (auto& s : ms.list())
				printf("%s  %-8s %-16s %s %s%s\n", s.id.c_str(), s.kind.c_str(), s.name.c_str(), s.url.c_str(), s.user.c_str(),
					s.hasPassword() ? " (password sealed)" : "");
			return 0;
		}
		if (argc < 8) { fprintf(stderr, "manga-add KIND NAME URL USER|- PASSWORD|-\n"); return 2; }
		MangaServer s;
		s.kind = argv[3];
		s.name = argv[4];
		s.url = argv[5];
		if (std::string(argv[6]) != "-") s.user = argv[6];
		std::string id = ms.add(s);
		if (id.empty()) { fprintf(stderr, "not added (a kind, name or URL refused)\n"); return 1; }
		if (std::string(argv[7]) != "-" && !ms.setPassword(id, argv[7])) { fprintf(stderr, "the password wasn't sealed\n"); return 1; }
		ms.save();
		printf("added %s\n", id.c_str());
		return 0;
	}
	NetStore store;
	store.load(argv[1]);
	if (cmd == "list")
		for (auto& s : store.servers())
			printf("%s  %-16s %s %s:%d %s%s\n", s.id.c_str(), s.name.c_str(), netTypeLabel(s.type), s.host.c_str(), s.port,
				s.guest ? "guest" : s.user.c_str(), s.secret.empty() ? "" : " (password sealed)");
	else if (cmd == "add" && argc >= 9)
	{
		NetServer s;
		s.type = std::string(argv[3]) == "ftp" ? NetType::Ftp : NetType::Smb;
		s.name = argv[4];
		s.host = argv[5];
		s.port = atoi(argv[6]);
		s.guest = std::string(argv[7]) == "-guest";
		if (!s.guest) s.user = argv[7];
		if (argc > 9) s.share = argv[9];
		if (argc > 10) s.folder = argv[10];
		store.put(s, s.guest ? "" : argv[8]);
		printf("added\n");
	}
	else if (cmd == "remove" && argc >= 4) { store.remove(argv[3]); printf("removed\n"); }
	else { fprintf(stderr, "bad arguments\n"); return 2; }
	return 0;
}
