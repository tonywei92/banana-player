// The comic servers the reader reads from: Komga, Kavita and OPDS (docs/design/manga.md, section 5.1). They are kept
// in <state>/manga/servers.tsv, one line each, and the password (Kavita's API key) is sealed by secret.* with a key
// file beside it and the handheld's serial, as NetStore keeps the network folders' passwords: a copy of the card's
// folder over BananaOS's own share opens nothing. A Kavita address is kept with {key} where Kavita prints its api
// key, so the key itself only ever lives in the sealed password. No window and no threads: the reader's pages read it.
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

	// A Kavita address read the way the store keeps it. Kavita prints its OPDS address with the api key as a path
	// segment (…/api/opds/<key>[/…]): the url kept has {key} where the key was and `key` comes back for the sealed
	// password. An address naming only the endpoint (…/api/opds) or the server itself keeps {key} too, so a key
	// typed in the form has a place to go. False, both left alone, for a url of neither shape (or one already
	// holding {key}); `key` is "" when the address carried none.
	bool readKavitaUrl(const std::string& url, std::string& storedUrl, std::string& key);

	// One server the user added. The id is ours and never changes; the url never carries a credential.
	struct MangaServer
	{
		std::string id;			// 8 hex characters, ours; the key everywhere
		std::string kind;		// "komga", "kavita" or "opds"
		std::string name;		// what the user calls it (1..40 characters)
		std::string url;		// http(s) with a host, without a user or a password (Kavita's keeps {key} for its api key)
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
