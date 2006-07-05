/*
 * This file is part of the Mingw32 package.
 *
 * unistd.h maps (roughly) to io.h
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#ifdef _MSC_VER
#include <io.h>
#include <process.h>
typedef size_t ssize_t;
#endif

#endif /* _UNISTD_H */
