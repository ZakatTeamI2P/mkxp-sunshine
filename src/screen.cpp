#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>

#define FPS 60
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240

#include "config.h"
#include "debugwriter.h"
#include "pipe.h"

// #include "oldimpls/include.h"

static void showInitError(const std::string &msg)
{
	Debug() << msg;
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OneShot: sunshine", msg.c_str(), 0);
}

static bool readMessage(Pipe &ipc, char *buf, size_t size)
{
	size_t index = 0;
	while (index < size - 1) {
		if (ipc.read(buf + index)) {
			++index;
		} else {
			break;
		}
	}
	buf[index] = 0;

	return index > 0;
}

int screenMain(Config &conf)
{
	const SDL_Color colorKey = {0x00, 0xFF, 0x00, 0xFF};
	const SDL_Color black = {0x00, 0x00, 0x00, 0xFF};

	Pipe ipc("oneshot-pipe", Pipe::Read);

	//int imgFlags = IMG_INIT_PNG;
	//if (IMG_Init(imgFlags) != imgFlags)
	//{
	//	showInitError(std::string("Error initializing SDL_image: ") + SDL_GetError());
	//	SDL_Quit();
	//
	//	return 0;
	//}

	SDL_Window *win;
	win = SDL_CreateWindow("The Journal", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT);
	//SDL_SetWindowShape(win, );

	if (!win)
	{
		showInitError(std::string("Error creating window: ") + SDL_GetError());
		return 0;
	}

	// SDL_WindowShapeMode shapeMode;
	// shapeMode.mode = ShapeModeColorKey;
	// shapeMode.parameters.colorKey = colorKey;

	//SDL_Surface *surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_INDEX8);
	//SDL_PIXELFORMAT_UNKNOWN - 0,0,0,0 ....maybe?

	//SDL_Surface *surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_INDEX8);
	//SDL_Surface *shape = SDL_CreateSurface(DEFAULT_WIDTH, DEFAULT_HEIGHT, SDL_PIXELFORMAT_UNKNOWN);

	// i have no idea if it works.
	//SDL_Surface *shape = SDL_CreateRGBSurface(0, DEFAULT_WIDTH, DEFAULT_HEIGHT, 8, 0, 0, 0, 0);
	SDL_Surface *shape = SDL_CreateSurface(DEFAULT_WIDTH, DEFAULT_HEIGHT, SDL_PixelFormat::SDL_PIXELFORMAT_RGBA32);
	SDL_Palette *palette = SDL_CreateSurfacePalette(shape);
	SDL_SetPaletteColors(palette, &black, 0, 1);

	char messageBuf[256];

	unsigned int ticks = SDL_GetTicks();
	for (;;) {
		// Handle events
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_EVENT_QUIT:
				return 0;
			}
		}

		// Change shape
		if (readMessage(ipc, messageBuf, sizeof(messageBuf))) {
			if (strcmp(messageBuf, "END") == 0)
				break;
			std::string imgname = conf.gameFolder + "/Journal/" + messageBuf + ".png";
			SDL_DestroySurface(shape);
			if ((shape = IMG_Load(imgname.c_str())) == NULL)
				break;
			SDL_SetWindowSize(win, shape->w, shape->h);
			// SDL_SetWindowShape(win, shape, &shapeMode);
			SDL_SetWindowShape(win, shape);
		}

		// Redraw
		SDL_BlitSurface(shape, NULL, SDL_GetWindowSurface(win), NULL);
		SDL_UpdateWindowSurface(win);

		// Regulate framerate
	    unsigned int ticksDelta = SDL_GetTicks() - ticks;
	    if (ticksDelta < 1000 / FPS)
	        SDL_Delay(1000 / FPS - ticksDelta);
	    ticks = SDL_GetTicks();
	}

	SDL_DestroySurface(shape);
	return 0;
}
