// The text the on-screen keyboard edits, with no window (the keys: kitlayout's lay::keyboard, drawn by Kit::keyboard).
#pragma once
#include <string>
#include <vector>

namespace bp
{
	// a line of text and its caret (a byte offset, always at a UTF-8 character's start)
	struct TextEdit
	{
		std::string text;
		size_t caret = 0;
		void set(const std::string& s) { text = s; caret = s.size(); }
		void insert(const std::string& s);
		void backspace();	// the character before the caret
		void left();
		void right();
	};
}
