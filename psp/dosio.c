#include "compiler.h"
#include <sys/stat.h>
#include <time.h>
#if defined(WIN32) && defined(OSLANG_UTF8)
#include "codecnv/codecnv.h"
#endif
#include "dosio.h"
#if defined(WIN32)
#include <direct.h>
#else
#include <dirent.h>
#endif


/* ---- Disk image RAM cache ----
 * fdd_2d/fdd_d88 open/seek/read/close the image for EVERY sector access;
 * on the memory stick that costs tens of ms per frame during loads.
 * Read-opens of disk images are served from a whole-file RAM copy,
 * invalidated by write opens (file_open/file_create). */

#define	DCACHE_SLOTS	2
#define	DCACHE_MAX		(2 * 1024 * 1024)
#define	MEMFH_MAGIC		0x4d464831

typedef struct {
	UINT32	magic;
	UINT8	*data;
	UINT	size;
	UINT	pos;
} MEMFH;

typedef struct {
	char	path[MAX_PATH];
	UINT8	*data;
	UINT	size;
	UINT32	last;
} DSLOT;

static DSLOT	dslot[DCACHE_SLOTS];
static UINT32	duse;

static int dcache_isimage(const char *path) {

	const char *p = strrchr(path, '.');
	if (p == NULL) {
		return(0);
	}
	p++;
	return((!strcasecmp(p, "2d")) || (!strcasecmp(p, "d88")) ||
			(!strcasecmp(p, "88d")) || (!strcasecmp(p, "2hd")));
}

static void dcache_invalidate(const char *path) {

	int i;
	for (i = 0; i < DCACHE_SLOTS; i++) {
		if (dslot[i].data && (!strcmp(dslot[i].path, path))) {
			free(dslot[i].data);
			dslot[i].data = NULL;
			dslot[i].path[0] = '\0';
		}
	}
}

static FILEH dcache_open(const char *path) {

	int		i, victim;
	FILE	*fh;
	long	size;
	MEMFH	*m;
	DSLOT	*sl = NULL;

	for (i = 0; i < DCACHE_SLOTS; i++) {
		if (dslot[i].data && (!strcmp(dslot[i].path, path))) {
			sl = &dslot[i];
			break;
		}
	}
	if (sl == NULL) {
		fh = fopen(path, "rb");
		if (fh == NULL) {
			return(NULL);
		}
		fseek(fh, 0, SEEK_END);
		size = ftell(fh);
		if ((size <= 0) || (size > DCACHE_MAX)) {
			fclose(fh);
			return(NULL);
		}
		victim = 0;
		for (i = 1; i < DCACHE_SLOTS; i++) {
			if (dslot[i].data == NULL) {
				victim = i;
				break;
			}
			if (dslot[i].last < dslot[victim].last) {
				victim = i;
			}
		}
		sl = &dslot[victim];
		if (sl->data) {
			free(sl->data);
			sl->data = NULL;
		}
		sl->data = (UINT8 *)malloc(size);
		if (sl->data == NULL) {
			fclose(fh);
			return(NULL);
		}
		fseek(fh, 0, SEEK_SET);
		if (fread(sl->data, 1, size, fh) != (size_t)size) {
			fclose(fh);
			free(sl->data);
			sl->data = NULL;
			return(NULL);
		}
		fclose(fh);
		sl->size = (UINT)size;
		strncpy(sl->path, path, MAX_PATH - 1);
	}
	sl->last = ++duse;
	m = (MEMFH *)malloc(sizeof(MEMFH));
	if (m == NULL) {
		return(NULL);
	}
	m->magic = MEMFH_MAGIC;
	m->data = sl->data;
	m->size = sl->size;
	m->pos = 0;
	return((FILEH)m);
}

static MEMFH *memfh(FILEH handle) {

	MEMFH *m = (MEMFH *)handle;
	if ((m != NULL) && (m->magic == MEMFH_MAGIC)) {
		return(m);
	}
	return(NULL);
}

static	char	curpath[MAX_PATH] = "./";
static	char	*curfilep = curpath + 2;

/* �t�@�C������ */
FILEH file_open(const char *path) {

	dcache_invalidate(path);
#if defined(WIN32) && defined(OSLANG_UTF8)
	char	sjis[MAX_PATH];
	codecnv_utf8tosjis(sjis, NELEMENTS(sjis), path, (UINT)-1);
	return(fopen(sjis, "rb+"));
#else
	return(fopen(path, "rb+"));
#endif
}

FILEH file_open_rb(const char *path) {

	if (dcache_isimage(path)) {
		FILEH m = dcache_open(path);
		if (m != NULL) {
			return(m);
		}
	}
#if defined(WIN32) && defined(OSLANG_UTF8)
	char	sjis[MAX_PATH];
	codecnv_utf8tosjis(sjis, NELEMENTS(sjis), path, (UINT)-1);
	return(fopen(sjis, "rb"));
#else
	return(fopen(path, "rb"));
#endif
}

FILEH file_create(const char *path) {

	dcache_invalidate(path);
#if defined(WIN32) && defined(OSLANG_UTF8)
	char	sjis[MAX_PATH];
	codecnv_utf8tosjis(sjis, NELEMENTS(sjis), path, (UINT)-1);
	return(fopen(sjis, "wb+"));
#else
	return(fopen(path, "wb+"));
#endif
}

long file_seek(FILEH handle, long pointer, int method) {

	MEMFH *m = memfh(handle);
	if (m) {
		long p = pointer;
		if (method == SEEK_CUR) p += (long)m->pos;
		else if (method == SEEK_END) p += (long)m->size;
		if (p < 0) p = 0;
		if (p > (long)m->size) p = (long)m->size;
		m->pos = (UINT)p;
		return(p);
	}
	fseek(handle, pointer, method);
	return(ftell(handle));
}

UINT file_read(FILEH handle, void *data, UINT length) {

	MEMFH *m = memfh(handle);
	if (m) {
		UINT n = m->size - m->pos;
		if (n > length) n = length;
		CopyMemory(data, m->data + m->pos, n);
		m->pos += n;
		return(n);
	}
	return((UINT)fread(data, 1, length, handle));
}

UINT file_write(FILEH handle, const void *data, UINT length) {

	if (memfh(handle)) {
		return(0);
	}
	return((UINT)fwrite(data, 1, length, handle));
}

short file_close(FILEH handle) {

	MEMFH *m = memfh(handle);
	if (m) {
		free(m);
		return(0);
	}
	fclose(handle);
	return(0);
}

UINT file_getsize(FILEH handle) {

	struct stat sb;
	MEMFH *m = memfh(handle);
	if (m) {
		return(m->size);
	}

	if (fstat(fileno(handle), &sb) == 0)
	{
		return (UINT)sb.st_size;
	}
	return(0);
}

short file_attr(const char *path) {

struct stat	sb;
	short	attr;

#if defined(WIN32) && defined(OSLANG_UTF8)
	char	sjis[MAX_PATH];
	codecnv_utf8tosjis(sjis, NELEMENTS(sjis), path, (UINT)-1);
	if (stat(sjis, &sb) == 0)
#else
	if (stat(path, &sb) == 0)
#endif
	{
#if defined(WIN32)
		if (sb.st_mode & _S_IFDIR) {
			attr = FILEATTR_DIRECTORY;
		}
		else {
			attr = 0;
		}
		if (!(sb.st_mode & S_IWRITE)) {
			attr |= FILEATTR_READONLY;
		}
#else
		if (S_ISDIR(sb.st_mode)) {
			return(FILEATTR_DIRECTORY);
		}
		attr = 0;
		if (!(sb.st_mode & S_IWUSR)) {
			attr |= FILEATTR_READONLY;
		}
#endif
		return(attr);
	}
	return(-1);
}

static BRESULT cnv_sttime(time_t *t, DOSDATE *dosdate, DOSTIME *dostime) {

struct tm	*ftime;

	ftime = localtime(t);
	if (ftime == NULL) {
		return(FAILURE);
	}
	if (dosdate) {
		dosdate->year = ftime->tm_year + 1900;
		dosdate->month = ftime->tm_mon + 1;
		dosdate->day = ftime->tm_mday;
	}
	if (dostime) {
		dostime->hour = ftime->tm_hour;
		dostime->minute = ftime->tm_min;
		dostime->second = ftime->tm_sec;
	}
	return(SUCCESS);
}

short file_getdatetime(FILEH handle, DOSDATE *dosdate, DOSTIME *dostime) {

struct stat sb;

	if (memfh(handle)) {
		return(-1);
	}
	if (fstat(fileno(handle), &sb) == 0) {
		if (cnv_sttime(&sb.st_mtime, dosdate, dostime) == SUCCESS) {
			return(0);
		}
	}
	return(-1);
}

short file_delete(const char *path) {

	return(remove(path));
}

short file_dircreate(const char *path) {

#if defined(WIN32)
	return((short)mkdir(path));
#else
	return((short)mkdir(path, 0777));
#endif
}


/* �J�����g�t�@�C������ */
void file_setcd(const char *exepath) {

	file_cpyname(curpath, exepath, NELEMENTS(curpath));
	curfilep = file_getname(curpath);
	*curfilep = '\0';
}

char *file_getcd(const char *path) {

	file_cpyname(curfilep, path, NELEMENTS(curpath) - (UINT)(curfilep - curpath));
	return(curpath);
}

FILEH file_open_c(const char *path) {

	file_cpyname(curfilep, path, NELEMENTS(curpath) - (UINT)(curfilep - curpath));
	return(file_open(curpath));
}

FILEH file_open_rb_c(const char *path) {

	file_cpyname(curfilep, path, NELEMENTS(curpath) - (UINT)(curfilep - curpath));
	return(file_open_rb(curpath));
}

FILEH file_create_c(const char *path) {

	file_cpyname(curfilep, path, NELEMENTS(curpath) - (UINT)(curfilep - curpath));
	return(file_create(curpath));
}

short file_delete_c(const char *path) {

	file_cpyname(curfilep, path, NELEMENTS(curpath) - (UINT)(curfilep - curpath));
	return(file_delete(curpath));
}

short file_attr_c(const char *path) {

	file_cpyname(curfilep, path, NELEMENTS(curpath) - (UINT)(curfilep - curpath));
	return(file_attr(curpath));
}

#if defined(WIN32)
static BRESULT cnvdatetime(FILETIME *file, DOSDATE *dosdate, DOSTIME *dostime) {

	FILETIME	localtime;
	SYSTEMTIME	systime;

	if ((FileTimeToLocalFileTime(file, &localtime) == 0) ||
		(FileTimeToSystemTime(&localtime, &systime) == 0)) {
		return(FAILURE);
	}
	if (dosdate) {
		dosdate->year = (UINT16)systime.wYear;
		dosdate->month = (UINT8)systime.wMonth;
		dosdate->day = (UINT8)systime.wDay;
	}
	if (dostime) {
		dostime->hour = (UINT8)systime.wHour;
		dostime->minute = (UINT8)systime.wMinute;
		dostime->second = (UINT8)systime.wSecond;
	}
	return(SUCCESS);
}

static BRESULT setflist(WIN32_FIND_DATA *w32fd, FLINFO *fli) {

	if ((w32fd->dwFileAttributes & FILEATTR_DIRECTORY) &&
		((!file_cmpname(w32fd->cFileName, ".")) ||
		(!file_cmpname(w32fd->cFileName, "..")))) {
		return(FAILURE);
	}
	fli->caps = FLICAPS_SIZE | FLICAPS_ATTR;
	fli->size = w32fd->nFileSizeLow;
	fli->attr = w32fd->dwFileAttributes;
	if (cnvdatetime(&w32fd->ftLastWriteTime, &fli->date, &fli->time)
																== SUCCESS) {
		fli->caps |= FLICAPS_DATE | FLICAPS_TIME;
	}
#if defined(OSLANG_UTF8)
	codecnv_sjistoutf8(fli->path, NELEMENTS(fli->path),
												w32fd->cFileName, (UINT)-1);
#else
	file_cpyname(fli->path, w32fd->cFileName, sizeof(fli->path));
#endif
	return(SUCCESS);
}

FLISTH file_list1st(const char *dir, FLINFO *fli) {

	char			path[MAX_PATH];
	HANDLE			hdl;
	WIN32_FIND_DATA	w32fd;

	file_cpyname(path, dir, NELEMENTS(path));
	file_setseparator(path, NELEMENTS(path));
	file_catname(path, "*.*", NELEMENTS(path));
	hdl = FindFirstFile(path, &w32fd);
	if (hdl != INVALID_HANDLE_VALUE) {
		do {
			if (setflist(&w32fd, fli) == SUCCESS) {
				return(hdl);
			}
		} while(FindNextFile(hdl, &w32fd));
		FindClose(hdl);
	}
	return(FLISTH_INVALID);
}

BRESULT file_listnext(FLISTH hdl, FLINFO *fli) {

	WIN32_FIND_DATA	w32fd;

	while(FindNextFile(hdl, &w32fd)) {
		if (setflist(&w32fd, fli) == SUCCESS) {
			return(SUCCESS);
		}
	}
	return(FAILURE);
}

void file_listclose(FLISTH hdl) {

	FindClose(hdl);
}
#else
FLISTH file_list1st(const char *dir, FLINFO *fli) {

	DIR		*ret;

	ret = opendir(dir);
	if (ret == NULL) {
		goto ff1_err;
	}
	if (file_listnext((FLISTH)ret, fli) == SUCCESS) {
		return((FLISTH)ret);
	}
	closedir(ret);

ff1_err:
	return(FLISTH_INVALID);
}

BRESULT file_listnext(FLISTH hdl, FLINFO *fli) {

struct dirent	*de;
struct stat		sb;

	de = readdir((DIR *)hdl);
	if (de == NULL) {
		return(FAILURE);
	}
	if (fli) {
		memset(fli, 0, sizeof(*fli));
		fli->caps = FLICAPS_ATTR;
#if defined(DT_DIR)
		fli->attr = (de->d_type & DT_DIR) ? FILEATTR_DIRECTORY : 0;
#else
		/* PSP newlib の dirent には d_type がない。stat で代用する */
		fli->attr = 0;
#endif

		if (stat(de->d_name, &sb) == 0) {
			fli->caps |= FLICAPS_SIZE;
			fli->size = (UINT)sb.st_size;
#if !defined(DT_DIR)
			if (S_ISDIR(sb.st_mode)) {
				fli->attr |= FILEATTR_DIRECTORY;
			}
#endif
			if (!(sb.st_mode & S_IWUSR)) {
				fli->attr |= FILEATTR_READONLY;
			}
			if (cnv_sttime(&sb.st_mtime, &fli->date, &fli->time) == SUCCESS) {
				fli->caps |= FLICAPS_DATE | FLICAPS_TIME;
			}
		}
		milstr_ncpy(fli->path, de->d_name, NELEMENTS(fli->path));
	}
	return(SUCCESS);
}

void file_listclose(FLISTH hdl) {

	closedir((DIR *)hdl);
}
#endif

void file_catname(char *path, const char *name, int maxlen) {

	int		csize;

	while(maxlen > 0) {
		if (*path == '\0') {
			break;
		}
		path++;
		maxlen--;
	}
	file_cpyname(path, name, maxlen);
	while((csize = milstr_charsize(path)) != 0) {
		if ((csize == 1) && (*path == '\\')) {
			*path = '/';
		}
		path += csize;
	}
}

char *file_getname(const char *path) {

const char	*ret;
	int		csize;

	ret = path;
	while((csize = milstr_charsize(path)) != 0) {
		if ((csize == 1) && (*path == '/')) {
			ret = path + 1;
		}
		path += csize;
	}
	return((char *)ret);
}

void file_cutname(char *path) {

	char	*p;

	p = file_getname(path);
	*p = '\0';
}

char *file_getext(const char *path) {

const char	*p;
const char	*q;

	p = file_getname(path);
	q = NULL;
	while(*p != '\0') {
		if (*p == '.') {
			q = p + 1;
		}
		p++;
	}
	if (q == NULL) {
		q = p;
	}
	return((char *)q);
}

void file_cutext(char *path) {

	char	*p;
	char	*q;

	p = file_getname(path);
	q = NULL;
	while(*p != '\0') {
		if (*p == '.') {
			q = p;
		}
		p++;
	}
	if (q != NULL) {
		*q = '\0';
	}
}

void file_cutseparator(char *path) {

	int		pos;

	pos = (int)strlen(path) - 1;
	if ((pos > 0) &&							// 2�����ȏ�Ł[
		(path[pos] == '/') &&					// �P�c�� \ �Ł[
		((pos != 1) || (path[0] != '.'))) {		// './' �ł͂Ȃ�������
		path[pos] = '\0';
	}
}

void file_setseparator(char *path, int maxlen) {

	int		pos;

	pos = (int)strlen(path);
	if ((pos) && (path[pos-1] != '/') && ((pos + 2) < maxlen)) {
		path[pos++] = '/';
		path[pos] = '\0';
	}
}

