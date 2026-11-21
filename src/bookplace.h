#pragma once
// A book's one reading place, shared by the handheld's readers and the dashboard's (docs/redesign/web-books.md §4).
// It is the document's entry in doc-places.conf (resume.h's ResumeEntry: position the block id or page index, aid the
// kind, duration the share or the page count) with rev / when / by. Every write bumps rev; a browser's write names the
// rev it read (base), so a place read on while the two couldn't talk is caught instead of thrown away (rule 5).
// Pure: the clock comes in on the write.
#include "resume.h"
#include <cstdint>
#include <string>

namespace bp
{
	// ResumeEntry::aid in doc-places.conf: a text's place (a block id and the share) or a page of so many
	enum { PLACE_TEXT = 1, PLACE_PAGES = 2 };

	struct PlaceWrite
	{
		std::string path;
		int kind = PLACE_TEXT;
		double position = 0;	// a text's block id, a book's page index
		double share = 0;		// a text's share (0..1), a book's page count
		uint64_t base = 0;		// the rev the writer last read
		bool force = false;		// the writer chose its place over the stored one (the Two places sheet, Read from the start)
		std::string by;
		int64_t when = 0;
	};

	enum class PlaceVerdict
	{
		Written,	// out is the new place (rev one past the stored one)
		Same,		// the stored one moved since base, but to this very place: nothing to write (out: the stored)
		Conflict,	// it moved since base, elsewhere: the writer asks its reader (out: the stored)
	};

	// a browser's write against what is stored (null: none yet)
	PlaceVerdict mergePlace(const ResumeEntry* stored, const PlaceWrite& w, ResumeEntry& out);
	// the handheld's own write: always taken, the next rev
	ResumeEntry stampPlace(const ResumeEntry* stored, ResumeEntry e, const std::string& by, int64_t when);
	// the same place: kind and position (the share of a text may differ by its layout)
	bool samePlace(const ResumeEntry& a, const ResumeEntry& b);
	// a writer's name as kept: no tabs or line breaks, at most 40 bytes (cut on a UTF-8 character's start)
	std::string placeWriter(const std::string& by);
}
