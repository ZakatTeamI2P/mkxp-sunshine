/*
** eventthread.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "eventthread.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_touch.h>
#include <SDL3/SDL_rect.h>

// #include <al.h>
#include <AL/al.h>
// #include <alc.h>
#include <AL/alc.h>

#include "sharedstate.h"
#include "graphics.h"
#include "settingsmenu.h"
#include "al-util.h"
#include "debugwriter.h"

#include "oneshot.h"

#include <string.h>

#include <map>

#include <iostream>

typedef void (ALC_APIENTRY *LPALCDEVICEPAUSESOFT) (ALCdevice *device);
typedef void (ALC_APIENTRY *LPALCDEVICERESUMESOFT) (ALCdevice *device);

#define AL_DEVICE_PAUSE_FUN \
	AL_FUN(DevicePause, LPALCDEVICEPAUSESOFT) \
	AL_FUN(DeviceResume, LPALCDEVICERESUMESOFT)

struct ALCFunctions
{
#define AL_FUN(name, type) type name;
	AL_DEVICE_PAUSE_FUN
#undef AL_FUN
} static alc;

static void
initALCFunctions(ALCdevice *alcDev)
{
	if (!strstr(alcGetString(alcDev, ALC_EXTENSIONS), "ALC_SOFT_pause_device"))
		return;

	Debug() << "ALC_SOFT_pause_device present";

#define AL_FUN(name, type) alc. name = (type) alcGetProcAddress(alcDev, "alc" #name "SOFT");
	AL_DEVICE_PAUSE_FUN;
#undef AL_FUN
}

#define HAVE_ALC_DEVICE_PAUSE alc.DevicePause

uint8_t EventThread::keyStates[];
EventThread::ControllerState EventThread::gcState;
EventThread::JoyState EventThread::joyState;
EventThread::MouseState EventThread::mouseState;
EventThread::TouchState EventThread::touchState;

/* User event codes */
enum
{
	REQUEST_SETFULLSCREEN = 0,
	REQUEST_WINRESIZE,
	REQUEST_MESSAGEBOX,
	REQUEST_SETCURSORVISIBLE,

	UPDATE_FPS,
	UPDATE_SCREEN_RECT,

	EVENT_COUNT
};

static uint32_t usrIdStart;

bool EventThread::allocUserEvents()
{
	usrIdStart = SDL_RegisterEvents(EVENT_COUNT);
	// SDL_RegisterEvents() now returns 0 if it couldn't allocate any user events.
	if (usrIdStart == (uint32_t) 0)
		return false;

	return true;
}

EventThread::EventThread()
    : fullscreen(false),
      showCursor(true)
{}

void EventThread::process(RGSSThreadData &rtData)
{
	SDL_Event event;
	SDL_Window *win = rtData.window;
	UnidirMessage<Vec2i> &windowSizeMsg = rtData.windowSizeMsg;

	initALCFunctions(rtData.alcDev);

	// XXX this function breaks input focus on OSX
	#ifndef __APPLE__
		SDL_SetEventFilter(eventFilter, (void*)&rtData);
	#endif

	fullscreen = rtData.config.fullscreen;

	fps.lastFrame = SDL_GetPerformanceCounter();
	fps.displayCounter = 0;
	fps.acc = 0;
	fps.accDiv = 0;

	if (rtData.config.printFPS)
		fps.sendUpdates.set();

	bool displayingFPS = false;

	bool cursorInWindow = false;
	/* Will be updated eventually */
	SDL_Rect gameScreen = { 0, 0, 0, 0 };

	/* SDL doesn't send an initial FOCUS_GAINED event */
	bool windowFocused = true;

	bool terminate = false;

	std::map<int, SDL_Gamepad*> controllers;
	std::map<int, SDL_Joystick*> joysticks;

	for (int i = 0; i < SDL_GetJoysticks(NULL); ++i) {
		if (SDL_IsGamepad(i)) {
			//Load as game controller
			SDL_Gamepad *gc = SDL_OpenGamepad(i);
			int id = SDL_GetJoystickID(SDL_GetGamepadJoystick(gc));
			controllers[id] = gc;
		} else {
			//Fall back to joystick
			SDL_Joystick *js = SDL_OpenJoystick(i);
			joysticks[SDL_GetJoystickID(js)] = js;
		}
	}

	char buffer[128];

	char pendingTitle[128];

	bool resetting = false;

	int winW, winH;
	int i;

	SDL_Joystick *js;
	SDL_Gamepad *gc;
	int id;
	std::map<int, SDL_Joystick*>::iterator jsit;
	std::map<int, SDL_Gamepad*>::iterator gcit;

	SDL_GetWindowSize(win, &winW, &winH); // SDL_GL_GetDrawableSize(win, &winW, &winH);

	SettingsMenu *sMenu = 0;

	while (true)
	{
		if (!SDL_WaitEvent(&event))
		{
			Debug() << "EventThread: Event error";
			break;
		}

		if (sMenu && sMenu->onEvent(event, joysticks))
		{
			if (sMenu->destroyReq())
			{
				delete sMenu;
				sMenu = 0;

				updateCursorState(cursorInWindow && windowFocused, gameScreen);
			}

			continue;
		}

		/* Preselect and discard unwanted events here */
		switch (event.type)
		{
		case SDL_EVENT_MOUSE_BUTTON_DOWN :
		case SDL_EVENT_MOUSE_BUTTON_UP :
		case SDL_EVENT_MOUSE_MOTION :
			if (event.button.which == SDL_TOUCH_MOUSEID)
				continue;
			break;

		case SDL_EVENT_FINGER_DOWN :
		case SDL_EVENT_FINGER_UP :
		case SDL_EVENT_FINGER_MOTION :
			if (event.tfinger.fingerID >= MAX_FINGERS)
				continue;
			break;
		}

		/* Now process the rest */
		switch (event.type)
		{
		case SDL_WINDOWEVENT :
			switch (event.window.event)
			{
			//SDL_WINDOWEVENT_SIZE_CHANGED - handle the SDL_EVENT_WINDOW_RESIZED and SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED events instead
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED :
				winW = event.window.data1;
				winH = event.window.data2;

				//SDL_GL_GetDrawableSize(win, &winW, &winH);

				windowSizeMsg.post(Vec2i(winW, winH));
				resetInputStates();
				break;

			case SDL_EVENT_WINDOW_MOUSE_ENTER :
				cursorInWindow = true;
				mouseState.inWindow = true;
				updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);

				break;

			case SDL_EVENT_WINDOW_MOUSE_LEAVE :
				cursorInWindow = false;
				mouseState.inWindow = false;
				updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);

				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED :
				if (rtData.allowExit) {
					terminate = true;
				} else {
					rtData.triedExit.set();
				}

				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED :
				windowFocused = true;
				updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);

				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST :
				windowFocused = false;
				updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);
				resetInputStates();

				break;
			}

			#ifdef __APPLE__
				case SDL_EVENT_WINDOW_MOVED:
					if (shState != NULL && event.window.data1 && event.window.data2)
						shState->oneshot().setWindowPos(event.window.data1, event.window.data2);
					break;
			#endif

			break;

		case SDL_EVENT_QUIT :
			if (rtData.allowExit) {
				terminate = true;
				Debug() << "EventThread termination requested";
			} else {
				rtData.triedExit.set();
			}

			break;

		case SDL_EVENT_TEXT_INPUT:
			if (rtData.inputText.length() < (size_t)(rtData.inputTextLimit)) rtData.inputText += event.text.text;
			break;

		case SDL_EVENT_KEY_DOWN :
			if (event.key.keysym.scancode == SDL_SCANCODE_F1)
			{
				if (!sMenu)
				{
					sMenu = new SettingsMenu(rtData);
					updateCursorState(false, gameScreen);
				}

				sMenu->raise();
			}

			if (event.key.keysym.scancode == SDL_SCANCODE_F2)
			{
				if (!displayingFPS)
				{
					fps.immInitFlag.set();
					fps.sendUpdates.set();
					displayingFPS = true;
				}
				else
				{
					displayingFPS = false;

					if (!rtData.config.printFPS)
						fps.sendUpdates.clear();

					if (fullscreen)
					{
						/* Prevent fullscreen flicker */
						strncpy(pendingTitle, rtData.config.windowTitle.c_str(),
						        sizeof(pendingTitle));
						break;
					}

					SDL_SetWindowTitle(win, rtData.config.windowTitle.c_str());
				}

				break;
			}

			if (event.key.keysym.scancode == SDL_SCANCODE_F12)
			{
				if (!rtData.config.debugMode)
					break;

				if (resetting)
					break;

				resetting = true;
				rtData.rqResetFinish.clear();
				rtData.rqReset.set();
				break;
			}

			if (rtData.acceptingTextInput) {
				if (event.key.keysym.sym == SDLK_BACKSPACE && rtData.inputText.length() > 0)
					rtData.inputText.pop_back();
				else if (event.key.keysym.sym == SDLK_RETURN)
					rtData.acceptingTextInput.clear();

				break;
			}

			keyStates[event.key.keysym.scancode] = true;
			break;

		case SDL_EVENT_KEY_UP :
			if (event.key.keysym.scancode == SDL_SCANCODE_F12)
			{
				if (!rtData.config.debugMode)
					break;

				resetting = false;
				rtData.rqResetFinish.set();
				break;
			}

			keyStates[event.key.keysym.scancode] = false;
			break;

		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			gcState.buttons[event.gbutton.button] = true;
			break;

		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			gcState.buttons[event.gbutton.button] = false;
			break;

		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			gcState.axes[event.caxis.axis] = event.caxis.value;
			break;

		case SDL_EVENT_GAMEPAD_ADDED:
			gc = SDL_OpenGamepad(event.jdevice.which);
			id = SDL_GetJoystickID(SDL_GetGamepadJoystick(gc));
			controllers[id] = gc;
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
			gcit = controllers.find(event.jdevice.which);
			SDL_CloseGamepad(gcit->second);
			controllers.erase(gcit);
			break;

		case SDL_EVENT_JOYSTICK_BUTTON_DOWN :
			if (joysticks.find(event.jbutton.which) != joysticks.end())
				joyState.buttons[event.jbutton.button] = true;
			break;

		case SDL_EVENT_JOYSTICK_BUTTON_UP :
			if (joysticks.find(event.jbutton.which) != joysticks.end())
				joyState.buttons[event.jbutton.button] = false;
			break;

		case SDL_EVENT_JOYSTICK_HAT_MOTION :
			if (joysticks.find(event.jbutton.which) != joysticks.end())
				joyState.hats[event.jhat.hat] = event.jhat.value;
			break;

		case SDL_EVENT_JOYSTICK_AXIS_MOTION :
			if (joysticks.find(event.jbutton.which) != joysticks.end())
				joyState.axes[event.jaxis.axis] = event.jaxis.value;
			break;

		case SDL_EVENT_JOYSTICK_ADDED :
			if (SDL_IsGamepad(event.jdevice.which))
				break;
			js = SDL_OpenJoystick(event.jdevice.which);
			joysticks[SDL_GetJoystickID(js)] = js;
			break;

		case SDL_EVENT_JOYSTICK_REMOVED :
			jsit = joysticks.find(event.jdevice.which);
			if (jsit != joysticks.end()) {
				SDL_CloseJoystick(jsit->second);
				joysticks.erase(jsit);
				resetInputStates();
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN :
			mouseState.buttons[event.button.button] = true;
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP :
			mouseState.buttons[event.button.button] = false;
			break;

		case SDL_EVENT_MOUSE_MOTION :
			mouseState.x = event.motion.x;
			mouseState.y = event.motion.y;
			updateCursorState(cursorInWindow, gameScreen);
			break;

		case SDL_EVENT_FINGER_DOWN :
			i = event.tfinger.fingerID;
			touchState.fingers[i].down = true;
			/* falls through */

		case SDL_EVENT_FINGER_MOTION :
			i = event.tfinger.fingerID;
			touchState.fingers[i].x = event.tfinger.x * winW;
			touchState.fingers[i].y = event.tfinger.y * winH;
			break;

		case SDL_EVENT_FINGER_UP :
			i = event.tfinger.fingerID;
			memset(&touchState.fingers[i], 0, sizeof(touchState.fingers[0]));
			break;

		default :
			/* Handle user events */
			switch(event.type - usrIdStart)
			{
			case REQUEST_SETFULLSCREEN :
				setFullscreen(win, static_cast<bool>(event.user.code));
				break;

			case REQUEST_WINRESIZE :
				SDL_SetWindowSize(win, event.window.data1, event.window.data2);
				break;

			case REQUEST_MESSAGEBOX :
				SDL_ShowSimpleMessageBox(event.user.code,
				                         rtData.config.windowTitle.c_str(),
				                         (const char*) event.user.data1, win);
				free(event.user.data1);
				msgBoxDone.set();
				break;

			case REQUEST_SETCURSORVISIBLE :
				showCursor = event.user.code;
				updateCursorState(cursorInWindow, gameScreen);
				break;

			case UPDATE_FPS :
				if (rtData.config.printFPS)
					Debug() << "FPS:" << event.user.code;

				if (!fps.sendUpdates)
					break;

				snprintf(buffer, sizeof(buffer), "%s - %d FPS",
				         rtData.config.windowTitle.c_str(), event.user.code);

				/* Updating the window title in fullscreen
				 * mode seems to cause flickering */
				if (fullscreen)
				{
					strncpy(pendingTitle, buffer, sizeof(pendingTitle));
					break;
				}

				SDL_SetWindowTitle(win, buffer);
				break;

			case UPDATE_SCREEN_RECT :
				gameScreen.x = event.user.windowID;
				gameScreen.y = event.user.code;
				gameScreen.w = reinterpret_cast<intptr_t>(event.user.data1);
				gameScreen.h = reinterpret_cast<intptr_t>(event.user.data2);
				updateCursorState(cursorInWindow, gameScreen);

				break;
			}
		}

		if (terminate)
			break;
	}

	/* Just in case */
	rtData.syncPoint.resumeThreads();

	for (gcit = controllers.begin(); gcit != controllers.end(); ++gcit)
		SDL_CloseGamepad(gcit->second);
	for (jsit = joysticks.begin(); jsit != joysticks.end(); ++jsit)
		SDL_CloseJoystick(jsit->second);

	delete sMenu;
}

bool EventThread::eventFilter(void *data, SDL_Event *event)
{
	RGSSThreadData &rtData = *static_cast<RGSSThreadData*>(data);

	switch (event->type)
	{
	case SDL_EVENT_WILL_ENTER_BACKGROUND :
		Debug() << "SDL_EVENT_WILL_ENTER_BACKGROUND";

		if (HAVE_ALC_DEVICE_PAUSE)
			alc.DevicePause(rtData.alcDev);

		rtData.syncPoint.haltThreads();

		return 0;

	case SDL_EVENT_DID_ENTER_BACKGROUND :
		Debug() << "SDL_EVENT_DID_ENTER_BACKGROUND";
		return 0;

	case SDL_EVENT_WILL_ENTER_FOREGROUND :
		Debug() << "SDL_EVENT_WILL_ENTER_FOREGROUND";
		return 0;

	case SDL_EVENT_DID_ENTER_FOREGROUND :
		Debug() << "SDL_EVENT_DID_ENTER_FOREGROUND";

		if (HAVE_ALC_DEVICE_PAUSE)
			alc.DeviceResume(rtData.alcDev);

		rtData.syncPoint.resumeThreads();

		return 0;

	case SDL_EVENT_TERMINATING :
		Debug() << "SDL_EVENT_TERMINATING";
		return 0;

	case SDL_EVENT_LOW_MEMORY :
		Debug() << "SDL_EVENT_LOW_MEMORY";
		return 0;

	/* Workaround for Windows pausing on drag */
	case SDL_WINDOWEVENT:
		if (event->window.event == SDL_EVENT_WINDOW_MOVED)
		{
			if (shState != NULL && shState->rgssVersion > 0)
			{
				shState->oneshot().setWindowPos(event->window.data1, event->window.data2);
				shState->graphics().update(false);
			}
			return 0;
		}
		return 1;
	}

	return 1;
}

void EventThread::cleanup(){
	SDL_Event event;

	while (SDL_PollEvent(&event))
		if ((event.type - usrIdStart) == REQUEST_MESSAGEBOX)
			free(event.user.data1);
}

void EventThread::resetInputStates(){
	memset(&keyStates, 0, sizeof(keyStates));
	memset(&gcState, 0, sizeof(gcState));
	memset(&joyState, 0, sizeof(joyState));
	memset(&mouseState.buttons, 0, sizeof(mouseState.buttons));
	memset(&touchState, 0, sizeof(touchState));
}

void EventThread::setFullscreen(SDL_Window *win, bool mode){
	SDL_SetWindowFullscreen(win, mode);
	fullscreen = mode;
}

void EventThread::updateCursorState(bool inWindow, const SDL_Rect &screen){
	SDL_Point pos = { mouseState.x, mouseState.y };
	bool inScreen = inWindow && SDL_PointInRect(&pos, &screen);

	if (inScreen)
		SDL_ShowCursor();
	else
		SDL_HideCursor();
}

void EventThread::requestTerminate()
{
	SDL_Event event;
	event.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&event);
}

void EventThread::requestFullscreenMode(bool mode)
{
	if (mode == fullscreen)
		return;

	SDL_Event event;
	event.type = usrIdStart + REQUEST_SETFULLSCREEN;
	event.user.code = static_cast<Sint32>(mode);
	SDL_PushEvent(&event);
}

void EventThread::requestWindowResize(int width, int height)
{
	SDL_Event event;
	event.type = usrIdStart + REQUEST_WINRESIZE;
	event.window.data1 = width;
	event.window.data2 = height;
	SDL_PushEvent(&event);
}

void EventThread::requestShowCursor(bool mode)
{
	SDL_Event event;
	event.type = usrIdStart + REQUEST_SETCURSORVISIBLE;
	event.user.code = mode;
	SDL_PushEvent(&event);
}

void EventThread::showMessageBox(const char *body, int flags)
{
	msgBoxDone.clear();

	SDL_Event event;
	event.user.code = flags;
	event.user.data1 = strdup(body);
	event.type = usrIdStart + REQUEST_MESSAGEBOX;
	SDL_PushEvent(&event);

	/* Keep repainting screen while box is open */
	shState->graphics().repaintWait(msgBoxDone);
	/* Prevent endless loops */
	resetInputStates();
}

bool EventThread::getFullscreen() const
{
	return fullscreen;
}

bool EventThread::getShowCursor() const
{
	return showCursor;
}

void EventThread::notifyFrame()
{
	if (!fps.sendUpdates)
		return;

	uint64_t current = SDL_GetPerformanceCounter();
	uint64_t diff = current - fps.lastFrame;
	fps.lastFrame = current;

	if (fps.immInitFlag)
	{
		fps.immInitFlag.clear();
		fps.immFiniFlag.set();

		return;
	}

	static uint64_t freq = SDL_GetPerformanceFrequency();

	double currFPS = (double) freq / diff;
	fps.acc += currFPS;
	++fps.accDiv;

	fps.displayCounter += diff;
	if (fps.displayCounter < freq && !fps.immFiniFlag)
		return;

	fps.displayCounter = 0;
	fps.immFiniFlag.clear();

	int32_t avgFPS = fps.acc / fps.accDiv;
	fps.acc = fps.accDiv = 0;

	SDL_Event event;
	event.user.code = avgFPS;
	event.user.type = usrIdStart + UPDATE_FPS;
	SDL_PushEvent(&event);
}

void EventThread::notifyGameScreenChange(const SDL_Rect &screen)
{
	/* We have to get a bit hacky here to fit the rectangle
	 * data into the user event struct */
	SDL_Event event;
	event.type = usrIdStart + UPDATE_SCREEN_RECT;
	event.user.windowID = screen.x;
	event.user.code = screen.y;
	event.user.data1 = reinterpret_cast<void*>(screen.w);
	event.user.data2 = reinterpret_cast<void*>(screen.h);
	SDL_PushEvent(&event);
}

void SyncPoint::haltThreads()
{
	if (mainSync.locked)
		return;

	/* Lock the reply sync first to avoid races */
	reply.lock();

	/* Lock main sync and sleep until RGSS thread
	 * reports back */
	mainSync.lock();
	reply.waitForUnlock();

	/* Now that the RGSS thread is asleep, we can
	 * safely put the other threads to sleep as well
	 * without causing deadlocks */
	secondSync.lock();
}

void SyncPoint::resumeThreads()
{
	if (!mainSync.locked)
		return;

	mainSync.unlock(false);
	secondSync.unlock(true);
}

bool SyncPoint::mainSyncLocked()
{
	return mainSync.locked;
}

void SyncPoint::waitMainSync()
{
	reply.unlock(false);
	mainSync.waitForUnlock();
}

void SyncPoint::passSecondarySync()
{
	if (!secondSync.locked)
		return;

	secondSync.waitForUnlock();
}

SyncPoint::Util::Util()
{
	mut = SDL_CreateMutex();
	cond = SDL_CreateCondition();
}

SyncPoint::Util::~Util()
{
	SDL_DestroyCondition(cond);
	SDL_DestroyMutex(mut);
}

void SyncPoint::Util::lock()
{
	locked.set();
}

void SyncPoint::Util::unlock(bool multi)
{
	locked.clear();

	if (multi)
		SDL_BroadcastCondition(cond);
	else
		SDL_SignalCondition(cond);
}

void SyncPoint::Util::waitForUnlock()
{
	SDL_LockMutex(mut);

	while (locked)
		SDL_WaitCondition(cond, mut);

	SDL_UnlockMutex(mut);
}
