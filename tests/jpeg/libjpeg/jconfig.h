/* jconfig.h for building IJG libjpeg with CMake on any C99 compiler. */
#define HAVE_PROTOTYPES
#define HAVE_UNSIGNED_CHAR
#define HAVE_UNSIGNED_SHORT
#define HAVE_STDDEF_H
#define HAVE_STDLIB_H
#ifdef _WIN32
#ifndef __RPCNDR_H__
typedef unsigned char boolean;
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#define HAVE_BOOLEAN
#endif
