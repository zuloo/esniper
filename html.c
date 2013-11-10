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

#include <ctype.h>
#include <string.h>
#include "buffer.h"
#include "http.h"
#include "html.h"
#include "esniper.h"

/*
 * rudimentary HTML parser, maybe, we should use libxml2 instead?
 */


/*
 * Get next tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
const char *
getTag(memBuf_t *mp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int inStr = 0, comment = 0, c;

	if (memEof(mp)) {
		log(("getTag(): returning NULL\n"));
		return NULL;
	}
	while ((c = memGetc(mp)) != EOF && c != '<')
		;
	if (c == EOF) {
		log(("getTag(): returning NULL\n"));
		return NULL;
	}

	/* first char - check for comment */
	c = memGetc(mp);
	if (c == '>') {
		log(("getTag(): returning empty tag\n"));
		return "";
	} else if (c == EOF) {
		log(("getTag(): returning NULL\n"));
		return NULL;
	}
	addchar(buf, bufsize, count, (char)c);
	if (c == '!') {
		int c2 = memGetc(mp);

		if (c2 == '>' || c2 == EOF) {
			term(buf, bufsize, count);
			log(("getTag(): returning %s\n", buf));
			return buf;
		}
		addchar(buf, bufsize, count, (char)c2);
		if (c2 == '-') {
			int c3 = memGetc(mp);

			if (c3 == '>' || c3 == EOF) {
				term(buf, bufsize, count);
				log(("getTag(): returning %s\n", buf));
				return buf;
			}
			addchar(buf, bufsize, count, (char)c3);
			comment = 1;
		}
	}

	if (comment) {
		while ((c = memGetc(mp)) != EOF) {
			if (c=='>' && buf[count-1]=='-' && buf[count-2]=='-') {
				term(buf, bufsize, count);
				log(("getTag(): returning %s\n", buf));
				return buf;
			}
			if (isspace(c) && buf[count-1] == ' ')
				continue;
			addchar(buf, bufsize, count, (char)c);
		}
	} else {
		while ((c = memGetc(mp)) != EOF) {
			switch (c) {
			case '\\':
				addchar(buf, bufsize, count, (char)c);
				c = memGetc(mp);
				if (c == EOF) {
					term(buf, bufsize, count);
					log(("getTag(): returning %s\n", buf));
					return buf;
				}
				addchar(buf, bufsize, count, (char)c);
				break;
			case '>':
				if (inStr)
					addchar(buf, bufsize, count, (char)c);
				else {
					term(buf, bufsize, count);
					log(("getTag(): returning %s\n", buf));
					return buf;
				}
				break;
			case ' ':
			case '\n':
			case '\r':
			case '\t':
			case '\v':
				if (inStr)
					addchar(buf, bufsize, count, (char)c);
				else if (count > 0 && buf[count-1] != ' ')
					addchar(buf, bufsize, count, ' ');
				break;
			case '"':
				inStr = !inStr;
				/* fall through */
			default:
				addchar(buf, bufsize, count, (char)c);
			}
		}
	}
	term(buf, bufsize, count);
	log(("getTag(): returning %s\n", count ? buf : "NULL"));
	return count ? buf : NULL;
}

/*
 * Get next non-tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
char *
getNonTag(memBuf_t *mp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0, amp = 0;
	int c;

	if (memEof(mp)) {
		log(("getNonTag(): returning NULL\n"));
		return NULL;
	}
	while ((c = memGetc(mp)) != EOF) {
		switch (c) {
		case '<':
			memUngetc(mp);
			if (count) {
				if (buf[count-1] == ' ')
					--count;
				term(buf, bufsize, count);
				log(("getNonTag(): returning %s\n", buf));
				return buf;
			} else
				(void)getTag(mp);
			break;
		case ' ':
		case '\n':
		case '\r':
		case '\t':
		case '\v':
		case 0x82: /* UTF-8 */
		case 0xC2: /* UTF-8 */
		case 0xC3: /* UTF-8 */
		case 0xA0: /* iso-8859-1 nbsp */
			if (count && buf[count-1] != ' ')
				addchar(buf, bufsize, count, ' ');
			break;
		case ';':
			if (amp > 0) {
				char *cp = &buf[amp];

				term(buf, bufsize, count);
				if (*cp == '#') {
					buf[amp-1] = (char)atoi(cp+1);
					count = amp;
				} else if (!strcmp(cp, "amp")) {
					count = amp;
				} else if (!strcmp(cp, "gt")) {
					buf[amp-1] = '>';
					count = amp;
				} else if (!strcmp(cp, "lt")) {
					buf[amp-1] = '<';
					count = amp;
				} else if (!strcmp(cp, "nbsp")) {
					buf[amp-1] = ' ';
					count = amp;
					if (count && buf[count-1] == ' ')
						--count;
				} else if (!strcmp(cp, "quot")) {
					buf[amp-1] = '&';
					count = amp;
				} else
					addchar(buf, bufsize, count, (char)c);
				amp = 0;
			} else
				addchar(buf, bufsize, count, (char)c);
			break;
		case '&':
			amp = count + 1;
			/* fall through */
		default:
			addchar(buf, bufsize, count, (char)c);
		}
	}
	if (count && buf[count-1] == ' ')
		--count;
	term(buf, bufsize, count);
	log(("getNonTag(): returning %s\n", count ? buf : "NULL"));
	return count ? buf : NULL;
} /* getNonTag() */

char *
getNthNonTagFromString(const char *s, int n)
{
	memBuf_t buf;
	int i;

	strToMemBuf(s, &buf);
	for (i = 1; i < n; i++)
		getNonTag(&buf);
	return myStrdup(getNonTag(&buf));
}

char *
getNonTagFromString(const char *s)
{
	memBuf_t buf;

	strToMemBuf(s, &buf);
	return myStrdup(getNonTag(&buf));
}

int
getIntFromString(const char *s)
{
	memBuf_t buf;

	strToMemBuf(s, &buf);
	return atoi(getNonTag(&buf));
}

const char PAGENAME[] = "var pageName = \"";

/*
 * Get pagename variable, or NULL if not found.
 */
char *
getPageName(memBuf_t *mp)
{
	const char *line;

	log(("getPageName():\n"));
	while ((line = getTag(mp))) {
		char *tmp;

		if (strncmp(line, "!--", 3))
			continue;
		if ((tmp = strstr(line, PAGENAME))) {
			tmp = getPageNameInternal(tmp);
			log(("getPageName(): pagename = %s\n", nullStr(tmp)));
			return tmp;
		}
	}
	log(("getPageName(): Cannot find pagename, returning NULL\n"));
	return NULL;
}

char *
getPageNameInternal(char *s)
{
	char *pagename = s + sizeof(PAGENAME) - 1;
	char *quote = strchr(pagename, '"');

	if (!quote) { /* TG: returns NULL, not \0 */
		log(("getPageNameInternal(): Cannot find trailing quote in pagename: %s\n", pagename));
		return NULL;
	}
	*quote = '\0';
	log(("getPageName(): pagename = %s\n", pagename));
	return pagename;
}

/*
 * Search to end of table, returning /table tag (or NULL if not found).
 * Embedded tables are skipped.
 */
const char *
getTableEnd(memBuf_t *mp)
{
	int nesting = 1;
	const char *cp;

	while ((cp = getTag(mp))) {
		if (!strcmp(cp, "/table")) {
			if (--nesting == 0)
				return cp;
		} else if (!strncmp(cp, "table", 5) &&
			   (isspace((int)*(cp+5)) || *(cp+5) == '\0')) {
			++nesting;
		}
	}
	return NULL;
}

/*
 * Search for next table item.  Return NULL at end of a row, and another NULL
 * at the end of a table.
 */
char *
getTableCell(memBuf_t *mp)
{
	int nesting = 1;
	const char *cp, *start = mp->readptr, *end = NULL;
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;

	while ((cp = getTag(mp))) {
		if (nesting == 1 && 
		    (!strncmp(cp, "td", 2) || !strncmp(cp, "th", 2)) &&
		    (isspace((int)*(cp+2)) || *(cp+2) == '\0')) {
			/* found <td>, now must find </td> */
			start = mp->readptr;
		} else if (nesting == 1 && 
			(!strcmp(cp, "/td") || !strcmp(cp, "/th"))) {
			/* end of this item */
			for (end = mp->readptr - 1; *end != '<'; --end)
				;
			for (cp = start; cp < end; ++cp) {
				addchar(buf, bufsize, count, *cp);
			}
			term(buf, bufsize, count);
			return buf;
		} else if (nesting == 1 && !strcmp(cp, "/tr")) {
			/* end of this row */
			return NULL;
		} else if (!strcmp(cp, "/table")) {
			/* end of this table? */
			if (--nesting == 0)
				return NULL;
		} else if (!strncmp(cp, "table", 5) &&
			   (isspace((int)*(cp+5)) || *(cp+5) == '\0')) {
			++nesting;
		}
	}
	/* error? */
	return NULL;
}

/*
 * Return NULL-terminated table row, or NULL at end of table.
 * All cells are malloc'ed and should be freed by the calling function.
 */
char **
getTableRow(memBuf_t *mp)
{
	char **ret = NULL, *cp = NULL;
	size_t size = 0, i = 0;

	do {
		cp = getTableCell(mp);
		if (cp || i) {
			if (i >= size) {
				size += 10;
				ret = (char **)myRealloc(ret, size * sizeof(char *));
			}
			ret[i++] = myStrdup(cp);
		}
	} while ((cp));
	return ret;
}

/*
 * Return number of columns in row, or -1 if null.
 */
int
numColumns(char **row)
{
	int ncols = 0;

	if (!row)
		return -1;
	while (row[ncols++])
		;
	return --ncols;
}

/*
 * Free a TableRow allocated by getTableRow
 */
void
freeTableRow(char **row)
{
	char **cpp;

	if (row) {
		for (cpp = row; *cpp; ++cpp)
			free(*cpp);
		free(row);
	}
}

/*
 * Search for next table tag.
 */
const char *
getTableStart(memBuf_t *mp)
{
	const char *cp;

	while ((cp = getTag(mp))) {
		if (!strncmp(cp, "table", 5) &&
		    (isspace((int)*(cp+5)) || *(cp+5) == '\0'))
			return cp;
	}
	return NULL;
}
