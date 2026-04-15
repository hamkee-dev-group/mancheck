#ifndef MC_TS_COMPAT_H
#define MC_TS_COMPAT_H

/* This header is force-included for analyzer build, before any vendor headers.
 * It provides missing endian macros used by Tree-sitter on some platforms.
 *
 * Because this is force-included (-include), it runs before any source file's
 * own feature-test macros.  Define them here so that popen/pclose/etc. are
 * visible even under -std=c11.
 */
#ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 700
#endif

#if defined(__linux__)
#  include <endian.h>
#endif

#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#endif

#ifndef le16toh
/* On a little-endian host, little-endian -> host is identity. */
#  define le16toh(x) (x)
#endif

#ifndef be16toh
/* big-endian -> host; on little-endian, byte swap */
#  if defined(__GNUC__)
#    define be16toh(x) __builtin_bswap16(x)
#  else
#    define be16toh(x) ((((x) & 0x00ffu) << 8) | (((x) & 0xff00u) >> 8))
#  endif
#endif

#endif /* MC_TS_COMPAT_H */
