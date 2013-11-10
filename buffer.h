/*
 * Copyright (c) 2002, 2003, Scott Nicol <esniper@users.sf.net>
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BUFFER_H_INCLUDED
#define BUFFER_H_INCLUDED

#include <stdlib.h>

#ifdef __lint
extern int NEVER;
#else
#define NEVER 0
#endif

/*
 * Buffer handling code
 *
 * Don't call resize directly, just start out with a clean buffer
 * (buf = NULL, bufsize = 0, count = 0) and addchar() as much as necessary,
 * then term() to null-terminate it.
 */

#define addchar(buf, bufsize, count, c) \
	do {\
		if (count >= bufsize)\
			buf = resize(buf, &bufsize, (size_t)1024);\
		buf[count++] = c;\
	} while (NEVER)

#define term(buf, bufsize, count) \
	do {\
		if (count >= bufsize)\
			buf = resize(buf, &bufsize, (size_t)1024);\
		buf[count] = '\0';\
	} while (NEVER)

#define addcharinc(buf, bufsize, count, c, inc) \
	do {\
		if (count >= bufsize)\
			buf = resize(buf, &bufsize, inc);\
		buf[count++] = c;\
	} while (NEVER)

#define terminc(buf, bufsize, count, inc) \
	do {\
		if (count >= bufsize)\
			buf = resize(buf, &bufsize, inc);\
		buf[count] = '\0';\
	} while (NEVER)

extern char *resize(char *buf, size_t *size, size_t inc);

#endif /* BUFFER_H_INCLUDED */
