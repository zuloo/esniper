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

#include "esniper.h"
#include "auction.h"
#include "auctionfile.h"
#include "auctioninfo.h"
#include "options.h"
#include "util.h"

static const char *progname = NULL;
static const char blurb[] =
	"Please visit http://esniper.sf.net/ for updates and bug reports.  To learn\n"
	"about updates to and major bugs in esniper, subscribe to the esniper mailing\n"
	"list at http://lists.sf.net/lists/listinfo/esniper-announce";
static const char DEFAULT_CONF_FILE[] = ".esniper";

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(WIN32)
#	include <io.h>
#	define access(name, mode) _access((name), (mode))
#	define sleep(t)	_sleep((t) * 1000)
#	define R_OK 0x04
#else
#	include <unistd.h>
#endif

/* minimum bid time, in seconds before end of auction */
#define MIN_BIDTIME 5
/* default bid time */
#define DEFAULT_BIDTIME 10

#define DEFAULT_HISTORY_HOST "offer.ebay.com"
#define DEFAULT_PREBID_HOST "offer.ebay.com"
#define DEFAULT_BID_HOST "offer.ebay.com"
#define DEFAULT_LOGIN_HOST "signin.ebay.com"
#define DEFAULT_MYEBAY_HOST "my.ebay.com"

option_t options = {
	NULL,		/* username */
	NULL,		/* usernameEscape */
	NULL,		/* password */
	DEFAULT_BIDTIME,/* bidtime */
	1,		/* quantity */
	NULL,		/* configuration file */
	NULL,		/* auction file */
	1,		/* bid */
	1,		/* reduce quantity */
	0,		/* debug */
	0,		/* usage */
	0,		/* info on given auctions only */
	0,		/* get my eBay items */
	0,		/* batch */
	0,		/* password encrypted? */
	NULL,		/* proxy */
	NULL,		/* log directory */
	NULL,		/* historyHost */
	NULL,		/* prebidHost */
	NULL,		/* bidHost */
	NULL,		/* loginHost */
	NULL,		/* bidHost */
	0,		/* curldebug */
	2      /* delay */
};

/* used for option table */
static int CheckDebug(const void *valueptr, const optionTable_t *tableptr,
		      const char *filename, const char *line);
static int CheckSecs(const void *valueptr, const optionTable_t *tableptr,
		     const char *filename, const char *line);
static int CheckQuantity(const void *valueptr, const optionTable_t *tableptr,
			 const char *filename, const char *line);
static int ReadUser(const void *valueptr, const optionTable_t *tableptr,
		    const char *filename, const char *line);
static int ReadPass(const void *valueptr, const optionTable_t *tableptr,
		    const char *filename, const char *line);
static int CheckAuctionFile(const void *valueptr, const optionTable_t *tableptr,
			    const char *filename, const char *line);
static int CheckConfigFile(const void *valueptr, const optionTable_t *tableptr,
			   const char *filename, const char *line);
static int SetLongHelp(const void *valueptr, const optionTable_t *tableptr,
		       const char *filename, const char *line);
static int SetConfigHelp(const void *valueptr, const optionTable_t *tableptr,
			 const char *filename, const char *line);
static int CheckUser(const void *valueptr, const optionTable_t *tableptr,
	  const char *filename, const char *line);
static int CheckPass(const void *valueptr, const optionTable_t *tableptr,
	  const char *filename, const char *line);

/* this table describes options and config entries */
optionTable_t optiontab[] = {
   {"username", "u", (void*)&options.username,     OPTION_STRING,  LOG_CONFID, &CheckUser, 0},
   {"password",NULL, (void*)&options.password,     OPTION_SPECSTR, LOG_CONFID, &CheckPass, 0},
   {"seconds",  "s", (void*)&options.bidtime,      OPTION_SPECINT, LOG_NORMAL, &CheckSecs, 0},
   {"quantity", "q", (void*)&options.quantity,     OPTION_INT,     LOG_NORMAL, &CheckQuantity, 0},
   {"proxy",    "p", (void*)&options.proxy,        OPTION_STRING,  LOG_NORMAL, NULL, 0},
   {NULL,       "P", (void*)&options.password,     OPTION_STRING,  LOG_CONFID,   &ReadPass, 0},
   {NULL,       "U", (void*)&options.username,     OPTION_STRING,  LOG_NORMAL, &ReadUser, 0},
   {NULL,       "c", (void*)&options.conffilename, OPTION_STRING,  LOG_NORMAL, &CheckConfigFile, 0},
   /*
    * -f can't be entered from command line, it's just a convenient way
    * to integrate auction filename processing with option processing.
    */
   {NULL,       "f", (void*)&options.auctfilename, OPTION_STRING,  LOG_NORMAL, &CheckAuctionFile, 0},
   {"reduce",  NULL, (void*)&options.reduce,       OPTION_BOOL,    LOG_NORMAL, NULL, 0},
   {NULL,       "r", (void*)&options.reduce,       OPTION_BOOL_NEG,LOG_NORMAL, NULL, 0},
   {"bid",     NULL, (void*)&options.bid,          OPTION_BOOL,    LOG_NORMAL, NULL, 0},
   {NULL,       "n", (void*)&options.bid,          OPTION_BOOL_NEG,LOG_NORMAL, NULL, 0},
   {NULL,       "m", (void*)&options.myitems,      OPTION_BOOL,    LOG_NORMAL, NULL, 0},
   {NULL,       "i", (void*)&options.info,         OPTION_BOOL,    LOG_NORMAL, NULL, 0},
   {"debug",    "d", (void*)&options.debug,        OPTION_BOOL,    LOG_NORMAL, CheckDebug, 0},
   {"curldebug","C", (void*)&options.curldebug,    OPTION_BOOL,    LOG_NORMAL, NULL, 0},
   {"batch",    "b", (void*)&options.batch,        OPTION_BOOL,    LOG_NORMAL, NULL, 0},
   {"logdir",   "l", (void*)&options.logdir,       OPTION_STRING,  LOG_NORMAL, NULL, 0},
   {"historyHost",NULL,(void*)&options.historyHost,OPTION_STRING,  LOG_NORMAL, NULL, 0},
   {"prebidHost",NULL,(void*)&options.prebidHost,  OPTION_STRING,  LOG_NORMAL, NULL, 0},
   {"bidHost", NULL, (void*)&options.bidHost,      OPTION_STRING,  LOG_NORMAL, NULL, 0},
   {"loginHost",NULL,(void*)&options.loginHost,    OPTION_STRING,  LOG_NORMAL, NULL, 0},
   {"myeBayHost",NULL,(void*)&options.myeBayHost,  OPTION_STRING,  LOG_NORMAL, NULL, 0},
   {"delay",    "D", (void*)&options.delay,        OPTION_INT,     LOG_NORMAL, NULL, 0},
   {NULL,       "?", (void*)&options.usage,        OPTION_BOOL,    LOG_NORMAL, NULL, 0},
   {NULL,       "h", (void*)&options.usage,        OPTION_BOOL,    LOG_NORMAL, SetLongHelp, 0},
   {NULL,       "H", (void*)&options.usage,        OPTION_BOOL,    LOG_NORMAL, SetConfigHelp, 0},
   {NULL, NULL, NULL, 0, 0, NULL, 0}
};

/* support functions */
#if !defined(WIN32)
static void sigAlarm(int sig);
#endif
static void sigTerm(int sig);
static void cleanup(void);
static int usage(int helptype);
static void printRemain(int remain);
static void printVersion(void);
#define USAGE_SUMMARY	0x01
#define USAGE_LONG	0x02
#define USAGE_CONFIG	0x04
int main(int argc, char *argv[]);

/* called by CheckAuctionFile, CheckConfigFile */
static int CheckFile(const void *valueptr, const optionTable_t *tableptr,
		     const char *filename, const char *line,
		     const char *fileType);


const char *
getVersion(void)
{
	return VERSION;
}

const char *
getProgname(void)
{
	return progname ? progname : "esniper";
}

#if !defined(WIN32)
static void
sigAlarm(int sig)
{
	signal(sig, sigAlarm);
	log((" SIGALRM"));
}
#endif

static void
sigTerm(int sig)
{
	signal(sig, SIG_DFL);
	log(("SIGTERM...\n"));
	raise(sig);
}

/* cleanup open files */
static void
cleanup()
{
	logClose();
}

/* specific check functions would reside in main module */

/*
 * CheckDebug(): convert boolean value, open of close log file
 *
 * returns: 0 = OK, else error
 */
static int
CheckDebug(const void *valueptr, const optionTable_t *tableptr,
	   const char *filename, const char *line)
{
	int val = *((const int*)valueptr);

	val ? logOpen(NULL, options.logdir) : logClose();
	*(int*)(tableptr->value) = val;
	log(("Debug mode is %s\n", val ? "on" : "off"));
	return 0;
}

/*
 * CheckSecs(): convert integer value or "now", check minimum value
 *
 * returns: 0 = OK, else error
 */
static int
CheckSecs(const void *valueptr, const optionTable_t *tableptr,
	  const char *filename, const char *line)
{
	int intval;
	char *endptr;

	/* value specified? */
	if (!valueptr) {
		if (filename)
			printLog(stderr, "Configuration option \"%s\" in file %s needs an integer value or \"now\"\n", line, filename);
		else
			printLog(stderr, "Option -%s needs an integer value or \"now\"\n", line);
	}
	/* specific string value "now" */
	if (!strcmp((const char *)valueptr, "now")) {
		/* copy value to target option */
		*(int *)(tableptr->value)=0;
		log(("seconds value is %d (now)", *(int *)(tableptr->value)));
		return 0;
	}

	/* else must be integer value */
	intval = (int)strtol((const char*)valueptr, &endptr, 10);
	if (*endptr != '\0') {
		if (filename)
			printLog(stderr, "Configuration option \"%s\" in file %s", line, filename);
		else
			printLog(stderr, "Option -%s", line);
		printLog(stderr, "accepts integer values greater than %d or \"now\"\n", MIN_BIDTIME - 1);
		return 1;
	}
	/* check minimum */
	if (intval < MIN_BIDTIME) {
		if (filename)
			printLog(stderr, "Value at configuration option \"%s\" in file %s", line, filename);
		else
			printLog(stderr, "Value %d at option -%s", intval, line);
		printLog(stderr, " too small, using minimum value of %d seconds\n", MIN_BIDTIME);
		intval = MIN_BIDTIME;
	}

	/* copy value to target option */
	*(int *)(tableptr->value) = intval;
	log(("seconds value is %d\n", *(const int *)(tableptr->value)));
	return 0;
}

/*
 * CheckPass(): set password
 *
 * returns: 0 = OK, else error
 */
static int
CheckPass(const void *valueptr, const optionTable_t *tableptr,
	  const char *filename, const char *line)
{
	if (!valueptr) {
		if (filename)
			printLog(stderr,
				 "Invalid password at \"%s\" in file %s\n",
				 line, filename);
		else
			printLog(stderr, "Invalid password at option -%s\n",
				 line);
		return 1;
	}
	setPassword(myStrdup((const char *)valueptr));
	log(("password has been set\n"));
	return 0;
}

/*
 * CheckQuantity(): convert integer value, check for positive value
 *
 * returns: 0 = OK, else error
 */
static int
CheckQuantity(const void *valueptr, const optionTable_t *tableptr,
	      const char *filename, const char *line)
{
	if (*(const int*)valueptr <= 0) {
		if (filename)
			printLog(stderr, "Quantity must be positive at \"%s\" in file %s\n", line, filename);
		else
			printLog(stderr,
				 "Quantity must be positive at option -%s\n",
				 line);
		return 1;
	}
	/* copy value to target option */
	*(int *)(tableptr->value) = *(const int *)valueptr;
	log(("quantity is %d\n", *(const int *)(tableptr->value)));
	return 0;
}

/*
 * CheckUser(): set user
 *
 * returns: 0 = OK, else error
 */
static int
CheckUser(const void *valueptr, const optionTable_t *tableptr,
	  const char *filename, const char *line)
{
	if (!valueptr) {
		if (filename)
			printLog(stderr, "Invalid user at \"%s\" in file %s\n",
				 line, filename);
		else
			printLog(stderr, "Invalid user at option -%s\n", line);
		return 1;
	}
	setUsername(myStrdup((const char *)valueptr));
	log(("user has been set\n"));
	return 0;
}

/*
 * ReadUser(): read username from console
 *
 * note: not called by option processing code.  Called directly from main()
 *	if esniper has not been given username.
 *
 * returns: 0 = OK, else error
 */
static int
ReadUser(const void *valueptr, const optionTable_t *tableptr,
	 const char *filename, const char *line)
{
	char *username = prompt("Enter eBay username: ", 0);

	if (!username) {
		printLog(stderr, "Username entry failed!\n");
		return 1;
	}

	setUsername(myStrdup(username));
	log(("username is %s\n", *(char **)(tableptr->value)));
	return 0;
}

/*
 * ReadPass(): read password from console
 *
 * returns: 0 = OK, else error
 */
static int
ReadPass(const void *valueptr, const optionTable_t *tableptr,
	 const char *filename, const char *line)
{
	char *passwd = prompt("Enter eBay password: ", 1);

	if (!passwd) {
		printLog(stderr, "Password entry failed!\n");
		return 1;
	}
	putchar('\n');

	setPassword(passwd);
	/* don't log password! */
	return 0;
}

/*
 * CheckAuctionFile(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckAuctionFile(const void *valueptr, const optionTable_t *tableptr,
			    const char *filename, const char *line)
{
	return CheckFile(valueptr, tableptr, filename, line, "Auction");
}

/*
 * CheckConfigFile(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckConfigFile(const void *valueptr, const optionTable_t *tableptr,
			   const char *filename, const char *line)
{
	return CheckFile(valueptr, tableptr, filename, line, "Config");
}

/*
 * CheckFile(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckFile(const void *valueptr, const optionTable_t *tableptr,
		     const char *filename, const char *line,
		     const char *filetype)
{
	if (access((const char*)valueptr, R_OK)) {
		printLog(stderr, "%s file \"%s\" is not readable: %s\n",
			 filetype, nullStr((const char*)valueptr),
			 strerror(errno));
		return 1;
	}
	free(*(char **)(tableptr->value));
	*(char **)(tableptr->value) = myStrdup(valueptr);
	return 0;
}

/*
 * SetLongHelp(): set usage to 2 to activate long help
 *
 * returns: 0 = OK
 */
static int SetLongHelp(const void *valueptr, const optionTable_t *tableptr,
		       const char *filename, const char *line)
{
	/* copy value to target option */
	*(int *)(tableptr->value) |= USAGE_SUMMARY | USAGE_LONG;
	return 0;
}

/*
 * SetConfigHelp(): set usage to 3 to activate config file help
 *
 * returns: 0 = OK
 */
static int SetConfigHelp(const void *valueptr, const optionTable_t *tableptr,
			 const char *filename, const char *line)
{
	/* copy value to target option */
	*(int *)(tableptr->value) = USAGE_CONFIG;
	return 0;
}

/*
 * Print number of auctions remaining.
 */
static void
printRemain(int remain)
{
	printLog(stdout, "\nNeed to win %d item(s), %d auction(s) remain\n\n",
		options.quantity, remain);
}

static void
printVersion(void)
{
	const char *newVersion;

	fprintf(stderr, "%s version %s\n", getProgname(), getVersion());
	if ((newVersion = checkVersion()))
		fprintf(stderr,
			"\n"
			"The newest version is %s, you should upgrade.\n"
			"Get it from http://esniper.sf.net/\n",
			newVersion);
	fprintf(stderr, "\n%s\n", blurb);
}

static const char usageSummary[] =
 "usage: %s [-bdhHnmPrUv] [-c conf_file] [-l logdir] [-p proxy] [-q quantity]\n"
 "       [-s secs|now] [-u user] [-D delay] (auction_file | [auction price ...])\n"
 "\n";

/* split in two to prevent gcc portability warning.  maximum length is 509 */
static const char usageLong1[] =
 "where:\n"
 "-b: batch mode, don't prompt for password or username if not specified\n"
#if defined(WIN32)
 "-c: configuration file (default is \"My Documents/.esniper\" and, if auction\n"
#else
 "-c: configuration file (default is \"$HOME/.esniper\" and, if auction\n"
#endif
 "    file is specified, .esniper in auction file's directory)\n"
 "-d: write debug output to file\n"
 "-D: delay in seconds when retrieving auction list (default 2 seconds)\n"
 "-h: command line options help\n"
 "-H: configuration and auction file help\n"
 "-i: get info on auctions and exit\n"
 "-l: log directory (default: ., or directory of auction file, if specified)\n"
 "-m: get my ebay watched items and exit\n"
 "-n: do not place bid\n";
static const char usageLong2[] =
 "-p: http proxy (default: http_proxy environment variable, format is\n"
 "    http://host:port/)\n"
 "-P: prompt for password\n"
 "-q: quantity to buy (default is 1)\n"
 "-r: do not reduce quantity on startup if already won item(s)\n"
 "-s: time to place bid which may be \"now\" or seconds before end of auction\n"
 "    (default is %d seconds before end of auction)\n"
 "-u: ebay username\n"
 "-U: prompt for ebay username\n";
static const char usageLong3[] =
 "-v: print version and exit\n"
 "\n"
 "You must specify an auction file or <auction> <price> pair[s].  Options\n"
 "on the command line override settings in auction and configuration files.\n";

/* split in two to prevent gcc portability warning.  maximum length is 509 */
static const char usageConfig1[] =
 "Configuration options (values shown are default):\n"
 "  Boolean: (valid values: true,y,yes,on,1,enabled  false,n,no,off,0,disabled)\n"
 "    batch = false\n"
 "    bid = true\n"
 "    debug = false\n"
 "    reduce = true\n"
 "  String:\n"
 "    logdir = .\n"
 "    password =\n"
 "    proxy = <http_proxy environment variable, format is http://host:port/>\n"
 "    username =\n"
 "    historyHost = %s\n"
 "    prebidHost = %s\n"
 "    bidHost = %s\n"
 "    loginHost = %s\n"
 "    myeBayHost = %s\n"
 "  Numeric: (seconds may also be \"now\")\n"
 "    delay = 2\n"
 "    quantity = 1\n"
 "    seconds = %d\n"
 "\n";
static const char usageConfig2[] =
 "A configuration file consists of option settings, blank lines, and comment\n"
 "lines.  Comment lines begin with #\n"
 "\n"
 "An auction file is similar to a configuration file, but it also has one or\n"
 "more auction lines.  An auction line contains an auction number, optionally\n"
 "followed by a bid price.  If no bid price is given, the auction number uses\n"
 "the bid price of the first prior auction line that contains a bid price.\n";

static int
usage(int helplevel)
{
	if (helplevel & USAGE_SUMMARY)
		fprintf(stderr, usageSummary, getProgname());
	if (helplevel & USAGE_LONG) {
		fprintf(stderr, usageLong1);
		fprintf(stderr, usageLong2, DEFAULT_BIDTIME);
		fprintf(stderr, usageLong3);
	}
	if (helplevel & USAGE_CONFIG) {
		fprintf(stderr, usageConfig1, options.historyHost, options.prebidHost, options.bidHost, options.loginHost, options.myeBayHost, DEFAULT_BIDTIME);
		fprintf(stderr, usageConfig2);
	}
	if (helplevel == USAGE_SUMMARY)
		fprintf(stderr, "Try \"%s -h\" for more help.\n", getProgname());
	fprintf(stderr,"\n%s\n", blurb);
	return 1;
}

int
main(int argc, char *argv[])
{
	int won = 0;	/* number of items won */
	auctionInfo **auctions = NULL;
	int c, i, numAuctions = 0, numAuctionsOrig = 0;
	int XFlag = 0;

	/* all known options */
	static const char optionstring[]="bc:dhHil:mnp:Pq:rs:u:UvX";

	atexit(cleanup);
	progname = basename(argv[0]);

	/* some defaults... */
	options.historyHost = myStrdup(DEFAULT_HISTORY_HOST);
	options.prebidHost = myStrdup(DEFAULT_PREBID_HOST);
	options.bidHost = myStrdup(DEFAULT_BID_HOST);
	options.loginHost = myStrdup(DEFAULT_LOGIN_HOST);
	options.myeBayHost = myStrdup(DEFAULT_MYEBAY_HOST);

	/* first, check for debug, configuration file and auction file
	 * options but accept all other options to avoid error messages
	 */
	while ((c = getopt(argc, argv, optionstring)) != EOF) {
		switch (c) {
			/* Debug is in both getopt() sections, because we want
			 * debugging as soon as possible, and also because
			 * command line -d overrides settings in config files
			 */
		case 'd': /* debug */
		case 'h': /* command-line options help */
		case 'H': /* configuration and auction file help */
		case 'i': /* info only */
		case 'm': /* get my ebay items */
		case '?': /* unknown -> help */
			if (parseGetoptValue(c, NULL, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
		case 'c': /* configuration file */
		case 'l': /* log directory */
			if (parseGetoptValue(c, optarg, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
		case 'X': /* secret option - for testing page parsing */
			++XFlag;
			break;
		case 'v': /* version */
			printVersion();
			exit(0);
			break;
		default:
			/* ignore other options, these will be parsed
			 * after configuration and auction files.
			 */
			break;
		}
	}

	if (options.usage)
		exit(usage(options.usage));

	/* One argument after options?  Must be an auction file. */
	if ((argc - optind) == 1) {
		if (parseGetoptValue('f', argv[optind], optiontab)) {
			options.usage |= USAGE_SUMMARY;
			exit(usage(options.usage));
		}
	}

	/*
	 * if configuration file is specified don't try to load any other
	 * configuration file (i.e. $HOME/.esniper, etc).
	 */
	if (options.conffilename) {
		if (readConfigFile(options.conffilename, optiontab) > 1)
			options.usage |= USAGE_SUMMARY;
	} else {
		/* TODO: on UNIX use getpwuid() to find the home dir? */
		char *homedir = getenv("HOME");
#if defined(WIN32)
		char *profiledir = getenv("USERPROFILE");

		if (profiledir && *profiledir) {
			/* parse $USERPROFILE/My Documents/.esniper */
			char *cfname = myStrdup3(profiledir,
						 "\\My Documents\\",
						 DEFAULT_CONF_FILE);

			switch (readConfigFile(cfname, optiontab)) {
			case 1: /* file not found */
				if (homedir && *homedir) {
					/* parse $HOME/.esniper */
					free(cfname);
					cfname = myStrdup3(homedir, "/",
							   DEFAULT_CONF_FILE);
					if (readConfigFile(cfname, optiontab) > 1)
						options.usage |= USAGE_SUMMARY;
				}
				break;
			case 0: /* OK */
				break;
			default: /* other error */
				options.usage |= USAGE_SUMMARY;
			}
			free(cfname);
		} else
			printLog(stderr, "Warning: environment variable USERPROFILE not set. Cannot parse $USERPROFILE/My Documents/%s.\n", DEFAULT_CONF_FILE);
#else
		if (homedir && *homedir) {
			/* parse $HOME/.esniper */
			char *cfname = myStrdup3(homedir,"/",DEFAULT_CONF_FILE);
			if (readConfigFile(cfname, optiontab) > 1)
				options.usage |= USAGE_SUMMARY;
			free(cfname);
		} else
			printLog(stderr, "Warning: environment variable HOME not set. Cannot parse $HOME/%s.\n", DEFAULT_CONF_FILE);
#endif

		if (options.auctfilename) {
			/* parse .esniper in auction file's directory */
			char *auctfilename = myStrdup(options.auctfilename);
			char *cfname = myStrdup3(dirname(auctfilename), "/", DEFAULT_CONF_FILE);

			if (readConfigFile(cfname, optiontab) > 1)
				options.usage |= USAGE_SUMMARY;
			free(auctfilename);
			free(cfname);
		}
	}

	/* parse auction file */
	if (options.auctfilename) {
		if (!options.logdir) {
			char *tmp = myStrdup(options.auctfilename);

			options.logdir = myStrdup(dirname(tmp));
			free(tmp);
		}
		if (readConfigFile(options.auctfilename, optiontab) > 1)
			options.usage |= USAGE_SUMMARY;
	}

	/* skip back to first arg */
	optind = 1;
	/*
	 * check options which may overwrite settings from configuration
	 * or auction file.
	 */
	while ((c = getopt(argc, argv, optionstring)) != EOF) {
		switch (c) {
		case 'l': /* log directory */
		case 'p': /* proxy */
		case 'q': /* quantity */
		case 's': /* seconds */
		case 'u': /* user */
			if (parseGetoptValue(c, optarg, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
			/* Debug is in both getopt() sections, because we want
			 * debugging as soon as possible, and also because
			 * command line -d overrides settings in config files
			 */
		case 'd': /* debug */
			if (options.debug)
				break;
			/* fall through */
		case 'b': /* batch */
		case 'n': /* don't bid */
		case 'P': /* read password */
		case 'r': /* reduce */
		case 'U': /* read username */
			if (parseGetoptValue(c, NULL, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
		default:
			/* ignore other options, these have been parsed
			 * before configuration and auction files.
			 */
			break;
		}
	}

	argc -= optind;
	argv += optind;

	/* don't log username/password */
	/*log(("options.username=%s\n", nullStr(options.username)));*/
	/*log(("options.password=%s\n", nullStr(options.password)));*/
	log(("options.bidtime=%d\n", options.bidtime));
	log(("options.quantity=%d\n", options.quantity));
	log(("options.conffilename=%s\n", nullStr(options.conffilename)));
	log(("options.auctfilename=%s\n", nullStr(options.auctfilename)));
	log(("options.bid=%d\n", options.bid));
	log(("options.reduce=%d\n", options.reduce));
	log(("options.debug=%d\n", options.debug));
	log(("options.usage=%d\n", options.usage));
	log(("options.info=%d\n", options.info));
	log(("options.myitems=%d\n", options.myitems));

	if (!options.usage) {
		if (!XFlag) {
			if (options.auctfilename) {
				/* should never happen */
				if (argc != 1) {
					printLog(stderr, "Error: arguments specified after auction filename.\n");
					options.usage |= USAGE_SUMMARY;
				}
			} else if (options.myitems) {
				if (argc != 0) {
					printLog(stderr, "Error: auctions specified with -m option.\n");
					options.usage |= USAGE_SUMMARY;
				}
			} else if (argc < 2) {
				printLog(stderr, "Error: no auctions specified.\n");
				options.usage |= USAGE_SUMMARY;
			} else if (argc % 2) {
				printLog(stderr, "Error: auctions and prices must be specified in pairs.\n");
				options.usage |= USAGE_SUMMARY;
			}
		}
		if (!options.username) {
			if (options.batch) {
				printLog(stderr, "Error: no username specified.\n");
				options.usage |= USAGE_SUMMARY;
			} else if (!options.usage &&
				   parseGetoptValue('U', NULL, optiontab)) {
					options.usage |= USAGE_SUMMARY;
			}
		}
		if (!options.password) {
			if (options.batch) {
				printLog(stderr, "Error: no password specified.\n");
				options.usage |= USAGE_SUMMARY;
			} else if (!options.usage &&
				   parseGetoptValue('P', NULL, optiontab)) {
					options.usage |= USAGE_SUMMARY;
			}
		}
	}

	if (XFlag) {
		testParser(XFlag);
		exit(0);
	}

	if (options.usage)
		exit(usage(options.usage));

	/* init variables */
	if (options.auctfilename) {
		numAuctions = readAuctionFile(options.auctfilename, &auctions);
	} else {
		numAuctions = argc / 2;
		auctions = (auctionInfo **)myMalloc((size_t)numAuctions * sizeof(auctionInfo *));
		for (i = 0; i < argc/2; i++)
			auctions[i] = newAuctionInfo(argv[2*i], argv[2*i+1]);
	}

 	if (options.myitems)
		exit(printMyItems());
	if (numAuctions <= 0)
		exit(usage(USAGE_SUMMARY));

#if !defined(WIN32)
	signal(SIGALRM, sigAlarm);
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
#endif
	signal(SIGTERM, sigTerm);

	numAuctionsOrig = numAuctions;
	{
		int quantity = options.quantity;
		numAuctions = sortAuctions(auctions, numAuctions, &quantity);

		if (quantity < options.quantity) {
			printLog(stdout, "\nYou have already won %d item(s).\n",
				 options.quantity - quantity);
			if (options.reduce) {
				options.quantity = quantity;
				printLog(stdout,
					 "Quantity reduced to %d item(s).\n",
					 options.quantity);
			}
		}
	}

	if (options.info) {
		if (numAuctionsOrig > 1)
			printRemain(numAuctions);
		exit(0);
	}

	for (i = 0; i < numAuctions && options.quantity > 0; ++i) {
		if (numAuctionsOrig > 1)
			printRemain(numAuctions - i);
		won += snipeAuction(auctions[i]);
	}
	for (i = 0; i < numAuctions && options.quantity > 0; ++i)
		freeAuction(auctions[i]);
	free(auctions);

	cleanupCurlStuff();

	return won > 0 ? 0 : 1;
}
