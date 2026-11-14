#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	// Where each file was left and which tracks it played, by its full path; and the languages chosen last, which a
	// file never opened before starts with (the next episode in Japanese with English subtitles).
	struct ResumeEntry
	{
		std::string path;
		double position = -1;	// < 0: from the start
		int64_t aid = -1;		// mpv's track id; -1: not kept
		int64_t sid = -1;		// 0: subtitles off; -1: not kept
		double duration = -1;	// its length as last played (Home's Recent cards: "0:27 / 0:52"); < 0: not known
	};

	class ResumeStore
	{
	public:
		static constexpr size_t MAX_ENTRIES = 300;

		void parse(const std::string& text);
		std::string serialize() const;

		const ResumeEntry* find(const std::string& path) const;
		// keeps the entry first (the most recent); the oldest go past MAX_ENTRIES
		void put(const ResumeEntry& e);
		// a file or folder moved (browser.h's movedPath): its entries, and those inside it, follow; true if any did
		bool moved(const std::string& from, const std::string& to);

		// most recent first
		const std::vector<ResumeEntry>& all() const { return entries; }

		std::string lastAudioLang, lastSubLang;
		std::string lastFolder;	// the browser's, when Remember last folder is on
		bool lastSubOff = false;

	private:
		std::vector<ResumeEntry> entries;	// most recent first
	};

	// a position worth coming back to: not the first seconds, not the credits
	bool worthResuming(double position, double duration);

	// a Recent card's progress (Home's strip, the web's Recent): where it was left (position, < 0: none kept; a song
	// always starts at its start), of how long (duration, < 0: not known), how far (frac 0..1); played: none kept, so it
	// was played through (or as good as) or only begun, which the card shows as "Played" with a full bar
	struct RecentProgress { double position = -1, duration = -1, frac = 1; bool played = true; };
	RecentProgress recentProgress(const ResumeEntry* e, bool audio);
}
