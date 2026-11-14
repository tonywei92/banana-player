#pragma once
// What the text viewer reads (Files: a TXT, a reflowable EPUB): a run of blocks — paragraphs, headings, pictures —
// each found by an id that orders them. The viewer asks for blocks around the place it shows and nothing else, so
// the cost of a step never depends on the book's length: a TXT's ids are byte offsets (TextDoc's pieces), an EPUB's
// are its chapter and the block in it (a chapter is read into blocks when first asked, and a few are kept).
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	struct TextBlock
	{
		enum Kind { Para, H1, H2, H3, Quote, Pre, Image, Rule };
		Kind kind = Para;
		std::string text;	// UTF-8, ready to draw (no tabs, no controls)
		enum Style : uint8_t { Bold = 1, Italic = 2 };
		struct Span { uint32_t from, to; uint8_t style; };	// byte ranges of text drawn bold or italic
		std::vector<Span> spans;
		std::string image;	// Image: its name for TextSource::image
	};

	class TextSource
	{
	public:
		virtual ~TextSource() = default;
		// the first block's id; ids grow along the text. end(): past the last
		virtual uint64_t first() const = 0;
		virtual uint64_t end() const = 0;
		// the block starting at id, and the next block's id (end() past the last); false when id is no block's start
		virtual bool block(uint64_t id, TextBlock& out, uint64_t& next) = 0;
		// the block before the one starting at id; false at the start
		virtual bool before(uint64_t id, uint64_t& prev) = 0;
		// how far through the text a block is (0..1), and the block at such a share (a jump, the progress bar)
		virtual double fraction(uint64_t id) const = 0;
		virtual uint64_t at(double f) = 0;
		// a block's start at or after id (a place kept from before, a contents entry): id itself when it is one
		virtual uint64_t snap(uint64_t id) = 0;
		// a picture's bytes (an EPUB's); false for a TXT
		virtual bool image(const std::string& name, std::vector<uint8_t>& out) { (void)name; (void)out; return false; }
		// the chapters, for the contents list (an EPUB's table of contents; none for a TXT)
		struct Chapter { std::string title; uint64_t id; int depth = 0; };
		virtual std::vector<Chapter> contents() { return {}; }
		// the encoding, as shown, and a code page asked for ("" back to the detected one); TXT only
		virtual std::string encoding() const { return ""; }
		virtual void setEncoding(const std::string&) {}
		virtual std::string title() const { return ""; }
		// a book's blocks are paragraphs (space between them); a TXT's are lines, set close
		virtual bool paragraphs() const { return false; }
		const std::string& error() const { return err; }
		// Find: the block holding query (ASCII letters in any case) after the one starting at from (back: before
		// it), going round past the end (the start); false when the text has none. cancel and progress as TextDoc's
		using Cancel = std::function<bool()>;
		using Progress = std::function<void(double)>;
		virtual bool find(const std::string& query, uint64_t from, bool back, uint64_t& found, const Cancel& cancel,
			const Progress& progress) = 0;

	protected:
		std::string err;
	};

	// a TXT file (textdoc.h); null with error set when it can't be opened
	std::unique_ptr<TextSource> openTextFile(const std::string& path, std::string& error);

	// Find on a copy of its own (a thread's: the viewer's source is the main thread's): the file opened again (an EPUB
	// as a book, a TXT in charset when one was chosen), then find. false: none, or the file can't be opened
	bool findInFile(const std::string& path, bool epub, const std::string& charset, const std::string& query, uint64_t from,
		bool back, uint64_t& found, const TextSource::Cancel& cancel, const TextSource::Progress& progress);
}
