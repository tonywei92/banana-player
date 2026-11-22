// The on-screen keyboard over the bottom panel: a form's field, a name, a query typed in. Built on the redesign's kit
// keyboard (lay::keyboard laid out, Kit::keyboard drawn, the touches tested on the same boxes). Its keys are a KeyPad
// (keyboard.h): Shift a toggle that stays lit, 123 and #+= the same (so a host name, a share or a password needs no
// key the keyboard lacks), Space, Clear, Del and Done. Search's own keyboard (ui_search.cpp) is the same pad and the
// same kit piece, less #+=.
#include "ui.h"
#include "format.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_sound.h"
#include "ui_docs.h"
#include "ui_manga.h"

namespace bp
{
	namespace
	{
		const char* fieldName(int f)
		{
			switch (f)
			{
			case FORM_NAME: return "Name";
			case FORM_HOST: return "Server";
			case FORM_PORT: return "Port";
			case FORM_SHARE: return "Share";
			case FORM_FOLDER: return "Folder";
			case FORM_USER: return "User";
			case FORM_PASS: return "Password";
			case EQ_NAME: return "Preset name";
			case PL_NAME: return "Playlist name";
			case FILE_NAME: return "New name";
			case FIND_FIELD: return "Find";
			case SRC_URL_FIELD: return "Repository URL";
			case SRC_SEARCH_FIELD: return "Search sources";
			case MANGA_SEARCH_FIELD: return "Search";
			case MANGA_FILTER_FIELD: return "Filter";
			case MGSRV_NAME_FIELD: return "Server name";
			case MGSRV_URL_FIELD: return "Server address";
			case MGSRV_USER_FIELD: return "User";
			case MGSRV_PASS_FIELD: return "Password";
			default: return "";
			}
		}

		bool isSecret(int f) { return f == FORM_PASS || f == MGSRV_PASS_FIELD; }
	}

	lay::Keyboard Ui::kbLayout() const
	{
		return lay::keyboard({16, 84, 608, 330}, {}, KeyPad::rows(), kbPad.toggles());
	}

	void Ui::openKeyboard(int field, const std::string& text)
	{
		kbActiveAt = SDL_GetTicks();
		kbOpen = true;
		kbField = field;
		kbEdit.set(text);
		kbPad = KeyPad{};
		kbCursor = 0;
		kbPressed = -1;
		pressedBtn = -1;
	}

	void Ui::kbClose(bool keep)
	{
		if (kbField == EQ_NAME)
		{
			// Done: checked by the player first (eqNameDone), which closes it or says what's wrong
			if (keep) { sound->eqNameSubmit = true; return; }
			sound->eqNameNote.clear();
		}
		if (kbField == FIND_FIELD)
		{
			// Find: Done searches on from the place; B keeps the last query as it was
			kbOpen = false;
		kbClosedAt = SDL_GetTicks();
			kbPressed = -1;
			std::string q = trimmed(kbEdit.text);
			if (keep && !q.empty()) docs->findTyped(q);
			return;
		}
		if (kbField == FILE_NAME)
		{
			// a file's new name: checked and done by fileRenameDone, which closes it or says what's wrong
			if (keep) { browse->fileNameSubmit = true; return; }
			browse->fileNameNote.clear();
		}
		if (kbField == PL_NAME)
		{
			// the same for a playlist's name (plNameDone); cancelled, a new one for Add to... adds nothing
			if (keep) { browse->playlists->plNameSubmit = true; return; }
			browse->playlists->plNameNote.clear();
			browse->playlists->plNameThenAdd = false;
		}
		if (kbField == MANGA_SEARCH_FIELD)
		{
			// Done runs the source's search at once; B (a cancelled one) keeps the query that was there
			kbOpen = false;
		kbClosedAt = SDL_GetTicks();
			kbPressed = -1;
			if (keep) manga->mangaSourceSearch(kbEdit.text);
			else browse->browseVersion++;
			return;
		}
		if (kbField == SRC_SEARCH_FIELD)
		{
			// Done narrows Available to the words (none: the whole list again); B keeps the search that was there
			kbOpen = false;
			kbClosedAt = SDL_GetTicks();
			kbPressed = -1;
			if (keep) manga->mangaAvSearchTyped(trimmed(kbEdit.text));
			else browse->browseVersion++;
			return;
		}
		if (kbField == MANGA_FILTER_FIELD)
		{
			// Done writes the text filter and the sheet is under it again; B keeps the value that was there
			kbOpen = false;
		kbClosedAt = SDL_GetTicks();
			kbPressed = -1;
			if (keep) manga->mangaFilterTextTyped(kbEdit.text);
			else browse->browseVersion++;
			return;
		}
		kbOpen = false;
		kbClosedAt = SDL_GetTicks();
		kbPressed = -1;
		if (!keep) { browse->browseVersion++; return; }
		std::string t = kbEdit.text;
		switch (kbField)
		{
		case FORM_NAME: browse->form.name = trimmed(t); break;
		case FORM_HOST: browse->form.host = trimmed(t); break;
		case FORM_PORT: browse->form.port = atoi(t.c_str()); if (browse->form.port <= 0 || browse->form.port > 65535) browse->form.port = defaultPort(browse->form.type); break;
		case FORM_SHARE:
			// "Media", "/Media/", "\\Media": the share's name (a path after it is the start folder)
			for (char& c : t) if (c == '\\') c = '/';
			t = trimmed(t);
			while (!t.empty() && t[0] == '/') t.erase(0, 1);
			while (!t.empty() && t.back() == '/') t.pop_back();
			if (size_t slash = t.find('/'); slash != std::string::npos) { browse->form.folder = t.substr(slash + 1); t = t.substr(0, slash); }
			browse->form.share = t;
			break;
		case FORM_FOLDER: browse->form.folder = trimmed(t); break;
		case FORM_USER: browse->form.user = trimmed(t); break;
		case FORM_PASS: browse->formPassword = t; browse->formPasswordTyped = true; break;	// (spaces kept: they can be a password's)
		// the Sources page's repository URL: added by the next tick, which has the Player for the answer's toast
		case SRC_URL_FIELD: manga->mangaSourcesUrlTyped(trimmed(t)); return;
		// a saved server's form (ui_manga_servers.cpp): the page's own, never a plain password kept anywhere else
		case MGSRV_NAME_FIELD:
		case MGSRV_URL_FIELD:
		case MGSRV_USER_FIELD:
		case MGSRV_PASS_FIELD: manga->mangaServerFieldTyped(kbField, t); return;
		default: break;
		}
		browse->formNote.clear();
		browse->testText.clear();
		browse->formRows();
	}

	bool Ui::kbCaretShown() const
	{
		// on for 530 ms, off for 530, from the last key (a caret that's mid-blink as you type looks lost)
		return (SDL_GetTicks() - kbActiveAt) % 1060 < 530;
	}

	// a key's number: the pad's (the letters first, then the bottom row); a port takes digits only
	void Ui::kbKey(int i)
	{
		if (kbPad.press(i, kbEdit, 0, kbField == FORM_PORT)) kbClose(true);
	}

	void Ui::kbPress(Button b)
	{
		kbActiveAt = SDL_GetTicks();
		lay::Keyboard l = kbLayout();
		switch (b)
		{
		case Button::Up: kbCursor = lay::keyboardMove(l, kbCursor, 0, -1); break;
		case Button::Down: kbCursor = lay::keyboardMove(l, kbCursor, 0, 1); break;
		case Button::Left: kbCursor = lay::keyboardMove(l, kbCursor, -1, 0); break;
		case Button::Right: kbCursor = lay::keyboardMove(l, kbCursor, 1, 0); break;
		case Button::A: kbKey(kbCursor); break;
		// B deletes; with nothing left to delete, it closes (what was there before is kept)
		case Button::B:
			if (kbEdit.text.empty()) kbClose(false);
			else kbEdit.backspace();
			break;
		// the bottom row's keys by their buttons (each key's cap says which)
		case Button::X: kbKey(kbPad.keyOf(KeyPad::Space)); break;
		case Button::Y: kbKey(kbPad.keyOf(KeyPad::Shift)); break;
		case Button::L2: kbKey(kbPad.keyOf(KeyPad::Clear)); break;
		case Button::L3: case Button::Select: kbPad.stepLayer(); break;	// letters, 123, #+=, round
		case Button::L: kbEdit.left(); break;
		case Button::R: kbEdit.right(); break;
		case Button::Start: kbClose(true); break;
		default: break;
		}
	}

	void Ui::kbPointer(const Pointer& pt, int x, int y)
	{
		if (pt.kind == Pointer::Down) kbActiveAt = SDL_GetTicks();
		lay::Keyboard l = kbLayout();
		Rect back = buttonRect(BACK_ICON);
		if (pt.kind == Pointer::Down)
		{
			kbPressed = back.hit(x, y) ? -2 : lay::keyboardHit(l, x, y);
			if (kbPressed >= 0) kbCursor = kbPressed;
		}
		else if (pt.kind == Pointer::Up)
		{
			int was = kbPressed;
			kbPressed = -1;
			// "<": what was typed is let go, the field as it was
			if (was == -2 && back.hit(x, y)) kbClose(false);
			else if (was >= 0 && lay::keyboardHit(l, x, y) == was) kbKey(was);
		}
	}

	void Ui::drawKeyboard()
	{
		fill({0, 0, W, H}, BG);
		Rect back = buttonRect(BACK_ICON);
		kit->iconKey(back, G_BACK, kbPressed == -2);
		drawTextMid(fieldName(kbField), 2, TEXT, 72, 32, 540);
		lay::Keyboard l = kbLayout();
		Kit::KeyboardView v;
		v.title = fieldName(kbField);
		v.value = kbEdit.text;
		v.caret = kbEdit.caret;
		v.secret = isSecret(kbField);
		v.caretOn = kbCaretShown();
		// what the field says about itself, under the keys (a name taken, a repository's address)
		if (kbField == EQ_NAME) v.note = sound->eqNameNote;
		else if (kbField == PL_NAME) v.note = browse->playlists->plNameNote;
		else if (kbField == FILE_NAME) v.note = browse->fileNameNote;
		// both kinds of repository take a plain address: a Lua one's, or an extensions' index (index.pb, index.min.json)
		else if (kbField == SRC_URL_FIELD) v.note = "Any repository URL, including ones ending in index.pb or index.min.json";
		// a search of the sources says how many it has found so far, as it is typed
		else if (kbField == SRC_SEARCH_FIELD) v.note = manga->mangaAvSearchNote(kbEdit.text);
		v.keys(kbPad, kbEdit.text.empty());
		v.cursor = kbCursor;
		v.pressed = kbPressed;
		kit->keyboard(l, v);
		// the kit's hint bar: what the bottom row's caps don't already say (Shift, 123, Space and Clear carry theirs)
		kit->hints({{"A", "Type"}, {"B", kbEdit.text.empty() ? "Close" : "Delete"}, {"L/R", "Move"}, {"START", "Done"}});
	}
}
