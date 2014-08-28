/* 
 * open_noatime.so: Override open() and openat() such that all files
 * are attempted to be opened using O_NOATIME, and retrying without if
 * the first attempt fails with EPERM.
 *
 * Useful for backup and indexing programs which read a lot of files.
 *
 * License: Do whatever you want.
 *
 * Compile:
 *
 *   cc -O2 -Wall -Wextra -shared -fPIC -o open_noatime.so open_noatime.c -ldl
 *
 * Install:
 *
 *   sudo install -m 0644 open_noatime.so /usr/local/lib
 *
 * Use:
 *
 *   LD_PRELOAD=/usr/local/lib/open_noatime.so  /some/program
 *
 * or, if you are editing a "launcher icon" in some GUI, 
 *
 *   env LD_PRELOAD=/usr/local/lib/open_noatime.so  /some/program
 *
 */

#include <sys/types.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>

#if !(defined(O_NOATIME) && O_NOATIME != 0)
#error "You do not seem to have O_NOATIME, so there's no point in using this shared library."
#endif

/*
 * The correct test for whether a mode argument is present is (flags &
 * (O_CREAT | __O_TMPFILE)), if __O_TMPFILE is defined (whether or not
 * the kernel supports it). But if __O_TMPFILE is not defined in the
 * headers, and one subsequently installs newer glibc/kernel and a
 * program using O_TMPFILE, this shared library must also be
 * recompiled.
 *
 * The damage is not huge, however: The mode of an O_TMPFILE only
 * matters if it is subsequently linked into the file system, which is
 * probably not that usual.
 */
#if !defined(__O_TMPFILE)
#define __O_TMPFILE 0
#warning "Your headers don't have O_TMPFILE. Don't use this shared library with programs using O_TMPFILE."
#endif


/* 
 * Use the declarations from fcntl.h to declare typedefs for functions
 * pointers of the appropriate type. We need to put this here before
 * we declare our own versions of open and openat. If you don't have
 * typeof(), just replace with
 *
 * typedef int (*libc_open_t)(const char*, int, ...);
 * typedef int (*libc_openat_t)(int, const char*, int, ...);
 */

typedef typeof(open) *libc_open_t;
typedef typeof(openat) *libc_openat_t;

/*
 * If mode_t is strictly narrower than int, an int is passed to open()
 * as the vararg. In that case, we need to retrieve the mode using
 * va_arg(ap, int). However, if mode_t is strictly wider than int, we
 * would have to use va_arg(ap, mode_t), and this is arguably also the
 * right thing to use when they have the same width.
 *
 * Unfortunately, when mode_t is unsigned int (which is the case on
 * 64bit linux), the obvious attempt
 * 
 *   (sizeof(mode_t) < sizeof(int)) ? va_arg(ap, int) : va_arg(ap, mode_t)
 *
 * makes gcc (with -Wextra) warn "signed and unsigned type in
 * conditional expression" (the warning is legit, but the wording
 * sucks; <http://gcc.gnu.org/bugzilla/show_bug.cgi?id=56153>;
 * -Wsign-conversion gives another more accurate warning).
 *
 * The proper way to do this is probably to use mode_t's promoted type
 * as the second argument to va_arg. Since mode_t is an integral type,
 * we can get that using typeof(+(mode_t)0); (mode_t)0 is an
 * expression of type mode_t, and unary + forces application of the
 * "integer promotions".
 */

int
open(const char *path, int flags, ...)
{
	static libc_open_t libc_open = NULL;
	va_list ap;
	mode_t mode = 0;
	int fd;

	if (flags & (O_CREAT | __O_TMPFILE)) {
		va_start(ap, flags);
		mode = va_arg(ap, typeof(+(mode_t)0));
		va_end(ap);
	}

	/*
	 * I'm assuming that assigning to a pointer variable is
	 * atomic, and dlsym(RTLD_NEXT, "open") should return the same
	 * value every time, so thread safety shouldn't be an issue.
	 */
	if (libc_open == NULL)
		libc_open = dlsym(RTLD_NEXT, "open");
	assert(libc_open != NULL);

	fd = libc_open(path, flags | O_NOATIME, mode);
	if (fd < 0 && errno == EPERM)
		fd = libc_open(path, flags & ~O_NOATIME, mode);

	return fd;
}

int
openat(int dirfd, const char *path, int flags, ...)
{
	static libc_openat_t libc_openat = NULL;
	va_list ap;
	mode_t mode = 0;
	int fd;

	if (flags & (O_CREAT | __O_TMPFILE)) {
		va_start(ap, flags);
		mode = va_arg(ap, typeof(+(mode_t)0));
		va_end(ap);
	}
	
	if (libc_openat == NULL)
		libc_openat = dlsym(RTLD_NEXT, "openat");
	assert(libc_openat != NULL);
	
	fd = libc_openat(dirfd, path, flags | O_NOATIME, mode);
	if (fd < 0 && errno == EPERM)
		fd = libc_openat(dirfd, path, flags & ~O_NOATIME, mode);

	return fd;
}
