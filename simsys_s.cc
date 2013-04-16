/*
 * Copyright (c) 1997 - 2001 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic license.
 */

#include "SDL.h"

#ifdef _WIN32
// windows.h defines min and max macros which we don't want
#define NOMINMAX 1
#include <windows.h>
#endif

#include <stdio.h>

#include "macros.h"
#include "simsys_w32_png.h"
#include "simversion.h"
#include "simsys.h"
#include "simevent.h"
#include "simgraph.h"
#include "simdebug.h"

static Uint8 hourglass_cursor[] = {
	0x3F, 0xFE, //   *************
	0x30, 0x06, //   **         **
	0x3F, 0xFE, //   *************
	0x10, 0x04, //    *         *
	0x10, 0x04, //    *         *
	0x12, 0xA4, //    *  * * *  *
	0x11, 0x44, //    *  * * *  *
	0x18, 0x8C, //    **   *   **
	0x0C, 0x18, //     **     **
	0x06, 0xB0, //      ** * **
	0x03, 0x60, //       ** **
	0x03, 0x60, //       **H**
	0x06, 0x30, //      ** * **
	0x0C, 0x98, //     **     **
	0x18, 0x0C, //    **   *   **
	0x10, 0x84, //    *    *    *
	0x11, 0x44, //    *   * *   *
	0x12, 0xA4, //    *  * * *  *
	0x15, 0x54, //    * * * * * *
	0x3F, 0xFE, //   *************
	0x30, 0x06, //   **         **
	0x3F, 0xFE  //   *************
};

static Uint8 hourglass_cursor_mask[] = {
	0x3F, 0xFE, //   *************
	0x3F, 0xFE, //   *************
	0x3F, 0xFE, //   *************
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x0F, 0xF8, //     *********
	0x07, 0xF0, //      *******
	0x03, 0xE0, //       *****
	0x03, 0xE0, //       **H**
	0x07, 0xF0, //      *******
	0x0F, 0xF8, //     *********
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x1F, 0xFC, //    ***********
	0x3F, 0xFE, //   *************
	0x3F, 0xFE, //   *************
	0x3F, 0xFE  //   *************
};

static Uint8 blank_cursor[] = {
	0x0,
	0x0,
};

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Surface *screen;
static SDL_Texture *screen_tx;
static int width = 16;
static int height = 16;

// switch off is a little faster (<3%)
static int sync_blit = 0;

static SDL_Cursor* arrow;
static SDL_Cursor* hourglass;
static SDL_Cursor* blank;

// Color component bitmasks for the RGB555 pixel format used by simgraph16.cc
#define RMASK 0x7c00  // 0x1f << 10
#define GMASK 0x03E0  // 0x1f << 5
#define BMASK 0x001F

// Drop events that are fired frequently that we aren't interested in.
// The internal_GetEvents function expects that events excluding SDL_MOUSEMOTION
// come in approximately one at a time and are all more or less interesting.
static int filter_SDL_event(void* /* unused userdata */, SDL_Event* event)
{
	switch (event->type) {
	case SDL_FINGERDOWN:
	case SDL_FINGERUP:
	case SDL_FINGERMOTION:
	case SDL_MULTIGESTURE:
		return 0;
	default:
		return 1;
	}
}

/*
 * Hier sind die Basisfunktionen zur Initialisierung der
 * Schnittstelle untergebracht
 * -> init,open,close
 */
bool dr_os_init(const int* parameter)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) != 0) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
		return false;
	}

	sync_blit = parameter[1];

	// prepare for next event
	sys_event.type = SIM_NOEVENT;
	sys_event.code = 0;

	SDL_SetEventFilter(filter_SDL_event, NULL);

	atexit(SDL_Quit); // clean up on exit
	return true;
}


resolution dr_query_screen_resolution()
{
	resolution res;
	SDL_DisplayMode mode;
	SDL_GetCurrentDisplayMode(0, &mode);
	DBG_MESSAGE("dr_query_screen_resolution(SDL)", "screen resolution width=%d, height=%d", mode.w, mode.h);
	res.w = mode.w;
	res.h = mode.h;
	return res;
}

bool internal_create_surfaces() {
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (renderer == NULL) {
		fprintf(stderr, "Couldn't create renderer: %s\n", SDL_GetError());
		return false;
	}

	screen_tx = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB555, SDL_TEXTUREACCESS_STREAMING, width, height);
	if (screen_tx == NULL) {
		fprintf(stderr, "Couldn't create texture: %s\n", SDL_GetError());
		return false;
	}

	// Use bitmasks appropriate for RGB555 mode. (Alpha is handled below the SDL level,
	// within simgraph16.cc.)
	screen = SDL_CreateRGBSurface(0, width, height, COLOUR_DEPTH, RMASK, GMASK, BMASK, 0);
	if (screen == NULL) {
		fprintf(stderr, "Couldn't get the window surface: %s\n", SDL_GetError());
		return false;
	}

	return true;
}

// open the window
int dr_os_open(int w, int const h, int const fullscreen)
{
	// some cards need those alignments
	// especially 64bit want a border of 8bytes
	w = (w + 15) & 0x7FF0;
	if(  w<=0  ) {
		w = 16;
	}

	width = w;
	height = h;

	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;
	flags |= (fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_RESIZABLE);
	window = SDL_CreateWindow(SIM_TITLE, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags);
	if (window == NULL) {
		fprintf(stderr, "Couldn't open the window: %s\n", SDL_GetError());
		return 0;
	}

	if (!internal_create_surfaces()) {
		return 0;
	}
	fprintf(stderr, "Screen Flags: %x\n", screen->flags);
	DBG_MESSAGE("dr_os_open(SDL)", "SDL realized screen size width=%d, height=%d (requested w=%d, h=%d)", screen->w, screen->h, w, h);

	SDL_ShowCursor(0);
	arrow = SDL_GetCursor();
	hourglass = SDL_CreateCursor(hourglass_cursor, hourglass_cursor_mask, 16, 22, 8, 11);
	blank = SDL_CreateCursor(blank_cursor, blank_cursor, 8, 2, 0, 0);

	SDL_ShowCursor(1);

	display_set_actual_width( w );
	return w;
}


// shut down SDL
void dr_os_close()
{
	SDL_FreeCursor(hourglass);
	SDL_FreeCursor(blank);
	SDL_DestroyTexture(screen_tx);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}


void internal_lock() {
	SDL_LockTexture(screen_tx, NULL, &screen->pixels, &screen->pitch);
}


void internal_unlock() {
	SDL_UnlockTexture(screen_tx);
}


// resizes screen
int dr_textur_resize(unsigned short** const textur, int w, int const h)
{
	internal_unlock();

	display_set_actual_width( w );
	// some cards need those alignments
	// especially 64bit want a border of 8bytes
	w = (w + 15) & 0x7FF0;
	if(  w<=0  ) {
		w = 16;
	}

	if(  w!=screen->w  ||  h!=screen->h  ) {

		width = w;
		height = h;

		SDL_SetWindowSize(window, w, h);
		// Recreate the SDL surfaces at the new resolution.
		SDL_DestroyTexture(screen_tx);
		SDL_DestroyRenderer(renderer);
		internal_create_surfaces();
		printf("textur_resize()::screen=%p\n", screen);
		if (screen) {
			DBG_MESSAGE("dr_textur_resize(SDL)", "SDL realized screen size width=%d, height=%d (requested w=%d, h=%d)", screen->w, screen->h, w, h);
		}
		else {
			if (dbg) {
				dbg->warning("dr_textur_resize(SDL)", "screen is NULL. Good luck!");
			}
		}
		fflush(NULL);
	}
	*textur = dr_textur_init();
	return w;
}


unsigned short *dr_textur_init()
{
	internal_lock();
	return (unsigned short*)screen->pixels;
}


/**
 * Transform a 24 bit RGB color into the system format.
 * @return converted color value
 * @author Hj. Malthaner
 */
unsigned int get_system_color(unsigned int r, unsigned int g, unsigned int b)
{
	return SDL_MapRGB(screen->format, (Uint8)r, (Uint8)g, (Uint8)b);
}


void dr_prepare_flush()
{
	return;
}



void dr_flush(void)
{
	display_flush_buffer();
	internal_unlock();
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, screen_tx, NULL, NULL);
	SDL_RenderPresent(renderer);
	internal_lock();
}


void dr_textur(int xp, int yp, int w, int h)
{
	// TODO: remove this method. The SDL2 port always uses SDL_RenderPresent
	// to render the entire screen.
}


// move cursor to the specified location
void move_pointer(int x, int y)
{
	SDL_WarpMouseInWindow(window, x, y);
}


// set the mouse cursor (pointer/load)
void set_pointer(int loading)
{
	SDL_SetCursor(loading ? hourglass : arrow);
}


/**
 * Some wrappers can save screenshots.
 * @return 1 on success, 0 if not implemented for a particular wrapper and -1
 *         in case of error.
 * @author Hj. Malthaner
 */
int dr_screenshot(const char *filename, int x, int y, int w, int h)
{
#ifdef WIN32
	if(  dr_screenshot_png(filename, w, h, width, ((unsigned short *)(screen->pixels))+x+y*width, screen->format->BitsPerPixel )  ) {
		return 1;
	}
#endif
	return SDL_SaveBMP(screen, filename) == 0 ? 1 : -1;
}


/*
 * Hier sind die Funktionen zur Messageverarbeitung
 */


static inline unsigned int ModifierKeys(void)
{
	SDL_Keymod mod = SDL_GetModState();

	return
		(mod & KMOD_SHIFT ? 1 : 0) |
		(mod & KMOD_CTRL  ? 2 : 0);
}


static int conv_mouse_buttons(Uint8 const state)
{
	return
		(state & SDL_BUTTON_LMASK ? MOUSE_LEFTBUTTON  : 0) |
		(state & SDL_BUTTON_MMASK ? MOUSE_MIDBUTTON   : 0) |
		(state & SDL_BUTTON_RMASK ? MOUSE_RIGHTBUTTON : 0);
}


static void internal_GetEvents(bool const wait)
{
	SDL_Event event;
	event.type = 1;

	if (wait) {
		int n;

		do {
			SDL_WaitEvent(&event);
			n = SDL_PollEvent(NULL);
		} while (n != 0 && event.type == SDL_MOUSEMOTION);
	}
	else {
		int n;
		bool got_one = false;

		do {
			n = SDL_PollEvent(&event);

			if (n != 0) {
				got_one = true;

				if (event.type == SDL_MOUSEMOTION) {
					sys_event.type = SIM_MOUSE_MOVE;
					sys_event.code = SIM_MOUSE_MOVED;
					sys_event.mx   = event.motion.x;
					sys_event.my   = event.motion.y;
					sys_event.mb   = conv_mouse_buttons(event.motion.state);
				}
			}
		} while (n != 0 && event.type == SDL_MOUSEMOTION);

		if (!got_one) return;
	}

	switch (event.type) {
		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
				sys_event.type = SIM_SYSTEM;
				sys_event.code = SIM_SYSTEM_RESIZE;
				sys_event.mx   = event.window.data1;
				sys_event.my   = event.window.data2;
				printf("expose: x=%i, y=%i\n", sys_event.mx, sys_event.my);
				break;
			} else {
				// will be ignored ...
				sys_event.type = SIM_SYSTEM;
				sys_event.code = SIM_SYSTEM_UPDATE;
			}
			break;
		case SDL_SYSWMEVENT:
			// will be ignored ...
			sys_event.type = SIM_SYSTEM;
			sys_event.code = SIM_SYSTEM_UPDATE;
			break;

		case SDL_MOUSEBUTTONDOWN:
			sys_event.type    = SIM_MOUSE_BUTTONS;
			sys_event.key_mod = ModifierKeys();
			sys_event.mx      = event.button.x;
			sys_event.my      = event.button.y;
			sys_event.mb      = conv_mouse_buttons(SDL_GetMouseState(0, 0));
			switch (event.button.button) {
				case SDL_BUTTON_LEFT: sys_event.code = SIM_MOUSE_LEFTBUTTON;  break;
				case SDL_BUTTON_MIDDLE: sys_event.code = SIM_MOUSE_MIDBUTTON;   break;
				case SDL_BUTTON_RIGHT: sys_event.code = SIM_MOUSE_RIGHTBUTTON; break;
				// Probably wrong
				case SDL_BUTTON_X1: sys_event.code = SIM_MOUSE_WHEELUP;     break;
				case SDL_BUTTON_X2: sys_event.code = SIM_MOUSE_WHEELDOWN;   break;
			}
			break;

		case SDL_MOUSEBUTTONUP:
			dbg->warning("internal_GetEvents", "Got mouse event, x=%d, y=%d", event.button.x, event.button.y);
			sys_event.type    = SIM_MOUSE_BUTTONS;
			sys_event.key_mod = ModifierKeys();
			sys_event.mx      = event.button.x;
			sys_event.my      = event.button.y;
			sys_event.mb      = conv_mouse_buttons(SDL_GetMouseState(0, 0));
			switch (event.button.button) {
				case SDL_BUTTON_LEFT: sys_event.code = SIM_MOUSE_LEFTUP;  break;
				case SDL_BUTTON_MIDDLE: sys_event.code = SIM_MOUSE_MIDUP;   break;
				case SDL_BUTTON_RIGHT: sys_event.code = SIM_MOUSE_RIGHTUP; break;
			}
			break;

		case SDL_KEYDOWN:
		{
			unsigned long code;
#ifdef _WIN32
			// SDL doesn't set numlock state correctly on startup. Revert to win32 function as workaround.
			const bool numlock = (GetKeyState(VK_NUMLOCK) & 1) != 0;
#else
			const bool numlock = SDL_GetModState() & KMOD_NUM;
#endif
			SDL_Keycode sym = event.key.keysym.sym;
			switch (sym) {
				case SDLK_DELETE:   code = 127;                           break;
				case SDLK_DOWN:     code = SIM_KEY_DOWN;                  break;
				case SDLK_END:      code = SIM_KEY_END;                   break;
				case SDLK_HOME:     code = SIM_KEY_HOME;                  break;
				case SDLK_F1:       code = SIM_KEY_F1;                    break;
				case SDLK_F2:       code = SIM_KEY_F2;                    break;
				case SDLK_F3:       code = SIM_KEY_F3;                    break;
				case SDLK_F4:       code = SIM_KEY_F4;                    break;
				case SDLK_F5:       code = SIM_KEY_F5;                    break;
				case SDLK_F6:       code = SIM_KEY_F6;                    break;
				case SDLK_F7:       code = SIM_KEY_F7;                    break;
				case SDLK_F8:       code = SIM_KEY_F8;                    break;
				case SDLK_F9:       code = SIM_KEY_F9;                    break;
				case SDLK_F10:      code = SIM_KEY_F10;                   break;
				case SDLK_F11:      code = SIM_KEY_F11;                   break;
				case SDLK_F12:      code = SIM_KEY_F12;                   break;
				case SDLK_F13:      code = SIM_KEY_F13;                   break;
				case SDLK_F14:      code = SIM_KEY_F14;                   break;
				case SDLK_F15:      code = SIM_KEY_F15;                   break;
				case SDLK_KP_0:     code = numlock ? '0' : 0;             break;
				case SDLK_KP_1:     code = numlock ? '1' : SIM_KEY_END;   break;
				case SDLK_KP_2:     code = numlock ? '2' : SIM_KEY_DOWN;  break;
				case SDLK_KP_3:     code = numlock ? '3' : '<';           break;
				case SDLK_KP_4:     code = numlock ? '4' : SIM_KEY_LEFT;  break;
				case SDLK_KP_5:     code = numlock ? '5' : 0;             break;
				case SDLK_KP_6:     code = numlock ? '6' : SIM_KEY_RIGHT; break;
				case SDLK_KP_7:     code = numlock ? '7' : SIM_KEY_HOME;  break;
				case SDLK_KP_8:     code = numlock ? '8' : SIM_KEY_UP;    break;
				case SDLK_KP_9:     code = numlock ? '9' : '>';           break;
				case SDLK_LEFT:     code = SIM_KEY_LEFT;                  break;
				case SDLK_PAGEDOWN: code = '<';                           break;
				case SDLK_PAGEUP:   code = '>';                           break;
				case SDLK_RIGHT:    code = SIM_KEY_RIGHT;                 break;
				case SDLK_UP:       code = SIM_KEY_UP;                    break;
				case SDLK_PAUSE:    code = 16;                            break;

				default:
					if (event.key.keysym.unicode != 0) {
						code = event.key.keysym.unicode;
						if (event.key.keysym.unicode == 22 /* ^V */) {
#if 0	// disabled as internal buffer is used; code is retained for possible future implementation of dr_paste()
							// X11 magic ... not tested yet!
							SDL_SysWMinfo si;
							if (SDL_GetWMInfo(&si) && si.subsystem == SDL_SYSWM_X11) {
								// clipboard under X11
								XEvent         evt;
								Atom           sseln   = XA_CLIPBOARD(si.x11.display);
								Atom           target  = XA_STRING;
								unsigned char* sel_buf = 0;
								unsigned long  sel_len = 0;	/* length of sel_buf */
								unsigned int   context = XCLIB_XCOUT_NONE;
								xcout(si.x11.display, si.x11.window, evt, sseln, target, &sel_buf, &sel_len, &context);
								/* fallback is needed. set XA_STRING to target and restart the loop. */
								if (context != XCLIB_XCOUT_FALLBACK) {
									// something in clipboard
									SDL_Event new_event;
									new_event.type           = SDL_KEYDOWN;
									new_event.key.keysym.sym = SDLK_UNKNOWN;
									for (unsigned char const* chr = sel_buf; sel_len-- >= 0; ++chr) {
										new_event.key.keysym.sym = *chr == '\n' ? '\r' : *chr;
										SDL_PushEvent(&new_event);
									}
									free(sel_buf);
								}
							}
#endif
						}
					} else if (0 < sym && sym < 127) {
						code = event.key.keysym.sym; // try with the ASCII code
					} else {
						code = 0;
					}
			}
			sys_event.type    = SIM_KEYBOARD;
			sys_event.code    = code;
			sys_event.key_mod = ModifierKeys();
			break;
		}

		case SDL_MOUSEMOTION:
			sys_event.type = SIM_MOUSE_MOVE;
			sys_event.code = SIM_MOUSE_MOVED;
			sys_event.mx   = event.motion.x;
			sys_event.my   = event.motion.y;
			sys_event.mb   = conv_mouse_buttons(event.motion.state);
			sys_event.key_mod = ModifierKeys();
			break;

		case SDL_KEYUP:
			sys_event.type = SIM_KEYBOARD;
			sys_event.code = 0;
			break;

		case SDL_QUIT:
			sys_event.type = SIM_SYSTEM;
			sys_event.code = SIM_SYSTEM_QUIT;
			break;

		default:
			sys_event.type = SIM_IGNORE_EVENT;
			sys_event.code = 0;
			break;
	}
}


void GetEvents(void)
{
	internal_GetEvents(true);
}


void GetEventsNoWait(void)
{
	sys_event.type = SIM_NOEVENT;
	sys_event.code = 0;

	internal_GetEvents(false);
}


void show_pointer(int yesno)
{
	SDL_SetCursor((yesno != 0) ? arrow : blank);
//	SDL_ShowCursor(yesno != 0);
}


void ex_ord_update_mx_my()
{
	SDL_PumpEvents();
}


unsigned long dr_time(void)
{
	return SDL_GetTicks();
}


void dr_sleep(uint32 usec)
{
	SDL_Delay(usec);
}


#ifdef _WIN32
int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main(int argc, char **argv)
#endif
{
#ifdef _WIN32
	int    const argc = __argc;
	char** const argv = __argv;
#endif
	return sysmain(argc, argv);
}
