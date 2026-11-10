#include "input.h"

namespace bp
{
	static Button fromKey(SDL_Keycode k)
	{
		switch (k)
		{
		case SDLK_RETURN: case SDLK_SPACE: return Button::A;
		// the handheld's Back key (Linux's KEY_BACK) too
		case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: return Button::B;
		case SDLK_x: return Button::X;
		case SDLK_y: return Button::Y;
		case SDLK_PAGEUP: case SDLK_q: return Button::L;
		case SDLK_PAGEDOWN: case SDLK_e: return Button::R;
		case SDLK_s: return Button::Start;
		case SDLK_TAB: return Button::Select;
		case SDLK_r: return Button::L3;
		case SDLK_LEFTBRACKET: return Button::L2;
		case SDLK_RIGHTBRACKET: return Button::R2;
		case SDLK_UP: return Button::Up;
		case SDLK_DOWN: return Button::Down;
		case SDLK_LEFT: return Button::Left;
		case SDLK_RIGHT: return Button::Right;
		default: return Button::None;
		}
	}

	// SDL names a pad's face buttons by position, Xbox style: its B is the east button, the handheld's A, its A the
	// south one, B. X and Y come under their printed names (the owner's RG DS: the key printed Y sends SDL's Y; the
	// positional swap made every X hint answer to Y)
	static Button fromPad(Uint8 b)
	{
		switch (b)
		{
		case SDL_CONTROLLER_BUTTON_B: return Button::A;
		case SDL_CONTROLLER_BUTTON_A: return Button::B;
		case SDL_CONTROLLER_BUTTON_X: return Button::X;
		case SDL_CONTROLLER_BUTTON_Y: return Button::Y;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Button::L;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Button::R;
		case SDL_CONTROLLER_BUTTON_START: return Button::Start;
		case SDL_CONTROLLER_BUTTON_BACK: return Button::Select;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK: return Button::L3;
		case SDL_CONTROLLER_BUTTON_DPAD_UP: return Button::Up;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Button::Down;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Button::Left;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Button::Right;
		default: return Button::None;
		}
	}

	Button buttonFromEvent(const SDL_Event& e)
	{
		if (e.type == SDL_KEYDOWN) return fromKey(e.key.keysym.sym);
		if (e.type == SDL_CONTROLLERBUTTONDOWN) return fromPad(e.cbutton.button);
		return Button::None;
	}

	Button buttonReleased(const SDL_Event& e)
	{
		if (e.type == SDL_KEYUP) return fromKey(e.key.keysym.sym);
		if (e.type == SDL_CONTROLLERBUTTONUP) return fromPad(e.cbutton.button);
		return Button::None;
	}

	Button buttonFromName(const std::string& name)
	{
		static const char* names[] = {"none", "a", "b", "x", "y", "l", "r", "up", "down", "left", "right", "start", "select", "l3", "l2", "r2"};
		std::string n;
		for (char c : name) n += (char)SDL_tolower((unsigned char)c);
		for (int i = 1; i < (int)(sizeof names / sizeof *names); i++)
			if (n == names[i]) return (Button)i;
		return Button::None;
	}

	void handlePadHotplug(const SDL_Event& e)
	{
		if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
		else if (e.type == SDL_CONTROLLERDEVICEREMOVED)
		{
			if (SDL_GameController* c = SDL_GameControllerFromInstanceID(e.cdevice.which)) SDL_GameControllerClose(c);
		}
	}
}
