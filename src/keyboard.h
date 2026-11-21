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

	// The on-screen keyboard's keys and what each does, with no window: the one model of the two keyboards (a
	// field's, over the whole panel: ui_keyboard.cpp; Search's, in the shell's body: ui_search.cpp), so both have
	// the same keys, captions and buttons. A row of the digits 1–0, always there whatever else shows (the owner:
	// a number never needs a layer), three rows of characters (10, 9, 7), then the bottom row:
	//   Shift  a toggle: capitals while it is on (lit), until it is pressed again. Not a layer to pick
	//   123    a toggle: the marks while on (lit); again, the letters (its name is the key's usual one)
	//   #+=    (a field's keyboard only) the brackets and rarer marks, the same way
	//   Space, Clear (empties the field), Del (the character before the caret), Done
	// The keys are numbered as lay::keyboard lays them and Kit::keyboard draws them: the characters row by row,
	// then the bottom row from the left.
	struct KeyPad
	{
		enum Role { Char, Shift, Symbols, More, Space, Clear, Delete, Done };
		static constexpr int DIGITS = 10, CHARS = 36;	// the keys before the bottom row: the digits, then a layer's 26
		static constexpr int ROWS[4] = {10, 10, 9, 7};
		static std::vector<int> rows() { return {ROWS[0], ROWS[1], ROWS[2], ROWS[3]}; }	// (lay::keyboard's counts)
		bool more = true;		// has the #+= key (Search's has not: a name needs no brackets)
		bool shift = false;
		int layer = 0;			// 0 letters, 1 the marks (123), 2 the rarer marks (#+=)

		int toggles() const { return more ? 3 : 2; }	// the toggle keys that open the bottom row
		int keys() const { return CHARS + toggles() + 4; }
		Role role(int key) const;
		int keyOf(Role r) const;	// the key with that role (-1: this keyboard has none)
		// what a key shows: its character in the layer and case that are on, or its word ("Shift", "Del")
		std::string label(int key) const;
		// the button that does the same, as its cap names it ("" none): Y Shift, L3 123, X Space, L2 Clear,
		// B Del, START Done
		const char* button(int key) const;
		bool lit(int key) const;	// a toggle that is on
		// what a press of the key does to the text; true: it was Done. maxLen: the longest text (0: any);
		// digitsOnly: a port's field, which takes nothing else (no space either)
		bool press(int key, TextEdit& text, size_t maxLen = 0, bool digitsOnly = false);
		// the buttons' own: L3 steps letters, 123 (and #+=) round
		void stepLayer();
	};
}
