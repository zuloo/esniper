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

#ifndef HTTP_H_INCLUDED
#define HTTP_H_INCLUDED

#include "auctioninfo.h"

typedef struct {
   char *memory;
   size_t size;
   char *readptr;
   time_t timeToFirstByte;
} memBuf_t;

extern int memEof(memBuf_t *mp);
extern int memGetc(memBuf_t *mp);
extern void memUngetc(memBuf_t *mp);
extern void memReset(memBuf_t *mp);
extern void memSkip(memBuf_t *mp, int n);
extern char *memStr(memBuf_t *mp, const char *s);
extern char *memChr(memBuf_t *mp, char c);
extern char *memGetMetaRefresh(memBuf_t *mp);
extern time_t getTimeToFirstByte(memBuf_t *mp);

extern int initCurlStuff(void);
extern void cleanupCurlStuff(void);

extern int httpError(auctionInfo *aip);
extern memBuf_t *httpGet(const char *url, const char *logUrl);
extern memBuf_t *httpPost(const char *url, const char *data, const char *logData);
extern void freeMembuf(memBuf_t *mp);
extern memBuf_t *strToMemBuf(const char *s, memBuf_t *buf);

#include <stdio.h>
extern memBuf_t *readFile(FILE *fp);

#endif /* HTTP_H_INCLUDED */
