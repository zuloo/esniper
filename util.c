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

#include "util.h"
#include "esniper.h"
#include "auction.h"
#include "buffer.h"
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#if defined(WIN32)
#	include <windows.h>
#	include <io.h>
#	include <sys/timeb.h>
#else
#	include <pwd.h>
#	include <sys/types.h>
#	include <sys/time.h>
#	include <termios.h>
#	include <unistd.h>
#endif

static void vlog(const char *fmt, va_list arglist);
static void toLowerString(char *s);
static void seedPasswordRandom(void);
static void cryptPassword(char *password);

/*
 * various utility functions used in esniper.
 */

/*
 * Replacement malloc/realloc/strdup, with error checking
 */

void *
myMalloc(size_t size)
{
	void *ret = malloc(size);

	if (!ret) {
		printLog(stderr, "Cannot allocate memory: %s\n", strerror(errno));
		exit(1);
	}
	return ret;
}

void *
myRealloc(void *buf, size_t size)
{
	void *ret = buf ? realloc(buf, size) : malloc(size);

	if (!ret) {
		printLog(stderr, "Cannot reallocate memory: %s\n", strerror(errno));
		exit(1);
	}
	return ret;
}

char *
myStrdup(const char *s)
{
	char *ret;
	size_t len;

	if (!s)
		return NULL;
	len = strlen(s);
	ret = myMalloc(len + 1);
	memcpy(ret, s, len);
	ret[len] = '\0';
	return ret;
}

char *
myStrndup(const char *s, size_t len)
{
	char *ret;

	if (!s)
		return NULL;
	ret = myMalloc(len + 1);
	memcpy(ret, s, len);
	ret[len] = '\0';
	return ret;
}

char *
myStrdup2(const char *s1, const char *s2)
{
	char *ret = myMalloc(strlen(s1) + strlen(s2) + 1);

	sprintf(ret, "%s%s", s1, s2);
	return ret;
}

char *
myStrdup3(const char *s1, const char *s2, const char *s3)
{
	char *ret = myMalloc(strlen(s1) + strlen(s2) + strlen(s3) + 1);

	sprintf(ret, "%s%s%s", s1, s2, s3);
	return ret;
}

char *
myStrdup4(const char *s1, const char *s2, const char *s3, const char *s4)
{
	char *ret = myMalloc(strlen(s1) + strlen(s2) + strlen(s3) + strlen(s4) + 1);

	sprintf(ret, "%s%s%s%s", s1, s2, s3, s4);
	return ret;
}

/*
 * Debugging functions.
 */

static FILE *logfile = NULL;

void
logClose()
{
	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}
}

void
logOpen(const auctionInfo *aip, const char *logdir)
{
	char *logfilename;

	if (aip == NULL)
		logfilename = myStrdup2(getProgname(), ".log");
	else
		logfilename = myStrdup4(getProgname(), ".", aip->auction, ".log");
	if (logdir) {
		char *tmp = logfilename;

/* not win32 --> *nix */
#if defined(WIN32)
		logfilename = myStrdup3(logdir, "/", logfilename);
#else
		/*
		 * Usually the logdir on *nix looks something like this: ~/esniper/logs
		 * we want it to look (typically) like this: /home/user/esniper/logs/
		 * (depends on environment HOME variable).
		 *
		 * Need to distinguish between * ~/ (i.e. $HOME) and
		 * ~foo/ (i.e. foo's home directory in /etc/passwd).
		 */
		if (logdir[0] == '~') {
			if (logdir[1] == '\0') {
				logfilename = myStrdup3(getenv("HOME"), "/", logfilename);
			} else if (logdir[1] == '/') {
				logfilename = myStrdup4(getenv("HOME"), logdir+1, "/", logfilename);
			} else {
				const char *slash = strchr(logdir, '/');
				struct passwd *pw;

				if (slash) {
					size_t namelen = (size_t)(slash - (logdir+1));
					char *username = myMalloc(namelen + 1);

					strncpy(username, logdir + 1, namelen);
					username[namelen] = '\0';
					pw = getpwnam(username);
					free(username);
				} else {
					slash = logdir + strlen(logdir);
					pw = getpwnam(logdir + 1);
				}

				if (pw)
					logfilename = myStrdup4(pw->pw_dir, slash, "/", logfilename);
				else
					logfilename = myStrdup3(logdir, "/", logfilename);
			}
		} else
			logfilename = myStrdup3(logdir, "/", logfilename);
#endif
		free(tmp);
	}
	logClose();
	if (!(logfile = fopen(logfilename, "a"))) {
		/* non-fatal error! */
		fprintf(stderr, "Unable to open log file %s: %s\n",
			logfilename, strerror(errno));
	} else
		dlog("### %s version %s ###\n", getProgname(), getVersion());
	free(logfilename);
}

/*
 * va_list version of log
 */
static void
vlog(const char *fmt, va_list arglist)
{
#if defined(WIN32)
	struct timeb tb;
#else
	struct timeval tv;
#endif
	char timebuf[80];	/* more than big enough */
	time_t t;

	if (!logfile)
		return;

#if defined(WIN32)
	ftime(&tb);
	t = (time_t)(tb.time);
	strftime(timebuf, sizeof(timebuf), "\n\n*** %Y-%m-%d %H:%M:%S", localtime(&t));
	fprintf(logfile, "%s.%03d ", timebuf, tb.millitm);
#else
	gettimeofday(&tv, NULL);
	t = (time_t)(tv.tv_sec);
	strftime(timebuf, sizeof(timebuf), "\n\n*** %Y-%m-%d %H:%M:%S", localtime(&t));
	fprintf(logfile, "%s.%06ld ", timebuf, (long)tv.tv_usec);
#endif
	vfprintf(logfile, fmt, arglist);
	fflush(logfile);
}

/*
 * Debugging log function.  Use like printf.  Or, better yet, use the log()
 * macro (but be sure to enclose the argument list in two parens)
 */
void
dlog(const char *fmt, ...)
{
	va_list arglist;

	va_start(arglist, fmt);
	vlog(fmt, arglist);
	va_end(arglist);
}

/*
 * Send message to log file and stderr
 */
void
printLog(FILE *fp, const char *fmt, ...)
{
	va_list arglist;

	if (options.debug && logfile) {
		va_start(arglist, fmt);
		vlog(fmt, arglist);
		va_end(arglist);
	}
	va_start(arglist, fmt);
	vfprintf(fp, fmt, arglist);
	va_end(arglist);
	fflush(fp);
}

static const char ESNIPER_VERSION_URL[] = "http://esniper.sourceforge.net/version.txt";

/*
 * Return current version from esniper.sf.net if it is different from 
 * program's version, otherwise return NULL.
 */
const char *
checkVersion(void)
{
	static int result = -1;
	static char *newVersion = NULL;

	if (result == -1) {
		int i;
		memBuf_t *mp = httpGet(ESNIPER_VERSION_URL, NULL);

		/* not available */
		if (mp == NULL)
			return NULL;
		newVersion = mp->memory;
		for (i = 0; newVersion[i] && newVersion[i] != '\n'; ++i)
			;
		newVersion[i] = '\0';
		result = !strcmp(getVersion(), newVersion);
		/* Don't use freeMembuf(), it will also free
		 * mp->memory, which we are using.
		 */
		free(mp);
	}
	return result ? NULL : newVersion;
}

/*
 * Request user to file a bug report.
 */
void
bugReport(const char *func, const char *file, int line, auctionInfo *aip, memBuf_t *mp, const optionTable_t *optiontab, const char *fmt, ...)
{
	va_list arglist;
	const char *version = getVersion();
	const char *newVersion = checkVersion();
	char *optionlog;

	if (newVersion) {
		printLog(stdout,
			"esniper encountered a bug.  "
			"It looks like your esniper version is not\n"
			"current.  You have version %s, the "
			"newest version is %s.\n"
			"Please go to http://esniper.sf.net/ and update your "
			"copy of esniper.\n"
			"\n"
			"If you want to report this bug, please go to:\n",
			version, newVersion);
	} else
		printLog(stdout, "esniper encountered a bug.  Please go to:\n");
	printLog(stdout,
		"\thttp://sourceforge.net/tracker/?func=add&group_id=45285&atid=442436\n"
		"paste this into \"Detailed Description\":\n"
		"\tAutomated esniper bug report.\n"
		"\t%s version %s\n"
		"\t%s\n"
		"\tError encountered in function %s in %s line %d\n",
		getProgname(), version, curl_version(), func, file, line);

	if (aip) {
		printLog(stdout,
			"\tauction = %s, price = %s, remain = %d\n"
			"\tlatency = %d, result = %d, error = %d\n",
			nullStr(aip->auction), nullStr(aip->bidPriceStr),
			aip->remain, aip->latency, aip->bidResult,
			aip->auctionError);
	}

	if (mp) {
		pageInfo_t *pp;

		printLog(stdout,
			"\tbuf = %p, size = %d, read = %p\n"
			"\ttime = %d, offset = %d\n",
			mp->memory, mp->size, mp->readptr,
			mp->timeToFirstByte, mp->readptr - mp->memory);
		if ((pp = getPageInfo(mp))) {
			printLog(stdout,
				 "\tpagename = \"%s\", pageid = \"%s\", srcid = \"%s\"\n",
				 nullStr(pp->pageName), nullStr(pp->pageId),
				 nullStr(pp->srcId));
			freePageInfo(pp);
		}
	}
	if(optiontab) {
		optionlog = logOptionValues(optiontab);
		printLog(stdout, "%s", optionlog);
		free(optionlog);
	}
	printf("\t");
	if (options.debug && logfile) {
		va_start(arglist, fmt);
		vlog(fmt, arglist);
		va_end(arglist);
	}
	va_start(arglist, fmt);
	vfprintf(stdout, fmt, arglist);
	va_end(arglist);
	printLog(stdout, "\n");
	fflush(stdout);	/* in case writing memory contents causes core dump */
	if (mp && mp->memory && mp->size) {
		static int bugNum = 0;
		char tmp[40], *bugname;
		FILE *fp;

		sprintf(tmp, ".%d.%d.bug.html", (int)getpid(), ++bugNum);
		bugname = myStrdup2(getProgname(), tmp);
		if ((fp = fopen(bugname, "w"))) {
			fwrite(mp->memory, 1, mp->size, fp);
			fclose(fp);
			printLog(stdout, "then upload and attach %s ", bugname);
		} else
			printLog(stdout, "\tFailed to create bug file %s: %s\n", bugname, strerror(errno));
		free(bugname);
	} else {
		printLog(stdout, "\tPage content not available.");
	}
	printLog(stdout, "and click submit.\n");
	fflush(stdout);
}

/*
 * log a single character
 */
void
logChar(int c)
{
	if (!logfile)
		return;

	if (c == EOF)
		fflush(logfile);
	else
		putc(c, logfile);
}

/* read from file until you see the given character. */
char *
getUntil(memBuf_t *mp, int until)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int c;

	log(("\n\ngetUntil('%c')\n\n", until));

	while ((c = memGetc(mp)) != EOF) {
		if (options.debug)
			logChar(c);
		if ((char)c == until) {
			term(buf, bufsize, count);
			if (options.debug)
				logChar(EOF);
			return buf;
		}
		addchar(buf, bufsize, count, (char)c);
	}
	if (options.debug)
		logChar(EOF);
	return NULL;
}

/*
 * Return a valid string, even if it is null
 */
const char *
nullStr(const char *s)
{
	return s ? s : "(null)";
}

/*
 * Return a valid string, even if it is null
 */
const char *
nullEmptyStr(const char *s)
{
	return s ? s : "";
}

/*
 * Current date/time
 */
char *
timestamp()
{
	static char buf[80];	/* much larger than needed */
	static time_t saveTime = 0;
	time_t t = time(0);

	if (t != saveTime) {
		struct tm *tmp = localtime(&t);

		strftime(buf, (size_t)sizeof(buf), "%c", tmp);
		saveTime = t;
	}
	return buf;
}

/*
 * skip rest of line, up to newline.  Useful for handling comments.
 */
int
skipline(FILE *fp)
{
	int c;

	for (c = getc(fp); c != EOF && c != '\n' && c != '\r'; c = getc(fp))
		;
	return c;
}

/*
 * Prompt, with or without echo.  Returns malloc()'ed buffer containing
 * response.
 */
char *
prompt(const char *p, int noecho)
{
	char *buf = NULL;
	size_t size = 0, count = 0;
	int c;
#if defined(WIN32)
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	DWORD save, tmp;

	if (in == INVALID_HANDLE_VALUE || GetFileType (in) != FILE_TYPE_CHAR)
		noecho = 0;
#else
	struct termios save, tmp;
#endif

	if (!isatty(fileno(stdin))) {
		printLog(stderr, "Cannot prompt, stdin is not a terminal\n");
		return NULL;
	}

	fputs(p, stdout);

	if (noecho) {	/* echo off */
#if defined(WIN32)
		GetConsoleMode(in, &save);
		tmp = save & (~ENABLE_ECHO_INPUT);
		SetConsoleMode(in, tmp);
#else
		/*
		 * echo=ECHO is a silly hack to work around poorly defined constant
		 * that trips up a poorly implemented warning.  tcflag_t is unsigned,
		 * ECHO is signed.  gcc warns if you mix signed and unsigned in an
		 * expression, but does not warn if you mix in an assignment with a
		 * constant (assuming constant fits in the type of the variable).
		 */
		tcflag_t echo = ECHO;

		tcgetattr(fileno(stdin), &save);
		memcpy(&tmp, &save, sizeof(struct termios));
		/* you'll get the warning here if you change ~echo to ~ECHO */
		tmp.c_lflag &= ~echo;
		tcsetattr(fileno(stdin), TCSANOW, &tmp);
#endif
	}

	/* read value */
	for (c = getc(stdin); c != EOF && c != '\n' && c != '\r'; c = getc(stdin))
		addcharinc(buf, size, count, (char)c, (size_t)20);
	terminc(buf, size, count, (size_t)20);

	if (noecho) {	/* echo on */
#if defined(WIN32)
		SetConsoleMode(in, save);
#else
		tcsetattr(fileno(stdin), TCSANOW, &save);
#endif
		putchar('\n');
	}

	return buf;
}

/*
 * Converts string to boolean.
 * returns 0 (false), 1 (true), or -1 (invalid).  NULL is true.
 */
int
boolValue(const char *value)
{
	static const char* boolvalues[] = {
		"0", "1",
		"n", "y",
		"no", "yes",
		"off", "on",
		"false", "true",
		"disabled", "enabled",
		NULL
	};
	int i;
	char *buf;

	if (!value)
		return 1;

	buf = myStrdup(value);
	toLowerString(buf);
	for (i = 0; boolvalues[i]; i++) {
		if (!strcmp(buf, boolvalues[i]))
			break;
	}
	free(buf);
	return boolvalues[i] ? i % 2 : -1;
}

/*
 * Fixup a price to something that atof() and eBay will accept.
 * Final string must be in the form 1234.56.  Strip off all non-numeric
 * characters, convert ',' (non-english decimal) to '.'.
 */
char *
priceFixup(char *price, auctionInfo *aip)
{
	size_t len, i, j, start = 0, end, count = 0;

	if (!price)
		return price;
	len = strlen(price);
	if (aip && !aip->currency) {
		char tmp;

		for (; start < len && isalpha((int)price[start]); ++start)
			;
		tmp = price[start];
		price[start] = '\0';
		aip->currency = myStrdup(price);
		price[start] = tmp;
	}
	for (; start < len && !isdigit((int)price[start]) && price[start] != ',' && price[start] != '.'; ++start)
		;
	for (i = start; i < len; ++i) {
		if (isdigit((int)price[i]))
			continue;
		else if (price[i] == ',' || price[i] == '.')
			++count;
		else
			break;
	}
	end = i;

	for (j = 0, i = start; i < end; ++i) {
		if (price[i] == ',' || price[i] == '.') {
			if (--count == 0)
				price[j++] = '.';
		} else
			price[j++] = price[i];
	}
	price[j] = '\0';
	return price;
}

static void
toLowerString(char *s)
{
	for (; *s; ++s)
		*s = (char)tolower((int)*s);
}

/*
 * Password encrpytion/decryption.  xor with pseudo-random one-time pad,
 * so password isn't (usually) obvious in memory dump.
 */
static char *passwordPad = NULL;
static size_t passwordLen = 0;
static int needSeed = 1;

static void
seedPasswordRandom(void)
{
	if (needSeed) {
#if defined(WIN32)
		srand(time(0));
#else
		srandom((unsigned int)(getpid() * time(0)));
#endif
		needSeed = 0;
	}
}

/* create a malloc'ed string filled with '*' (to cover username/password
 * in logs)
 */
char *
stars(size_t len)
{
	char *s = (char *)myMalloc(len + 1);

	memset(s, '*', len);
	s[len] = '\0';
	return s;
}

void
setUsername(char *username)
{
	toLowerString(username);
	free(options.username);
	options.username = username;
	curl_free(options.usernameEscape);
	options.usernameEscape = curl_escape(username, (int)strlen(username));
}

void
setPassword(char *password)
{
	size_t i, len;
	char *escapedPassword;

	/* http escape password, clear original */
	len = strlen(password);
	escapedPassword = curl_escape(password, (int)len);
	for (i = 0; i < len; ++i)
		password[i] = '\0';
	free(password);
	password = escapedPassword;

	/* crypt with one-time pad */
	seedPasswordRandom();
	free(passwordPad);
	passwordLen = strlen(password) + 1;
	passwordPad = (char *)myMalloc(passwordLen);
	for (i = 0; i < (int)passwordLen; ++i)
#if defined(WIN32)
		passwordPad[i] = (char)rand();
#else
		passwordPad[i] = (char)random();
#endif
	cryptPassword(password);
	curl_free(options.password);
	options.password = password;
}

static void
cryptPassword(char *password)
{
	int i;

	for (i = 0; i < (int)passwordLen; ++i)
		password[i] ^= passwordPad[i];
}

char *
getPassword()
{
	char *password = (char *)myMalloc(passwordLen);

	memcpy(password, options.password, passwordLen);
	cryptPassword(password);
	return password;
}

void
freePassword(char *password)
{
	memset(password, '\0', passwordLen);
	free(password);
}

/*
 * Cygwin doesn't provide basename and dirname?
 *
 * Windows-specific code wrapped in ifdefs below, just in case basename
 * or dirname is needed on a non-windows system.
 */
#if defined(__CYGWIN__) || defined(WIN32)
char *
basename(char *name)
{
	int len;
	char *cp;

	if (!name)
		return name;

	len = strlen(name);
	if (len == 0)
		return (char *)".";	/* cast away const */
	cp = name + len - 1;
#if defined(__CYGWIN__) || defined(WIN32)
	if (*cp == '/' || *cp == '\\') {
		for (; cp >= name && (*cp == '/' || *cp == '\\'); --cp)
#else
	if (*cp == '/') {
		for (; cp >= name && *cp == '/'; --cp)
#endif
			*cp = '\0';
		if (cp < name)
			return (char *)"/";	/* cast away const */
	}
#if defined(__CYGWIN__) || defined(WIN32)
	for (; cp >= name && *cp != '/' && *cp != '\\'; --cp)
#else
	for (; cp >= name && *cp != '/'; --cp)
#endif
		;
	return cp + 1;
}

char *
dirname(char *name)
{
	int len;
	char *cp;

	if (!name) return name;

	len = strlen(name);
	if (len == 0)
		return (char *)".";	/* cast away const */
	cp = name + len - 1;
#if defined(__CYGWIN__) || defined(WIN32)
	if (*cp == '/' || *cp == '\\') {
		for (; cp >= name && (*cp == '/' || *cp == '\\'); --cp)
#else
	if (*cp == '/') {
		for (; cp >= name && *cp == '/'; --cp)
#endif
			*cp = '\0';
		if (cp <= name)
			return (char *)"/";	/* cast away const */
	}
#if defined(__CYGWIN__) || defined(WIN32)
	for (; cp >= name && *cp != '/' && *cp != '\\'; --cp)
#else
	for (; cp >= name && *cp != '/'; --cp)
#endif
		;
	if (cp < name)
		return (char *)".";	/* cast away const */
	if (cp == name)
		return (char *)"/";	/* cast away const */
	*cp = '\0';
	return name;
}
#endif

#if defined(WIN32)
/*
**  GETOPT PROGRAM AND LIBRARY ROUTINE
**
**  I wrote main() and AT&T wrote getopt() and we both put our efforts into
**  the public domain via mod.sources.
**	Rich $alz
**	Mirror Systems
**	(mirror!rs, rs@mirror.TMC.COM)
**	August 10, 1986
**
**  This is the public-domain AT&T getopt(3) code.  Hacked by Rich and by Jim.
*/

/*
 * Added to esniper to cover for Windows deficiency.  Unix versions of esniper
 * don't need to use this!
 */

#define ERR(_s, _c) { if (opterr) fprintf (stderr, "%s%s%c\n", argv[0], _s, _c);}

int	opterr = 1;
int	optind = 1;
int	optopt;
char	*optarg;

int
getopt(int argc, char *const *argv, const char *opts)
{
	static int sp = 1;
	register int c;
	register char *cp;

	if (sp == 1) {
		if (optind >= argc ||
		    argv[optind][0] != '-' || argv[optind][1] == '\0')
			return(EOF);
		else if (strcmp(argv[optind], "--") == 0) {
			optind++;
			return(EOF);
		}
	}
	optopt = c = argv[optind][sp];
	if (c == ':' || (cp=strchr(opts, c)) == NULL) {
		ERR(": illegal option -- ", c);
		if (argv[optind][++sp] == '\0') {
			optind++;
			sp = 1;
		}
		return('?');
	}
	if (*++cp == ':') {
		if (argv[optind][sp+1] != '\0')
			optarg = &argv[optind++][sp+1];
		else if (++optind >= argc) {
			ERR(": option requires an argument -- ", c);
			sp = 1;
			return('?');
		} else
			optarg = argv[optind++];
		sp = 1;
	} else {
		if (argv[optind][++sp] == '\0') {
			sp = 1;
			optind++;
		}
		optarg = NULL;
	}
	return(c);
}
#endif
