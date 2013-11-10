/*
 * Copyright (c) 2002, 2007, Scott Nicol <esniper@users.sf.net>
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

#ifndef HTML_H_
#define HTML_H_

#include "http.h"

/*
 * Get next tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
extern const char *getTag(memBuf_t *mp);

/*
 * Get next non-tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
extern char *getNonTag(memBuf_t *mp);
extern char *getNthNonTagFromString(const char *s, int n);
extern char *getNonTagFromString(const char *s);
extern int getIntFromString(const char *s);

/*
 * Get pagename variable, or NULL if not found.
 */
extern char *getPageName(memBuf_t *mp);
extern char *getPageNameInternal(char *s);

extern const char PAGENAME[];

/*
 * Search for next table tag.
 */
extern const char *getTableStart(memBuf_t *mp);

/*
 * Return NULL-terminated table row, or NULL at end of table.
 * All cells are malloc'ed and should be freed by the calling function.
 */
extern char **getTableRow(memBuf_t *mp);

/*
 * Free a TableRow allocated by getTableRow
 */
extern void freeTableRow(char **row);

/*
 * Return number of columns in row, or -1 if null.
 */
extern int numColumns(char **row);

/*
 * Search for next table item.  Return NULL at end of a row, and another NULL
 * at the end of a table.
 */
extern char *getTableCell(memBuf_t *mp);

/*
 * Search to end of table, returning /table tag (or NULL if not found).
 * Embedded tables are skipped.
 */
extern const char *getTableEnd(memBuf_t *mp);


#endif /*HTML_H_*/
