/*
 * Copyright (c) 2002, 2003, 2004, Scott Nicol <esniper@users.sf.net>
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

/*
 * This program will "snipe" an auction on eBay, automatically placing
 * your bid a few seconds before the auction ends.
 *
 * For updates, bug reports, etc, please go to http://esniper.sf.net/.
 */

#include "options.h"
#include "buffer.h"
#include "esniper.h"
#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int parseConfigValue(const char *name, const char *value,
			    optionTable_t *table, const char *filename,
			    const char *line);
static int parseBoolValue(const char *name, const char *value,
			  optionTable_t *tableptr, const char *filename,
			  const char *line, int neg);
static int parseStringValue(const char *name, const char *value,
			    optionTable_t *tableptr, const char *filename,
			    const char *line);
static int parseIntValue(const char *name, const char *value,
			 optionTable_t *tableptr, const char *filename,
			 const char *line);
static int parseSpecialValue(const char *name, const char *value,
			     optionTable_t *tableptr,
			     const char *filename, const char *line);

/*
 * readConfigFile(): read configuration from file, skipping auctions
 *
 * returns:
 *	0 file successfully read, even if no configuration options found
 *	1 file not found
 *	2 other error
 */
int
readConfigFile(const char *filename, optionTable_t *table)
{
	char *buf = NULL;
	size_t bufsize = 0, count = 0;
	int c, ret = 0;
	FILE *fp = fopen(filename, "r");

	if (fp == NULL) {
		/* file not found is OK */
		if (errno == ENOENT)
			return 1;
		printLog(stderr, "Cannot open %s: %s\n", filename,
			 strerror(errno));
		return 2;
	}

	while ((c = getc(fp)) != EOF) {
		if (isspace(c))
			continue;
		/* skip comments and anything starting with a number,
		 * assuming this is an auction entry */
		if ((c == '#') || isdigit(c))
			c = skipline(fp);
		else if (isalpha(c)) {
			char *name = NULL, *value = NULL;

			count = 0;
			do {
				addchar(buf, bufsize, count, (char)c);
				c = getc(fp);
			} while (c != EOF && !isspace(c) && c != '=');
			addchar(buf, bufsize, count, '\0');
			name = buf;

			if (c != EOF && c != '\n' && c != '\r') {
				do {
					c = getc(fp);
				} while (c == ' ' || c == '\t');
				if (c == '=') {
					do {
						c = getc(fp);
					} while (c == ' ' || c == '\t');
				}

				if (c != EOF && c != '\n' && c != '\r') {
					value = &buf[count];
					do {
						addchar(buf, bufsize, count, (char)c);
						c = getc(fp);
					} while (c != EOF && c != '\n' && c != '\r');
					/* strip trailing whitespace */
					while (isspace((int)(buf[count - 1])))
						--count;
					term(buf, bufsize, count);
				}
			}
			if (parseConfigValue(name, value, table, filename, buf))
				ret = 2;
		}

		/* don't read EOF twice! */
		if (c == EOF)
			break;
	}

	if (ferror(fp)) {
		printLog(stderr, "Cannot read %s: %s\n", filename,
			 strerror(errno));
		ret = 2;
	}
	fclose(fp);
	free(buf);
	return ret;
} /* readConfigFile() */

/*
 * parseGetoptValue(): parse option character and value
 *
 * returns: 0 = OK, else error
 */
int
parseGetoptValue(int option, const char *optval, optionTable_t *table)
{
	char optstr[] = { '\0', '\0' };

	optstr[0] = (char)option;
	/* optval "" should be handled the same as empty configuration option */
	if (optval && !*optval)
		optval = NULL;
	/* filename NULL means command line option */
	return parseConfigValue(optstr, optval, table, NULL, optstr);
}

/*
 * parseConfigValue(): lookup command line or configuration option in option
 *		table and call appropriate function according to table entry.
 *
 * returns: 0 = OK, else error
 */
static int
parseConfigValue(const char *name, const char *value,
	optionTable_t *table, const char *filename, const char *line)
{
	optionTable_t *tableptr;
	const char *tablename;
	int ret = 0;

	if (strcmp(name, "password")) {
		log(("parsing name %s value %s\n", name, nullStr(value)));
	}

	/* lookup name in table */
	for (tableptr=table; tableptr->value; tableptr++) {
		tablename = filename ?
				tableptr->configname : tableptr->optionname;
		if (tablename && !strcmp(name, tablename))
			break;
	}
	if (tableptr->value) {	/* found */
		switch (tableptr->type) {
		case OPTION_BOOL:
		case OPTION_BOOL_NEG:
			ret = parseBoolValue(name, value, tableptr, filename,
				line, (tableptr->type == OPTION_BOOL_NEG));
			break;
		case OPTION_STRING:
			ret = parseStringValue(name, value, tableptr, filename,
				line);
			break;
		case OPTION_SPECINT:
		case OPTION_SPECSTR:
			ret = parseSpecialValue(name, value, tableptr, filename,
				line);
			break;
		case OPTION_INT:
			ret = parseIntValue(name, value, tableptr, filename,
				line);
			break;
		default:
			printLog(stderr, "Internal error: invalid type in option table (%s)", tableptr->configname ? tableptr->configname : tableptr->optionname);
			ret = 1;
		}
	} else {
		if (filename)
			printLog(stderr, "Unknown configuration entry \"%s\" in file %s\n", line, filename);
		else
			printLog(stderr, "Unknown command line option -%s\n",
				 line);
		ret = 1;
	}
	return ret;
}

/*
 * parseBoolValue(): parse a boolean value
 *
 * returns: 0 = OK, else error
 */
static int
parseBoolValue(const char *name, const char *value,
	optionTable_t *tableptr, const char *filename, const char *line,
	int neg)
{
	int intval = boolValue(value);

	if (intval == -1) {
		if (filename)
			printLog(stderr, "Invalid boolean value in file %s, line \"%s\"\n", filename, line);
		else
			printLog(stderr, "Invalid boolean value \"%s\" at command line option -%s\n", value, line);
		return 1;
	}
	if (neg)
		intval = !intval;
	if (tableptr->checkfunc) { /* check value with check function */
		if ((*tableptr->checkfunc)(&intval, tableptr, filename, line))
			return 1;
	} else
		*(int*)(tableptr->value) = intval;
	tableptr->isSet++;
	log(("bool value for %s is %d\n", name, *(int*)(tableptr->value)));
	return 0;
}

/*
 * parseStringValue(): parse a string value, call checking func if specified
 *
 * returns: 0 = OK, else error
 */
static int
parseStringValue(const char *name, const char *value,
	optionTable_t *tableptr, const char *filename, const char *line)
{
	if (tableptr->checkfunc) {
		/* Check value with specific check function.
		 * Check function is responsible for allocating/freeing values
		 */
		if ((*tableptr->checkfunc)(value, tableptr, filename, line))
			return 1;
	} else {
		free(*(char**)(tableptr->value));
		*(char**)(tableptr->value) = myStrdup(value);
	}
	tableptr->isSet++;
	log(("string value for %s is \"%s\"\n",
	     name, nullStr(*(char**)(tableptr->value))));
	return 0;
}

/*
 * parseSpecialValue(): parse a special value, which is is not interpreted here
 *			A checking func is required to convert/check value.
 *
 * returns: 0 = OK, else error
 */
static int
parseSpecialValue(const char *name, const char *value,
	optionTable_t *tableptr, const char *filename, const char *line)
{
	if (tableptr->checkfunc) { /* check value with check function */
		if ((*tableptr->checkfunc)(value, tableptr, filename, line))
			return 1;
	} else {
		printLog(stderr, "Internal error: special type needs check function in option table (%s)", tableptr->configname ? tableptr->configname : tableptr->optionname);
		return 1;
	}
	tableptr->isSet++;
	return 0;
}

/*
 * parseIntValue(): parse an integer value, call checking func if specified
 *
 * returns: 0 = OK, else error
 */
static int
parseIntValue(const char *name, const char *value,
	optionTable_t *tableptr, const char *filename, const char *line)
{
	long longval;
	int intval;
	char *endptr;

	if (!value) {
		if (filename)
			printLog(stderr, "Configuration option \"%s\" in file %s needs an integer value\n", line, filename);
		else
			printLog(stderr, "Option -%s needs an integer value\n",
				 line);
		return 1;
	}
	longval = strtol(value, &endptr, 10);
	if (*endptr != '\0' || longval > INT_MAX || longval < INT_MIN) {
		if (filename)
			printLog(stderr, "Invalid integer value at configuration option \"%s\" in file %s\n", line, filename);
		else
			printLog(stderr, "Invalid integer value \"%s\" at command line option -%s\n", value, line);
		return 1;
	}
	intval = (int)longval;

	if (tableptr->checkfunc) {
		/* check value with specific check function */
		if ((*tableptr->checkfunc)(&intval, tableptr, filename, line))
			return 1;
	} else
		*(int*)(tableptr->value) = intval;
	tableptr->isSet++;
	log(("integer value for %s is %d\n", name, *(int*)(tableptr->value)));
	return 0;
}

/**
 * Write all user-specified option values to a buffer.
 * The buffer is dynamically allocated and must be freed by the caller.
 *
 * @param table pointer to the option table
 * @return pointer to text buffer
 */
char * logOptionValues(const optionTable_t *table)
{
	char *res = myStrdup("\tspecified options or config values:\n");
	char *tmp;
	char buf[1024];
	const optionTable_t *tableptr;
	for(tableptr = table; NULL != tableptr->value; ++tableptr)
	{
		if(tableptr->isSet)
		{
			if(tableptr->logging == LOG_NORMAL)
			{
				switch(tableptr->type)
				{
				case OPTION_STRING:
				case OPTION_SPECSTR:
					sprintf(buf, "\t %2d x %.15s(%.15s) = \"%.900s\"\n",
						tableptr->isSet,
						nullEmptyStr(tableptr->configname),
						nullEmptyStr(tableptr->optionname),
						nullStr(*(char**)(tableptr->value))
						);
					break;
				case OPTION_INT:
				case OPTION_SPECINT:
				case OPTION_BOOL:
				case OPTION_BOOL_NEG:
					sprintf(buf, "\t %2d x %.15s(%.15s) = %d\n",
						tableptr->isSet,
						nullEmptyStr(tableptr->configname),
						nullEmptyStr(tableptr->optionname),
						*(int*)(tableptr->value)
						);
					break;
				default:
					sprintf(buf, "\t %2d x %.15s(%.15s) = (unknown type)\n",
						tableptr->isSet,
						nullEmptyStr(tableptr->configname),
						nullEmptyStr(tableptr->optionname)
						);
					break;
				}
			} else {
				sprintf(buf, "\t %2d x %.15s(%.15s) = ***\n",
					tableptr->isSet,
					nullEmptyStr(tableptr->configname),
					nullEmptyStr(tableptr->optionname)
					);
			}
			tmp = myStrdup2(res, buf);
			free(res);
			res = tmp;
		}
	}
	return res;
}

