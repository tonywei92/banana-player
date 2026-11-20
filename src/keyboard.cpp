#include "keyboard.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace bp
{
	// a UTF-8 continuation byte (10xxxxxx): not a place for the caret
	static bool cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

	void TextEdit::insert(const std::string& s)
	{
		caret = std::min(caret, text.size());
		text.insert(caret, s);
		caret += s.size();
	}

	void TextEdit::backspace()
	{
		caret = std::min(caret, text.size());
		if (!caret) return;
		size_t from = caret - 1;
		while (from > 0 && cont(text[from])) from--;
		text.erase(from, caret - from);
		caret = from;
	}

	void TextEdit::left()
	{
		caret = std::min(caret, text.size());
		if (!caret) return;
		caret--;
		while (caret > 0 && cont(text[caret])) caret--;
	}

	void TextEdit::right()
	{
		if (caret >= text.size()) { caret = text.size(); return; }
		caret++;
		while (caret < text.size() && cont(text[caret])) caret++;
	}
}
