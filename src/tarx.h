#pragma once
// Unpacking a .tar.gz on the card (docs/design/manga.md 6 and 7): what the Java add-on's runtime arrives in. gzip is
// zlib's; the tar reader is ours because the archive is mine, so the rules fit what is written there: ustar (with its
// prefix field) and GNU long names, regular files and folders only. Symlinks and hard links are skipped, not followed
// or recreated: the card is exFAT and has neither. Every entry's path is checked to stay inside the folder asked for.
#include <string>

namespace bp
{
	// Unpacks archivePath into destDir (made when missing; the archive's own top folder is kept as it names it). On
	// failure error says why and destDir may hold what was read before it; the caller removes it.
	bool extractTarGz(const std::string& archivePath, const std::string& destDir, std::string& error);
}
