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

#ifndef ESNIPER_H_INCLUDED
#define ESNIPER_H_INCLUDED

#include "options.h"
#include "util.h"

/* this structure holds all values from command line or config entries */
typedef struct {
	char *username;
	char *usernameEscape;	/* URL escaped */
	char *password;
	int bidtime;
	int quantity;
	char *conffilename;
	char *auctfilename;
	int bid;
	int reduce;
	int debug;
	int usage;
	int info;
	int myitems;
	int batch;
	int encrypted;
	char *proxy;
	char *logdir;
	char *historyHost;
	char *prebidHost;
	char *bidHost;
	char *loginHost;
	char *myeBayHost;
	int curldebug;
	int delay;
} option_t;

extern option_t options;
extern optionTable_t optiontab[];

extern const char *getVersion(void);
extern const char *getProgname(void);

#ifdef __lint
#define log(x) if (!options.debug) 0; else dlog x
#else
#define log(x) if (!options.debug) ; else dlog x
#endif

#endif /* ESNIPER_H_INCLUDED */
