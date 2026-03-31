/*
** filesystem.cpp
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

#include "filesystem.h"

#include "rgssad.h"
#include "font.h"
#include "util.h"
#include "exception.h"
#include "sharedstate.h"
#include "boost-hash.h"
#include "debugwriter.h"

#include <physfs.h>

#include <SDL3_sound/SDL_sound.h>

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include <stack>

#ifdef __APPLE__
	#define OS_OSX
	#include <iconv.h>
#endif

struct SDLRWIoContext
{
	SDL_IOStream *ops;
	std::string filename;

	SDLRWIoContext(const char *filename)
	    : ops(SDL_IOFromFile(filename, "r")),
	      filename(filename)
	{
		if (!ops)
			throw Exception(Exception::SDLError,
			                "Failed to open file: %s", SDL_GetError());
	}

	~SDLRWIoContext()
	{
		SDL_CloseIO(ops);
	}
};

static PHYSFS_Io *createSDLRWIo(const char *filename);

static SDL_IOStream *getSDLRWops(PHYSFS_Io *io)
{
	return static_cast<SDLRWIoContext*>(io->opaque)->ops;
}

static PHYSFS_sint64 SDLRWIoRead(struct PHYSFS_Io *io, void *buf, PHYSFS_uint64 len)
{
	// return SDL_ReadIO(getSDLRWops(io), buf, 1, len);
	return SDL_ReadIO(getSDLRWops(io), buf, len);
}

static int SDLRWIoSeek(struct PHYSFS_Io *io, PHYSFS_uint64 offset)
{
	return (SDL_SeekIO(getSDLRWops(io), offset, SDL_IOWhence::SDL_IO_SEEK_SET) != -1);
}

static PHYSFS_sint64 SDLRWIoTell(struct PHYSFS_Io *io)
{
	return SDL_SeekIO(getSDLRWops(io), 0, SDL_IOWhence::SDL_IO_SEEK_CUR);
}

static PHYSFS_sint64 SDLRWIoLength(struct PHYSFS_Io *io)
{
	return SDL_GetIOSize(getSDLRWops(io));
}

static struct PHYSFS_Io *SDLRWIoDuplicate(struct PHYSFS_Io *io)
{
	SDLRWIoContext *ctx = static_cast<SDLRWIoContext*>(io->opaque);
	int64_t offset = io->tell(io);
	PHYSFS_Io *dup = createSDLRWIo(ctx->filename.c_str());

	if (dup)
		SDLRWIoSeek(dup, offset);

	return dup;
}

static void SDLRWIoDestroy(struct PHYSFS_Io *io)
{
	delete static_cast<SDLRWIoContext*>(io->opaque);
	delete io;
}

static PHYSFS_Io SDLRWIoTemplate =
{
	0, 0, /* version, opaque */
	SDLRWIoRead,
	0, /* write */
	SDLRWIoSeek,
	SDLRWIoTell,
	SDLRWIoLength,
	SDLRWIoDuplicate,
	0, /* flush */
	SDLRWIoDestroy
};

static PHYSFS_Io *createSDLRWIo(const char *filename)
{
	SDLRWIoContext *ctx;

	try
	{
		ctx = new SDLRWIoContext(filename);
	}
	catch (const Exception &e)
	{
		Debug() << "Failed mounting" << filename;
		return 0;
	}

	PHYSFS_Io *io = new PHYSFS_Io;
	*io = SDLRWIoTemplate;
	io->opaque = ctx;

	return io;
}

static inline PHYSFS_File *sdlPHYS(void *userdata)
{
	return static_cast<PHYSFS_File*>(userdata);
}

static Sint64 SDL_RWopsSize(void *userdata)
{
	PHYSFS_File *f = sdlPHYS(userdata);

	if (!f)
		return -1;

	return PHYSFS_fileLength(f);
}

static Sint64 SDL_RWopsSeek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
	PHYSFS_File *f = sdlPHYS(userdata);

	if (!f)
		return -1;

	int64_t base;

	switch (whence)
	{
	default:
	case SDL_IOWhence::SDL_IO_SEEK_SET:
		base = 0;
		break;
	case SDL_IOWhence::SDL_IO_SEEK_CUR:
		base = PHYSFS_tell(f);
		break;
	case SDL_IOWhence::SDL_IO_SEEK_END:
		base = PHYSFS_fileLength(f);
		break;
	}

	int result = PHYSFS_seek(f, base + offset);

	return (result != 0) ? PHYSFS_tell(f) : -1;
}

static size_t SDL_RWopsRead(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
	PHYSFS_File *f = sdlPHYS(userdata);

	if (!f)
		return 0;

	PHYSFS_sint64 result = PHYSFS_readBytes(f, ptr, size);

	if (result == 0)
		*status = SDL_IOStatus::SDL_IO_STATUS_EOF;
	else if (result == -1)
		*status = SDL_IOStatus::SDL_IO_STATUS_ERROR;

	return (result != -1) ? (result) : 0;
}

static size_t SDL_RWopsWrite(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status)
{
	PHYSFS_File *f = sdlPHYS(userdata);

	if (!f)
		return 0;

	PHYSFS_sint64 result = PHYSFS_writeBytes(f, ptr, size);

	if (result == -1)
		*status = SDL_IOStatus::SDL_IO_STATUS_ERROR;

	return (result != -1) ? (result) : 0;
}

static bool SDL_RWopsClose(void *userdata)
{
	PHYSFS_File *f = sdlPHYS(userdata);

	if (!f)
		return -1;

	int result = PHYSFS_close(f);
	
	// using userdata now.
	//ops->hidden.unknown.data1 = 0;

	return (result == 0);
}

// dummy function because i have NO IDEA if this function optional.
static bool SDL_RWopsFlush(void *userdata, SDL_IOStatus *status) { return true; }

/*
static int SDL_RWopsCloseFree(void *userdata)
{
	int result = SDL_RWopsClose(userdata);

	// SDL_CloseIO(ops);

	return result;
}*/

/* Copies the first srcN characters from src into dst,
 * or the full string if srcN == -1. Never writes more
 * than dstMax, and guarantees dst to be null terminated.
 * Returns copied bytes (minus terminating null) */
static size_t
strcpySafe(char *dst, const char *src,
           size_t dstMax, int srcN)
{
	if (srcN < 0)
		srcN = strlen(src);

	size_t cpyMax = std::min<size_t>(dstMax-1, srcN);

	memcpy(dst, src, cpyMax);
	dst[cpyMax] = '\0';

	return cpyMax;
}

/* Attempt to locate an extension string in a filename.
 * Either a pointer into the input string pointing at the
 * extension, or null is returned */
static const char *
findExt(const char *filename)
{
	size_t len;

	for (len = strlen(filename); len > 0; --len)
	{
		if (filename[len] == '/')
			return 0;

		if (filename[len] == '.')
			return &filename[len+1];
	}

	return 0;
}

static void
initReadOps(PHYSFS_File *handle,
            SDL_IOStream* &ops)
            //bool freeOnClose)
{
	SDL_IOStreamInterface iface;
	SDL_INIT_INTERFACE(&iface);

	iface.size  = SDL_RWopsSize;
	iface.seek  = SDL_RWopsSeek;
	iface.read  = SDL_RWopsRead;
	iface.write = SDL_RWopsWrite;

	/* useless now :P
	if (freeOnClose)
		ops.close = SDL_RWopsCloseFree;
	else */
	
	iface.flush = SDL_RWopsFlush;
	iface.close = SDL_RWopsClose;

	ops = SDL_OpenIO(&iface, handle);
}

static void strTolower(std::string &str)
{
	for (size_t i = 0; i < str.size(); ++i)
		str[i] = tolower(str[i]);
}

// const Uint32 SDL_RWOPS_PHYSFS = SDL_RWOPS_UNKNOWN+10;

struct FileSystemPrivate
{
	/* Maps: lower case full filepath,
	 * To:   mixed case full filepath */
	BoostHash<std::string, std::string> pathCache;
	/* Maps: lower case directory path,
	 * To:   list of lower case filenames */
	BoostHash<std::string, std::vector<std::string> > fileLists;

	/* This is for compatibility with games that take Windows'
	 * case insensitivity for granted */
	bool havePathCache;
};

FileSystem::FileSystem(bool allowSymlinks)
{
	p = new FileSystemPrivate;
	p->havePathCache = false;

	PHYSFS_registerArchiver(&RGSS1_Archiver);
	PHYSFS_registerArchiver(&RGSS2_Archiver);
	PHYSFS_registerArchiver(&RGSS3_Archiver);

	if (allowSymlinks)
		PHYSFS_permitSymbolicLinks(1);
}

FileSystem::~FileSystem()
{
	delete p;

	if (PHYSFS_deinit() == 0)
		Debug() << "PhyFS failed to deinit.";
}

void FileSystem::addPath(const char *path)
{
	/* Try the normal mount first */
	if (!PHYSFS_mount(path, 0, 1))
	{
		/* If it didn't work, try mounting via a wrapped
		 * SDL_IOStream */
		PHYSFS_Io *io = createSDLRWIo(path);

		if (io)
			PHYSFS_mountIo(io, path, 0, 1);
	}
}

struct CacheEnumData
{
	FileSystemPrivate *p;
	std::stack<std::vector<std::string>*> fileLists;

#ifdef OS_OSX
	iconv_t nfd2nfc;
	char buf[512];
#endif

	CacheEnumData(FileSystemPrivate *p)
	    : p(p)
	{
#ifdef OS_OSX
		nfd2nfc = iconv_open("utf-8", "utf-8-mac");
#endif
	}

	~CacheEnumData()
	{
#ifdef OS_OSX
		iconv_close(nfd2nfc);
#endif
	}

	/* Converts in-place */
	void toNFC(char *inout)
	{
#ifdef OS_OSX
		size_t srcSize = strlen(inout);
		size_t bufSize = sizeof(buf);
		char *bufPtr = buf;
		char *inoutPtr = inout;

		/* Reserve room for null terminator */
		--bufSize;

		iconv(nfd2nfc,
			  &inoutPtr, &srcSize,
			  &bufPtr, &bufSize);
		/* Null-terminate */
		*bufPtr = 0;
		strcpy(inout, buf);
#else
		(void) inout;
#endif
	}
};

static PHYSFS_EnumerateCallbackResult
cacheEnumCB(void *d, const char *origdir, const char *fname)
{
	CacheEnumData &data = *static_cast<CacheEnumData*>(d);
	char fullPath[512];

	if (!*origdir)
		snprintf(fullPath, sizeof(fullPath), "%s", fname);
	else
		snprintf(fullPath, sizeof(fullPath), "%s/%s", origdir, fname);

	/* Deal with OSX' weird UTF-8 standards */
	data.toNFC(fullPath);

	std::string mixedCase(fullPath);
	std::string lowerCase = mixedCase;
	strTolower(lowerCase);

	PHYSFS_Stat stat;
	PHYSFS_stat(fullPath, &stat);

	if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY)
	{
		/* Create a new list for this directory */
		std::vector<std::string> &list = data.p->fileLists[lowerCase];

		/* Iterate over its contents */
		data.fileLists.push(&list);
		PHYSFS_enumerate(fullPath, cacheEnumCB, d);
		data.fileLists.pop();
	}
	else
	{
		/* Get the file list for the directory we're currently
		 * traversing and append this filename to it */
		std::vector<std::string> &list = *data.fileLists.top();
		std::string lowerFilename(fname);
		strTolower(lowerFilename);
		list.push_back(lowerFilename);

		/* Add the lower -> mixed mapping of the file's full path */
		data.p->pathCache.insert(lowerCase, mixedCase);
	}

	return PHYSFS_ENUM_OK;
}

void FileSystem::createPathCache()
{
	CacheEnumData data(p);
	data.fileLists.push(&p->fileLists[""]);
	PHYSFS_enumerate("", cacheEnumCB, &data);

	p->havePathCache = true;
}

struct FontSetsCBData
{
	FileSystemPrivate *p;
	SharedFontState *sfs;
};

static PHYSFS_EnumerateCallbackResult
fontSetEnumCB (void *data, const char *dir, const char *fname)
{
	FontSetsCBData *d = static_cast<FontSetsCBData*>(data);

	/* Only consider filenames with font extensions */
	const char *ext = findExt(fname);

	if (!ext)
		return PHYSFS_ENUM_OK;

	char lowExt[8];
	size_t i;

	for (i = 0; i < sizeof(lowExt)-1 && ext[i]; ++i)
		lowExt[i] = tolower(ext[i]);
	lowExt[i] = '\0';

	if (strcmp(lowExt, "ttf") && strcmp(lowExt, "otf") && strcmp(lowExt, "ttc"))
		return PHYSFS_ENUM_OK;

	char filename[512];
	snprintf(filename, sizeof(filename), "%s/%s", dir, fname);

	PHYSFS_File *handle = PHYSFS_openRead(filename);
	if (!handle)
		return PHYSFS_ENUM_ERROR;

	SDL_IOStream* stream = nullptr;
	initReadOps(handle, stream);

	d->sfs->initFontSetCB(stream, filename);

	SDL_CloseIO(stream);

	return PHYSFS_ENUM_OK;
}

void FileSystem::initFontSets(SharedFontState &sfs)
{
	FontSetsCBData d = { p, &sfs };

	PHYSFS_enumerate("Fonts", fontSetEnumCB, &d);
}

struct OpenReadEnumData
{
	FileSystem::OpenHandler &handler;
	SDL_IOStream* ops;

	/* The filename (without directory) we're looking for */
	const char *filename;
	size_t filenameN;

	/* Optional hash to translate full filepaths
	 * (used with path cache) */
	BoostHash<std::string, std::string> *pathTrans;

	/* Number of files we've attempted to read and parse */
	size_t matchCount;
	bool stopSearching;

	/* In case of a PhysFS error, save it here so it
	 * doesn't get changed before we get back into our code */
	const char *physfsError;

	OpenReadEnumData(FileSystem::OpenHandler &handler,
	                 const char *filename, size_t filenameN,
	                 BoostHash<std::string, std::string> *pathTrans)
	    : handler(handler), filename(filename), filenameN(filenameN),
	      pathTrans(pathTrans), matchCount(0), stopSearching(false),
	      physfsError(0)
	{}
};

static PHYSFS_EnumerateCallbackResult
openReadEnumCB(void *d, const char *dirpath, const char *filename)
{
	OpenReadEnumData &data = *static_cast<OpenReadEnumData*>(d);
	char buffer[512];
	const char *fullPath;

	if (data.stopSearching)
		return PHYSFS_ENUM_STOP;

	/* If there's not even a partial match, continue searching */
	if (strncmp(filename, data.filename, data.filenameN) != 0)
		return PHYSFS_ENUM_OK;

	if (!*dirpath)
	{
		fullPath = filename;
	}
	else
	{
		snprintf(buffer, sizeof(buffer), "%s/%s", dirpath, filename);
		fullPath = buffer;
	}

	char last = filename[data.filenameN];

	/* If fname matches up to a following '.' (meaning the rest is part
	 * of the extension), or up to a following '\0' (full match), we've
	 * found our file */
	if (last != '.' && last != '\0')
		return PHYSFS_ENUM_STOP;

	/* If the path cache is active, translate from lower case
	 * to mixed case path */
	if (data.pathTrans)
		fullPath = (*data.pathTrans)[fullPath].c_str();

	PHYSFS_File *phys = PHYSFS_openRead(fullPath);

	if (!phys)
	{
		/* Failing to open this file here means there must
		 * be a deeper rooted problem somewhere within PhysFS.
		 * Just abort alltogether. */
		data.stopSearching = true;
		data.physfsError = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());

		return PHYSFS_ENUM_ERROR;
	}

	initReadOps(phys, data.ops);

	const char *ext = findExt(filename);

	if (data.handler.tryRead(data.ops, ext))
		data.stopSearching = true;

	++data.matchCount;
	return PHYSFS_ENUM_OK;
}

void FileSystem::openRead(OpenHandler &handler, const char *filename)
{
	char buffer[512];
	size_t len = strcpySafe(buffer, filename, sizeof(buffer), -1);
	char *delim;

	if (p->havePathCache)
		for (size_t i = 0; i < len; ++i)
			buffer[i] = tolower(buffer[i]);

	/* Find the deliminator separating directory and file name */
	for (delim = buffer + len; delim > buffer; --delim)
		if (*delim == '/')
			break;

	const bool root = (delim == buffer);

	const char *file = buffer;
	const char *dir = "";

	if (!root)
	{
		/* Cut the buffer in half so we can use it
		 * for both filename and directory path */
		*delim = '\0';
		file = delim+1;
		dir = buffer;
	}

	OpenReadEnumData data(handler, file, len + buffer - delim - !root,
	                      p->havePathCache ? &p->pathCache : 0);

	if (p->havePathCache)
	{
		/* Get the list of files contained in this directory
		 * and manually iterate over them */
		const std::vector<std::string> &fileList = p->fileLists[dir];

		for (size_t i = 0; i < fileList.size(); ++i)
			openReadEnumCB(&data, dir, fileList[i].c_str());
	}
	else
	{
		PHYSFS_enumerate(dir, openReadEnumCB, &data);
	}

	if (data.physfsError)
		throw Exception(Exception::PHYSFSError, "PhysFS: %s", data.physfsError);

	if (data.matchCount == 0)
		throw Exception(Exception::NoFileError, "%s", filename);
}

void FileSystem::openReadRaw(SDL_IOStream* &stream,
                             const char *filename)
                             //bool freeOnClose)
{
	PHYSFS_File *handle = PHYSFS_openRead(filename);
	if (!handle)
		throw Exception(Exception::NoFileError, "%s", filename);

	initReadOps(handle, stream);
}

bool FileSystem::exists(const char *filename)
{
	return PHYSFS_exists(filename);
}
