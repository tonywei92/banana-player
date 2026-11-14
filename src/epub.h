#pragma once
// EPUB: a ZIP (cbz.h reads it) of XHTML chapters, in the order its package (the OPF) gives them: its spine.
// Two ways to read one:
// - pages: a comic or a manga as an EPUB is a picture a chapter (often a fixed layout); epubPages lists the pictures
//   in spine order, and the reader shows them as any book's pages (pagebook.h's EPUB);
// - text: a novel is reflowed by the text viewer (textsource.h): each chapter read into blocks (paragraphs,
//   headings, pictures, bold and italic) when it's first shown. No CSS: the viewer's own font and layout.
#include "cbz.h"
#include "textsource.h"
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	class Epub
	{
	public:
		// false with error() set: not a ZIP, no container.xml, no package, an empty spine
		bool open(const std::string& path);
		const std::string& error() const { return err; }
		const std::string& title() const { return bookTitle; }

		// the spine's documents, in reading order: their names in the archive
		const std::vector<std::string>& spine() const { return docs; }
		// a document's bytes (64 MB at most, as any entry)
		bool read(const std::string& name, std::vector<uint8_t>& out);
		bool readText(const std::string& name, std::string& out);
		// a document's size in the archive (unpacked), for how far through the book a place is
		uint64_t sizeOf(const std::string& name) const;
		// the picture pages, in reading order: each spine document's pictures (img, svg image) and spine
		// entries that are pictures themselves
		std::vector<std::string> pictures();
		// a picture book (a comic): a fixed layout, or chapters of pictures with next to no text
		bool pictureBook();
		// the table of contents (EPUB 3's nav, else EPUB 2's NCX): its titles and the spine document each opens,
		// with the depth of its nesting
		struct Entry { std::string title, doc; int depth = 0; };
		std::vector<Entry> toc();

	private:
		Cbz zip;
		std::string err, opf, opfDir, bookTitle, navDoc, ncxDoc;
		bool fixedLayout = false;
		std::vector<std::string> docs;
		int picturesKnown = -1;	// pictureBook's answer, once
	};

	// a path inside the archive from href, relative to the document base lives in ("OEBPS/Text/c1.xhtml" +
	// "../Images/p1.jpg" -> "OEBPS/Images/p1.jpg"); %-escapes and a #fragment are taken off
	std::string epubResolve(const std::string& base, const std::string& href);

	// a chapter's XHTML read into the text viewer's blocks (pictures named as in the archive)
	std::vector<TextBlock> epubBlocks(const std::string& xhtml, const std::string& docName);

	// an EPUB for the text viewer; null with error set
	std::unique_ptr<TextSource> openEpubText(const std::string& path, std::string& error);
}
