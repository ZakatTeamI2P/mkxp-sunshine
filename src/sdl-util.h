#ifndef SDLUTIL_H
#define SDLUTIL_H

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_iostream.h>

#include <string>
#include <iostream>

struct AtomicFlag
{
	AtomicFlag()
	{
		clear();
	}

	AtomicFlag(bool value)
	{
		SDL_SetAtomicInt(&atom, value ? 1 : 0);
	}

	void set()
	{
		SDL_SetAtomicInt(&atom, 1);
	}

	void clear()
	{
		SDL_SetAtomicInt(&atom, 0);
	}

	operator bool() const
	{
		return SDL_GetAtomicInt(&atom);
	}

private:
	mutable SDL_AtomicInt atom;
};

template<class C, void (C::*func)()>
int __sdlThreadFun(void *obj)
{
	(static_cast<C*>(obj)->*func)();
	return 0;
}

template<class C, void (C::*func)()>
SDL_Thread *createSDLThread(C *obj, const std::string &name = std::string())
{
	return SDL_CreateThread((__sdlThreadFun<C, func>), name.c_str(), obj);
}

/* On Android, SDL_RWFromFile always opens files from inside
 * the apk asset folder even when a file with same name exists
 * on the physical filesystem. This wrapper attempts to open a
 * real file first before falling back to the assets folder */
//static inline
//SDL_RWops *RWFromFile(const char *filename,
//                      const char *mode)
//{
//	FILE *f = fopen(filename, mode);
//
//	if (!f)
//		return SDL_RWFromFile(filename, mode);
//
//	return SDL_RWFromFP(f, SDL_TRUE);
//}

inline bool readFileSDL(const char *path, std::string &out){
	SDL_IOStream *f = SDL_IOFromFile(path, "rb");

	if (!f)
		return false;

	long size = SDL_GetIOSize(f);
	size_t back = out.size();

	out.resize(back+size);
	size_t read = SDL_ReadIO(f, &out[back], size);
	SDL_CloseIO(f);

	if (read != (size_t) size)
		out.resize(back+read);

	return true;
}

template<size_t bufSize = 248, size_t pbSize = 8>
class SDLRWBuf : public std::streambuf
{
public:
	SDLRWBuf(SDL_IOStream *ops)
	    : ops(ops)
	{
		char *end = buf + bufSize + pbSize;
		setg(end, end, end);
	}

private:
	int_type underflow()
	{
		if (!ops)
			return traits_type::eof();

		if (gptr() < egptr())
			return traits_type::to_int_type(*gptr());

		char *base = buf;
		char *start = base;

		if (eback() == base)
		{
			memmove(base, egptr() - pbSize, pbSize);
			start += pbSize;
		}
		//size_t SDL_ReadIO(SDL_IOStream *context, void *ptr, size_t size);
		size_t n = SDL_ReadIO(ops, start, bufSize - (start - base));
		if (n == 0)
			return traits_type::eof();

		setg(base, start, start + n);

		return underflow();
	}

	SDL_IOStream *ops;
	char buf[bufSize+pbSize];
};

class SDLRWStream
{
public:
	SDLRWStream(const char *filename,
	            const char *mode)
	    : ops(SDL_IOFromFile(filename, mode)),
	      buf(ops),
	      s(&buf)
	{}

	~SDLRWStream()
	{
		if (ops)
			SDL_CloseIO(ops);
	}

	operator bool() const
	{
		return ops != 0;
	}

	std::istream &stream()
	{
		return s;
	}

private:
	SDL_IOStream *ops;
	SDLRWBuf<> buf;
	std::istream s;
};

#endif // SDLUTIL_H
