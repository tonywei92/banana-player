#include "keyboard.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace bp
{
	// a UTF-8 continuation byte (10xxxxxx): not a place for the caret
	static bool cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

	// ---- the key pad ----

	namespace
	{
		// the number row, on every layer
		const char* const PAD_DIGITS = "1234567890";
		// each layer's 26 characters, in the rows' order (10, 9, 7). The digits being always there, 123 is every
		// common mark and #+= the six left (braces, backslash, bar, caret, backtick) with the address marks again
		const char* const PAD_CHARS[3] = {
			"qwertyuiopasdfghjklzxcvbnm",
			"-_./:@#&+=?!%*,~()'\"$;<>[]",
			"{}\\|^`<>[]()'\";:,./_-+*&@~",
		};
	}

	KeyPad::Role KeyPad::role(int key) const
	{
		if (key < CHARS) return Char;
		int at = key - CHARS;
		if (at == 0) return Shift;
		if (at == 1) return Symbols;
		if (more && at == 2) return More;
		static const Role TAIL[4] = {Space, Clear, Delete, Done};
		return TAIL[std::clamp(at - toggles(), 0, 3)];
	}

	int KeyPad::keyOf(Role r) const
	{
		for (int i = CHARS; i < keys(); i++)
			if (role(i) == r) return i;
		return -1;
	}

	std::string KeyPad::label(int key) const
	{
		switch (role(key))
		{
		case Char:
		{
			if (key < 0) return "";
			if (key < DIGITS) return std::string(1, PAD_DIGITS[key]);
			char c = PAD_CHARS[std::clamp(layer, 0, 2)][key - DIGITS];
			if (layer == 0 && shift) c = (char)toupper((unsigned char)c);
			return std::string(1, c);
		}
		case Shift: return "Shift";
		case Symbols: return "123";
		case More: return "#+=";
		case Space: return "Space";
		case Clear: return "Clear";
		case Delete: return "Del";
		default: return "Done";
		}
	}

	const char* KeyPad::button(int key) const
	{
		switch (role(key))
		{
		case Shift: return "Y";
		case Symbols: return "L3";
		case Space: return "X";
		case Clear: return "L2";
		case Delete: return "B";
		case Done: return "START";
		default: return "";
		}
	}

	bool KeyPad::lit(int key) const
	{
		Role r = role(key);
		return (r == Shift && shift) || (r == Symbols && layer == 1) || (r == More && layer == 2);
	}

	bool KeyPad::press(int key, TextEdit& text, size_t maxLen, bool digitsOnly)
	{
		if (key < 0 || key >= keys()) return false;
		bool room = !maxLen || text.text.size() < maxLen;
		switch (role(key))
		{
		case Char:
		{
			std::string c = label(key);
			if (room && (!digitsOnly || isdigit((unsigned char)c[0]))) text.insert(c);
			break;
		}
		// capitals are the letters': Shift from a marks layer comes back to them, in capitals
		case Shift:
			shift = layer != 0 ? true : !shift;
			layer = 0;
			break;
		case Symbols: layer = layer == 1 ? 0 : 1; break;
		case More: layer = layer == 2 ? 0 : 2; break;
		case Space: if (room && !digitsOnly) text.insert(" "); break;
		case Clear: text.set(""); break;
		case Delete: text.backspace(); break;
		case Done: return true;
		}
		return false;
	}

	void KeyPad::stepLayer()
	{
		layer = (layer + 1) % toggles();
	}

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
