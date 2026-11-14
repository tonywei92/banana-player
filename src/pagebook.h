#pragma once
// A chapter (or a document) in one file, whatever its kind: a CBZ/ZIP (cbz.h), a CBR/RAR (libarchive, with
// BP_ARCHIVE) or a PDF (pdfium, with BP_PDF). The reader, the card's scan and the covers ask through this, so a new
// kind is one class here. Archives hand out a page's bytes (decoded as any picture); a PDF draws its pages itself
// (rendered()), at the width asked, so it is sharp at any zoom and never holds more than the page asked for.
// Not thread-safe by itself: a book is used under its owner's lock (the reader's), and every PDF call also takes
// pdfium's own process-wide lock, as pdfium is single-threaded.
#include "pagedecode.h"
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	class PageBook
	{
	public:
		virtual ~PageBook() = default;
		// the pages in reading order: an archive's picture entries (isPageImage, sortPages); a PDF's "1" … "N"
		const std::vector<std::string>& pages() const { return names; }
		// an archive entry's bytes (64 MB at most); false with error() set. A PDF has none: render() instead
		virtual bool read(const std::string& name, std::vector<uint8_t>& out) = 0;
		// a PDF: pages are drawn (render), not read
		virtual bool rendered() const { return false; }
		// page index drawn targetW wide (never past 2048, nor maxPixels pixels: narrower then), white under it
		virtual PageImage render(int index, int targetW, long long maxPixels);
		// a page's shape before it is drawn (a PDF's, from its page tree: the strip places every page right away)
		virtual bool pageSize(int index, int& w, int& h);
		// ComicInfo.xml's text, when the archive has one
		bool comicInfo(std::string& xml);
		const std::string& error() const { return err; }

	protected:
		std::vector<std::string> names;
		std::vector<std::string> all;	// every entry's name (an archive's), for comicInfo
		std::string err;
	};

	// by the file's extension: .cbz/.zip, .cbr/.rar, .pdf. null with error set when it can't be opened (or this
	// build reads no such file)
	std::unique_ptr<PageBook> openPageBook(const std::string& path, std::string& error);

	// a file openPageBook takes (what this build reads: .cbz and .zip always, .cbr/.rar and .pdf with their libraries)
	bool isPageBookName(const std::string& name);
	bool isPdfName(const std::string& name);
	// what Files opens in its document viewers: a TXT, an EPUB, a PDF (with pdfium)
	bool isDocumentName(const std::string& name);
	// a comic Files opens with the Manga reader on: a CBZ (or a renamed ZIP? no: a .zip is any archive) or a CBR
	bool isComicName(const std::string& name);
	// the name without its book extension ("Ch 3.cbr" -> "Ch 3"), else the name
	std::string stripBookExt(const std::string& name);

	// one page of a book as pixels (a PDF's drawn, an archive's read and decoded): bookPage, in pagedecode.h
}
