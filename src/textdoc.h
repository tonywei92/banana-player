#pragma once
// A text file for the TXT viewer, built for files of any size: the file is mapped, never read whole, and nothing
// walks it from the start. A place in it is a byte offset; lines are found around a place on demand (memchr and its
// backward twin), so opening a 1 GB file costs what opening a 1 KB one does, and so does a jump to its middle. A line
// longer than SEG bytes (a file with no line breaks) is cut in pieces of that size, so no step ever lays out more
// than SEG bytes. The encoding is settled once from the first 64 KB: a BOM (UTF-8, UTF-16), valid UTF-8, else the
// code page uchardet guesses (CP1252 without it); a line is converted to UTF-8 only when it's shown.
// No window, no font: the wrapping takes the glyph widths as a function, so core-test runs it as it is.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bp
{
	class TextDoc
	{
	public:
		static constexpr size_t SEG = 16 * 1024;

		TextDoc() = default;
		~TextDoc();
		TextDoc(const TextDoc&) = delete;
		TextDoc& operator=(const TextDoc&) = delete;

		// false with error() set: not there, a folder, unreadable, or not mappable
		bool open(const std::string& path);
		// the same over bytes in memory (core-test); the bytes are copied
		bool openBytes(const std::string& bytes);
		void close();
		bool isOpen() const { return data != nullptr; }
		const std::string& error() const { return err; }
		// the encoding, as shown ("UTF-8", "UTF-16LE", "CP1251"...)
		const std::string& encoding() const { return enc; }
		// a code page asked for (the viewer's Encoding row); "" goes back to the detected one
		void setEncoding(const std::string& charset);

		size_t size() const { return end; }	// where the text ends (begin() is past a BOM)
		size_t begin() const { return start; }

		// a piece of a line: its bytes [from, to) without the line break; next is where the following one starts
		// (size() past the last)
		struct Seg { size_t from = 0, to = 0, next = 0; };
		// the piece holding offset off (a piece starts at a line's start, or SEG bytes into it)
		Seg segAt(size_t off) const;
		// the piece starting at from (from is a piece's start)
		Seg segFrom(size_t from) const;
		// the piece before the one starting at from; false at the text's start
		bool segBefore(size_t from, Seg& out) const;

		// a piece's text in UTF-8, ready to draw: tabs as spaces, a CR dropped, a byte that isn't text as U+FFFD
		std::string text(const Seg& s) const;

		// Find: query (UTF-8; ASCII letters in any case) in the file's own bytes, in [from, to): forward the first
		// match, back the last. The query is put in the file's encoding once, so nothing is decoded or laid out: a
		// scan of the mapped file at memory speed. cancel stops it; progress (0..1 of the span) now and then. at: the
		// match's first byte
		bool find(const std::string& query, size_t from, size_t to, bool back, size_t& at,
			const std::function<bool()>& cancel = {}, const std::function<void(double)>& progress = {}) const;
		// the query in the file's encoding ("" when it can't be written in it)
		std::string encodeQuery(const std::string& utf8) const;

	private:
		size_t lineStart(size_t off) const;	// the start of the line holding off
		size_t findNl(size_t from, size_t limit) const;	// the next line break in [from, limit), or limit
		size_t align(size_t off) const;	// forward to a character's first byte
		void detect();

		const uint8_t* data = nullptr;
		size_t mapped = 0, start = 0, end = 0;
		bool ownsMap = false;
		std::string copy;	// openBytes'
		int unit = 1;		// 2: UTF-16
		bool bigEndian = false;
		std::string enc, detected;
		std::string err;
	};

	// The rows a line wraps into at width pixels: each row's first byte in text (the first is 0; an empty line is one
	// row). adv: a code point's advance in pixels (at: its byte in text, for a styled run's font). A row breaks after the last space (or a hyphen) that fits, else
	// between characters (CJK has no spaces), and always takes at least one character.
	std::vector<uint32_t> wrapRows(const std::string& text, int width, const std::function<int(uint32_t, size_t)>& adv);

	// the next code point of UTF-8 text from i (i moves past it); U+FFFD for a broken sequence
	uint32_t nextCodePoint(const std::string& s, size_t& i);
}
