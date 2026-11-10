// The comic servers the reader reads from: Komga, Kavita and OPDS (docs/design/manga.md, section 5.1). They are kept
// in <state>/manga/servers.tsv, one line each, and the password (Kavita's API key) is sealed by secret.* with a key
// file beside it and the handheld's serial, as NetStore keeps the network folders' passwords: a copy of the card's
// folder over BananaOS's own share opens nothing. No window and no threads: the reader's pages read it.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	// The kinds the reader knows.
	bool isMangaServerKind(const std::string& kind);
	// The url a server is reached at: http(s) with a host, at most 2048 characters, without spaces or control
	// characters and without credentials (those are never kept).
	bool isMangaServerUrl(const std::string& url);
	// The url without its "user:password@" part, so a credential never ends up in one.
	std::string withoutUrlCredentials(const std::string& url);

	// One server the user added. The id is ours and never changes; the url never carries a credential.
	struct MangaServer
	{
		std::string id;			// 8 hex characters, ours; the key everywhere
		std::string kind;		// "komga", "kavita" or "opds"
		std::string name;		// what the user calls it (1..40 characters)
		std::string url;		// http(s) with a host, without a user or a password
		std::string user;		// the user name, when the server asks for one
		std::string sealedPassword;	// the sealed password (Kavita's API key), opaque here
		bool hasPassword() const { return !sealedPassword.empty(); }
	};

	class MangaServerStore
	{
	public:
		// <stateDir>/manga/servers.tsv and the key file beside it. stateDir "": nothing is kept (a test run).
		bool load(const std::string& stateDir);
		// Written beside and renamed over, readable by its owner only: the file holds sealed passwords.
		bool save() const;

		// A server added (its id) or changed (the same id); one that isn't one of ours is refused ("").
		std::string add(MangaServer s);
		bool update(MangaServer s);
		void remove(const std::string& id);

		const MangaServer* find(const std::string& id) const;
		// The servers by name.
		std::vector<MangaServer> list() const;

		// The server's password (Kavita's API key), opened; "" for a guest or one that won't open (another
		// handheld's copy).
		std::string password(const std::string& id) const;
		// The password sealed before it is kept; "" takes it away. False when the id is unknown or it can't be
		// sealed: a plain password is never kept.
		bool setPassword(const std::string& id, const std::string& plain);

		int version() const { return changes_; }	// grows with every change

	private:
		void parse(const std::string& text);
		std::string serialize() const;

		std::string dir_;	// the state folder; the file is in <dir>/manga
		std::string key_;	// the sealing key (32 bytes), empty when there is none
		int changes_ = 0;
		std::vector<MangaServer> servers_;
	};
}
