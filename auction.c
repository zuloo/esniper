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

/* for strcasestr  prototype in string.h */
#define _GNU_SOURCE

#include "auction.h"
#include "buffer.h"
#include "http.h"
#include "html.h"
#include "history.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#if defined(WIN32)
#	define strcasecmp(s1, s2) stricmp((s1), (s2))
#	define sleep(t) _sleep((t) * 1000)
#	define strncasecmp(s1, s2, len) strnicmp((s1), (s2), (len))
#else
#	include <unistd.h>
#endif

#define newRemain(aip) (aip->endTime - time(NULL) - aip->latency - options.bidtime)

static time_t loginTime = 0;	/* Time of last login */
static time_t defaultLoginInterval = 12 * 60 * 60;	/* ebay login interval */

static int acceptBid(const char *pagename, auctionInfo *aip);
static int bid(auctionInfo *aip);
static int ebayLogin(auctionInfo *aip, time_t interval);
static int forceEbayLogin(auctionInfo *aip);
static char *getIdInternal(char *s, size_t len);
static int getInfoTiming(auctionInfo *aip, time_t *timeToFirstByte);
static int getQuantity(int want, int available);
static int makeBidError(const pageInfo_t *pageInfo, auctionInfo *aip);
static int match(memBuf_t *mp, const char *str);
static int parseBid(memBuf_t *mp, auctionInfo *aip);
static int preBid(auctionInfo *aip);
static int parsePreBid(memBuf_t *mp, auctionInfo *aip);
static int printMyItemsRow(char **row, int printNewline);
static int watch(auctionInfo *aip);

/*
 * attempt to match some input, neglecting case, ignoring \r and \n.
 * returns 0 on success, -1 on failure
 */
static int
match(memBuf_t *mp, const char *str)
{
	const char *cursor;
	int c;

	log(("\n\nmatch(\"%s\")\n\n", str));

	cursor = str;
	while ((c = memGetc(mp)) != EOF) {
		if (options.debug)
			logChar(c);
		if (tolower(c) == (int)*cursor) {
			if (*++cursor == '\0') {
				if (options.debug)
					logChar(EOF);
				return 0;
			}
		} else if (c != '\n' && c != '\r')
			cursor = str;
	}
	if (options.debug)
		logChar(EOF);
	return -1;
}

static const char PAGEID[] = "Page id: ";
static const char SRCID[] = "srcId: ";

/*
 * Get page info, including pagename variable, page id and srcid comments.
 */
pageInfo_t *
getPageInfo(memBuf_t *mp)
{
	const char *line;
	pageInfo_t p = {NULL, NULL, NULL}, *pp;
	int needPageName = 1;
	int needPageId = 1;
	int needSrcId = 1;
	int needMore = 3;
	char *title = NULL;

	log(("getPageInfo():\n"));
	memReset(mp);
	while (needMore && (line = getTag(mp))) {
		char *tmp;

		if (!strcasecmp(line, "title")) {
		    line = getNonTag(mp);
		    if (line) title = myStrdup(line);
		    continue;
		}
		if (strncmp(line, "!--", 3))
			continue;
		if (needPageName && (tmp = strstr(line, PAGENAME))) {
			if ((tmp = getPageNameInternal(tmp))) {
				--needMore;
				--needPageName;
				p.pageName = myStrdup(tmp);
			}
		} else if (needPageId && (tmp = strstr(line, PAGEID))) {
			if ((tmp = getIdInternal(tmp, sizeof(PAGEID)))) {
				--needMore;
				--needPageId;
				p.pageId = myStrdup(tmp);
			}
		} else if (needSrcId && (tmp = strstr(line, SRCID))) {
			if ((tmp = getIdInternal(tmp, sizeof(SRCID)))) {
				--needMore;
				--needSrcId;
				p.srcId = myStrdup(tmp);
			}
		}
	}
	if (needPageName && title) {
	   log(("using title as page name: %s", title));
	   p.pageName = title;
	   --needPageName;
	   --needMore;
	   title = NULL;
	}
	if (title) free(title);
	log(("getPageInfo(): pageName = %s, pageId = %s, srcId = %s\n", nullStr(p.pageName), nullStr(p.pageId), nullStr(p.srcId)));
	memReset(mp);
	if (needMore == 3)
		return NULL;
	pp = (pageInfo_t *)myMalloc(sizeof(pageInfo_t));
	pp->pageName = p.pageName;
	pp->pageId = p.pageId;
	pp->srcId = p.srcId;
	return pp;
}

static char *
getIdInternal(char *s, size_t len)
{
	char *id = s + len - 1;
	char *dash = strchr(id, '-');

	if (!*dash) {
		log(("getIdInternal(): Cannot find trailing dash: %s\n", id));
		return NULL;
	}
	*dash = '\0';
	log(("getIdInternal(): id = %s\n", id));
	return id;
}

/*
 * Free a pageInfo_t and it's internal members.
 */
void
freePageInfo(pageInfo_t *pp)
{
	if (pp) {
		free(pp->pageName);
		free(pp->pageId);
		free(pp->srcId);
		free(pp);
	}
}

/*
 * Calculate quantity to bid on.  If it is a dutch auction, never
 * bid on more than 1 less item than what is available.
 */
static int
getQuantity(int want, int available)
{
	if (want == 1 || available == 1)
		return 1;
	if (available > want)
		return want;
	return available - 1;
}

static const char HISTORY_URL[] = "http://%s/ws/eBayISAPI.dll?ViewBids&item=%s";

/*
 * getInfo(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
int
getInfo(auctionInfo *aip)
{
	return getInfoTiming(aip, NULL);
}

/*
 * getInfoTiming(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
static int
getInfoTiming(auctionInfo *aip, time_t *timeToFirstByte)
{
	int i, ret;
	time_t start;

	log(("\n\n*** getInfo auction %s price %s user %s\n", aip->auction, aip->bidPriceStr, options.username));
	if (ebayLogin(aip, 0))
		return 1;

	for (i = 0; i < 3; ++i) {
		memBuf_t *mp = NULL;

		if (!aip->query) {
			size_t urlLen = sizeof(HISTORY_URL) + strlen(options.historyHost) + strlen(aip->auction) - (2*2);

			aip->query = (char *)myMalloc(urlLen);
			sprintf(aip->query, HISTORY_URL, options.historyHost, aip->auction);
		}
		start = time(NULL);
		if (!(mp = httpGet(aip->query, NULL))) {
			freeMembuf(mp);
			return httpError(aip);
		}
		ret = parseBidHistory(mp, aip, start, timeToFirstByte, 0);
		freeMembuf(mp);
		if (i == 0 && ret == 1 && aip->auctionError == ae_mustsignin) {
			if (forceEbayLogin(aip))
				break;
		} else if (aip->auctionError == ae_notime)
			/* Blank time remaining -- give it another chance */
			sleep(2);
		else
			break;
	}
	return ret;
}

/*
 * Note: quant=1 is just to dupe eBay into allowing the pre-bid to get
 *	 through.  Actual quantity will be sent with bid.
 */
static const char PRE_BID_URL[] = "http://%s/ws/eBayISAPI.dll?MfcISAPICommand=MakeBid&fb=2&co_partner_id=&item=%s&maxbid=%s&quant=%s";

/*
 * Get bid key
 *
 * returns 0 on success, 1 on failure.
 */
static int
preBid(auctionInfo *aip)
{
	memBuf_t *mp = NULL;
	int quantity = getQuantity(options.quantity, aip->quantity);
	char quantityStr[12];	/* must hold an int */
	size_t urlLen;
	char *url;
	int ret = 0;
	int found = 0;

	if (ebayLogin(aip, 0))
		return 1;
	sprintf(quantityStr, "%d", quantity);
	urlLen = sizeof(PRE_BID_URL) + strlen(options.prebidHost) + strlen(aip->auction) + strlen(aip->bidPriceStr) + strlen(quantityStr) - (4*2);
	url = (char *)myMalloc(urlLen);
	sprintf(url, PRE_BID_URL, options.prebidHost, aip->auction, aip->bidPriceStr, quantityStr);
	log(("\n\n*** preBid(): url is %s\n", url));
	mp = httpGet(url, NULL);
	free(url);
	if (!mp)
		return httpError(aip);

	ret = parsePreBid(mp, aip);
	freeMembuf(mp);
	return ret;
}

static int
parsePreBid(memBuf_t *mp, auctionInfo *aip)
{
	int ret = 0;
	int found = 0;

	memReset(mp);
	while (!match(mp, "name=\"uiid\"")) {
		char *start, *value, *end;

		for (start = mp->readptr; start >= mp->memory && *start != '<'; --start)
			;
		value = strcasestr(start, "value=\"");
		end = strchr(start, '>');

		if (!value || !end || value > end)
			continue;
		free(aip->biduiid);
		mp->readptr = value + 7;
		aip->biduiid = myStrdup(getUntil(mp, '\"'));
		log(("preBid(): biduiid is \"%s\"", aip->biduiid));
		found = 1;
		break;
	}

	if (!found) {
		pageInfo_t *pageInfo = getPageInfo(mp);

		ret = makeBidError(pageInfo, aip);
		if (ret < 0) {
			ret = auctionError(aip, ae_biduiid, NULL);
			bugReport("preBid", __FILE__, __LINE__, aip, mp, optiontab, "cannot find bid uiid");
		}
		freePageInfo(pageInfo);
	}
	return ret;
}

static const char LOGIN_1_URL[] = "https://%s/ws/eBayISAPI.dll?SignIn";
static const char LOGIN_2_URL[] = "https://%s/ws/eBayISAPI.dll?SignInWelcome&userid=%s&pass=%s&keepMeSignInOption=1";


/*
 * Force an ebay login.
 *
 * Returns 0 on success, 1 on failure.
 */
static int
forceEbayLogin(auctionInfo *aip)
{
	loginTime = 0;
	return ebayLogin(aip, 0);
}

/*
 * Ebay login.  Make sure loging has been done with the given interval.
 *
 * Returns 0 on success, 1 on failure.
 */
static int
ebayLogin(auctionInfo *aip, time_t interval)
{
	memBuf_t *mp = NULL;
	size_t urlLen;
	char *url, *logUrl;
	pageInfo_t *pp;
	int ret = 0;
	char *password;

	/* negative value forces login */
	if (loginTime > 0) {
		if (interval == 0)
			interval = defaultLoginInterval;	/* default: 12 hours */
		if ((time(NULL) - loginTime) <= interval)
			return 0;
	}

	cleanupCurlStuff();
	if (initCurlStuff())
		return auctionError(aip, ae_unknown, NULL);

	urlLen = sizeof(LOGIN_1_URL) + strlen(options.loginHost) - (1*2);
	url = (char *)myMalloc(urlLen);
	sprintf(url, LOGIN_1_URL, options.loginHost);
	mp = httpGet(url, NULL);
	free(url);
	if (!mp)
		return httpError(aip);
	freeMembuf(mp);
	mp = NULL;

	urlLen = sizeof(LOGIN_2_URL) + strlen(options.loginHost) + strlen(options.usernameEscape) - (3*2);
	password = getPassword();
	url = (char *)myMalloc(urlLen + strlen(password));
	logUrl = (char *)myMalloc(urlLen + 5);

	sprintf(url, LOGIN_2_URL, options.loginHost, options.usernameEscape, password);
	freePassword(password);
	sprintf(logUrl, LOGIN_2_URL, options.loginHost, options.usernameEscape, "*****");

	mp = httpGet(url, logUrl);
	free(url);
	free(logUrl);
	if (!mp)
		return httpError(aip);

	if ((pp = getPageInfo(mp))) {
		log(("ebayLogin(): pagename = \"%s\", pageid = \"%s\", srcid = \"%s\"", nullStr(pp->pageName), nullStr(pp->pageId), nullStr(pp->srcId)));
		/*
		 * Pagename is usually MyeBaySummary, but it seems as though
		 * it can be any MyeBay page, and eBay is not consistent with
		 * naming of MyeBay pages (MyeBay, MyEbay, myebay, ...) so
		 * esniper must use strncasecmp().
		 */
		if ((pp->srcId && !strcmp(pp->srcId, "SignInAlertSupressor"))||
		    (pp->pageName &&
			(!strncasecmp(pp->pageName, "MyeBay", 6) ||
			 !strncasecmp(pp->pageName, "My eBay", 7))
		    ))
			loginTime = time(NULL);
		else if (pp->pageName &&
				(!strcmp(pp->pageName, "Welcome to eBay") ||
				 !strcmp(pp->pageName, "Welcome to eBay - Sign in - Error")))
			ret = auctionError(aip, ae_badpass, NULL);
		else if (pp->pageName && !strcmp(pp->pageName, "PageSignIn"))
			ret = auctionError(aip, ae_login, NULL);
		else if (pp->srcId && !strcmp(pp->srcId, "Captcha.xsl"))
			ret = auctionError(aip, ae_captcha, NULL);
		else {
			ret = auctionError(aip, ae_login, NULL);
			bugReport("ebayLogin", __FILE__, __LINE__, aip, mp, optiontab, "unknown pageinfo");
		}
	} else {
		log(("ebayLogin(): pageinfo is NULL\n"));
		ret = auctionError(aip, ae_login, NULL);
		bugReport("ebayLogin", __FILE__, __LINE__, aip, mp, optiontab, "pageinfo is NULL");
	}
	freeMembuf(mp);
	freePageInfo(pp);
	return ret;
}

/*
 * acceptBid: handle all known AcceptBid pages.
 *
 * Returns -1 if page not recognized, 0 if bid accepted, 1 if bid not accepted.
 */
static int
acceptBid(const char *pagename, auctionInfo *aip)
{
	static const char ACCEPTBID[] = "AcceptBid_";
	static const char HIGHBID[] = "HighBidder";
	static const char OUTBID[] = "Outbid";
	static const char RESERVENOTMET[] = "ReserveNotMet";

	if (!strcmp(pagename, "Bid confirmation"))
		return aip->bidResult = 0;

	if (!pagename ||
	    strncmp(pagename, ACCEPTBID, sizeof(ACCEPTBID) - 1))
		return -1;
	pagename += sizeof(ACCEPTBID) - 1;

	/*
	 * valid pagenames include AcceptBid_HighBidder,
	 * AcceptBid_HighBidder_rebid, possibly others.
	 */
	if (!strncmp(pagename, HIGHBID, sizeof(HIGHBID) - 1))
		return aip->bidResult = 0;
	/*
	 * valid pagenames include AcceptBid_Outbid, AcceptBid_Outbid_rebid,
	 * possibly others.
	 */
	if (!strncmp(pagename, OUTBID, sizeof(OUTBID) - 1))
		return aip->bidResult = auctionError(aip, ae_outbid, NULL);
	/*
	 * valid pagenames include AcceptBid_ReserveNotMet,
	 * AcceptBid_ReserveNotMet_rebid, possibly others.
	 */
	if (!strncmp(pagename, RESERVENOTMET, sizeof(RESERVENOTMET) - 1))
		return aip->bidResult = auctionError(aip, ae_reservenotmet, NULL);
	/* unknown AcceptBid page */
	return -1;
}

/*
 * makeBidError: handle all known MakeBidError pages.
 *
 * Returns -1 if page not recognized, 0 if bid accepted, 1 if bid not accepted.
 */
static int
makeBidError(const pageInfo_t *pageInfo, auctionInfo *aip)
{
	static const char MAKEBIDERROR[] = "MakeBidError";
	const char *pagename = pageInfo->pageName;

	if (!pagename) {
		const char *srcId = pageInfo->srcId;

		if (srcId && !strcasecmp(srcId, "ViewItem"))
			return aip->bidResult = auctionError(aip, ae_ended, NULL);
		else
			return -1;
	}
	if (!strcasecmp(pagename, "Place bid"))
		return aip->bidResult = auctionError(aip, ae_outbid, NULL);
	if (!strcasecmp(pagename, "eBay Alerts"))
		return aip->bidResult = auctionError(aip, ae_alert, NULL);
	if (!strcasecmp(pagename, "Buyer Requirements"))
		return aip->bidResult = auctionError(aip, ae_buyerrequirements, NULL);

	if (!strcasecmp(pagename, "PageSignIn"))
		return aip->bidResult = auctionError(aip, ae_mustsignin, NULL);
	if (!strncasecmp(pagename, "BidManager", 10) ||
	    !strncasecmp(pagename, "BidAssistant", 12))
		return aip->bidResult = auctionError(aip, ae_bidassistant, NULL);

	if (strncasecmp(pagename, MAKEBIDERROR, sizeof(MAKEBIDERROR) - 1))
		return -1;
	pagename += sizeof(MAKEBIDERROR) - 1;
	if (!*pagename ||
	    !strcasecmp(pagename, "AuctionEnded"))
		return aip->bidResult = auctionError(aip, ae_ended, NULL);
	if (!strcasecmp(pagename, "AuctionEnded_BINblock") ||
	    !strcasecmp(pagename, "AuctionEnded_BINblock "))
		return aip->bidResult = auctionError(aip, ae_cancelled, NULL);
	if (!strcasecmp(pagename, "Password"))
		return aip->bidResult = auctionError(aip, ae_badpass, NULL);
	if (!strcasecmp(pagename, "MinBid"))
		return aip->bidResult = auctionError(aip, ae_bidprice, NULL);
	if (!strcasecmp(pagename, "BuyerBlockPref"))
		return aip->bidResult = auctionError(aip, ae_buyerblockpref, NULL);
	if (!strcasecmp(pagename, "BuyerBlockPrefDoesNotShipToLocation"))
		return aip->bidResult = auctionError(aip, ae_buyerblockprefdoesnotshiptolocation, NULL);
	if (!strcasecmp(pagename, "BuyerBlockPrefNoLinkedPaypalAccount"))
		return aip->bidResult = auctionError(aip, ae_buyerblockprefnolinkedpaypalaccount, NULL);
	if (!strcasecmp(pagename, "HighBidder"))
		return aip->bidResult = auctionError(aip, ae_highbidder, NULL);
	if (!strcasecmp(pagename, "CannotBidOnItem"))
		return aip->bidResult = auctionError(aip, ae_cannotbid, NULL);
	if (!strcasecmp(pagename, "DutchSameBidQuantity"))
		return aip->bidResult = auctionError(aip, ae_dutchsamebidquantity, NULL);
	if (!strcasecmp(pagename, "BuyerBlockPrefItemCountLimitExceeded"))
		return aip->bidResult = auctionError(aip, ae_buyerblockprefitemcountlimitexceeded, NULL);
	if (!strcasecmp(pagename, "BidGreaterThanBin_BINblock"))
		return aip->bidResult = auctionError(aip, ae_bidgreaterthanbin_binblock, NULL);
	/* unknown MakeBidError page */
	return -1;
}

/*
 * Parse bid result.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
parseBid(memBuf_t *mp, auctionInfo *aip)
{
	/*
	 * The following sometimes have more characters after them, for
	 * example AcceptBid_HighBidder_rebid (you were already the high
	 * bidder and placed another bid).
	 */
	pageInfo_t *pageInfo = getPageInfo(mp);
	int ret;

	aip->bidResult = -1;
	log(("parseBid(): pagename = %s\n", pageInfo->pageName));
	if ((ret = acceptBid(pageInfo->pageName, aip)) >= 0 ||
	    (ret = makeBidError(pageInfo, aip)) >= 0) {
		;
	} else {
		bugReport("parseBid", __FILE__, __LINE__, aip, mp, optiontab, "unknown pagename");
		printLog(stdout, "Cannot determine result of bid\n");
		ret = 0;	/* prevent another bid */
	}
	freePageInfo(pageInfo);
	return ret;
} /* parseBid() */

static const char BID_URL[] = "http://%s/ws/eBayISAPI.dll?MfcISAPICommand=MakeBid&maxbid=%s&quant=%s&mode=1&uiid=%s&co_partnerid=2&user=%s&fb=2&item=%s";

/*
 * Place bid.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
bid(auctionInfo *aip)
{
	memBuf_t *mp = NULL;
	size_t urlLen;
	char *url, *logUrl, *tmpUsername, *tmpUiid;
	int ret;
	int quantity = getQuantity(options.quantity, aip->quantity);
	char quantityStr[12];	/* must hold an int */

	if (!aip->biduiid)
		return auctionError(aip, ae_biduiid, NULL);

	if (ebayLogin(aip, 0))
		return 1;
	sprintf(quantityStr, "%d", quantity);

	/* create url */
	urlLen = sizeof(BID_URL) + strlen(options.bidHost) + strlen(aip->bidPriceStr) + strlen(quantityStr) + strlen(aip->biduiid) + strlen(options.usernameEscape) + strlen(aip->auction) - (6*2);
	url = (char *)myMalloc(urlLen);
	sprintf(url, BID_URL, options.bidHost, aip->bidPriceStr, quantityStr, aip->biduiid, options.usernameEscape, aip->auction);

	logUrl = (char *)myMalloc(urlLen);
	tmpUsername = stars(strlen(options.usernameEscape));
	tmpUiid = stars(strlen(aip->biduiid));
	sprintf(logUrl, BID_URL, options.bidHost, aip->bidPriceStr, quantityStr, tmpUiid, tmpUsername, aip->auction);
	free(tmpUsername);
	free(tmpUiid);

	if (!options.bid) {
		printLog(stdout, "Bidding disabled\n");
		log(("\n\nbid(): query url:\n%s\n", logUrl));
		ret = aip->bidResult = 0;
	} else if (!(mp = httpGet(url, logUrl))) {
		ret = httpError(aip);
	} else {
		ret = parseBid(mp, aip);
	}
	free(url);
	free(logUrl);
	freeMembuf(mp);
	return ret;
} /* bid() */

/*
 * watch(): watch auction until it is time to bid
 *
 * returns:
 *	0 OK
 *	1 Error
 */
static int
watch(auctionInfo *aip)
{
	int errorCount = 0;
	long remain = LONG_MIN;
	unsigned int sleepTime = 0;

	log(("*** WATCHING auction %s price-each %s quantity %d bidtime %ld\n", aip->auction, aip->bidPriceStr, options.quantity, options.bidtime));

	for (;;) {
		time_t tmpLatency;
		time_t start = time(NULL);
		time_t timeToFirstByte = 0;
		int ret = getInfoTiming(aip, &timeToFirstByte);
		time_t end = time(NULL);

		if (timeToFirstByte == 0)
			timeToFirstByte = end;
		tmpLatency = (timeToFirstByte - start);
		if ((tmpLatency >= 0) && (tmpLatency < 600))
			aip->latency = tmpLatency;
		printLog(stdout, "Latency: %d seconds\n", aip->latency);

		if (ret) {
			printAuctionError(aip, stderr);

			/*
			 * Fatal error?  We allow up to 50 errors, then quit.
			 * eBay "unavailable" doesn't count towards the total.
			 */
			if (aip->auctionError == ae_unavailable) {
				if (remain >= 0)
					remain = newRemain(aip);
				if (remain == LONG_MIN || remain > 86400) {
					/* typical eBay maintenance period
					 * is two hours.  Sleep for half that
					 * amount of time.
					 */
					printLog(stdout, "%s: Will try again, sleeping for an hour\n", timestamp());
					sleepTime = 3600;
					sleep(sleepTime);
					continue;
				}
			} else if (remain == LONG_MIN) {
				/* first time through?  Give it 3 chances then
				 * make the error fatal.
				 */
				int j;

				for (j = 0; ret && j < 3 && aip->auctionError == ae_notitle; ++j)
					ret = getInfo(aip);
				if (ret)
					return 1;
				remain = newRemain(aip);
			} else {
				/* non-fatal error */
				log(("ERROR %d!!!\n", ++errorCount));
				if (errorCount > 50)
					return auctionError(aip, ae_toomany, NULL);
				printLog(stdout, "Cannot find auction - internet or eBay problem?\nWill try again after sleep.\n");
				remain = newRemain(aip);
			}
		} else if (!isValidBidPrice(aip))
			return auctionError(aip, ae_bidprice, NULL);
		else
			remain = newRemain(aip);

		/*
		 * Check login when we are close to bidding.
		 */
		if (remain <= 300) {
			if (ebayLogin(aip, defaultLoginInterval - 600))
				return 1;
			remain = newRemain(aip);
		}

		/*
		 * if we're less than two minutes away, get bid key
		 */
		if (remain <= 150 && !aip->biduiid && aip->auctionError == ae_none) {
			int i;

			printf("\n");
			for (i = 0; i < 5; ++i) {
				/* ae_biduiid is used when the page loaded
				 * but failed for some unknown reason.
				 * Do not try again in this situation.
				 */
				if (!preBid(aip) ||
				    aip->auctionError == ae_biduiid)
					break;
				if (aip->auctionError == ae_mustsignin &&
				    forceEbayLogin(aip))
					break;
			}
			if (aip->auctionError != ae_none &&
			    aip->auctionError != ae_highbidder) {
				printLog(stderr, "Cannot get bid key\n");
				return 1;
			}
		}

		remain = newRemain(aip);

		/* it's time!!! */
		if (remain <= 0)
			break;

		/*
		 * Setup sleep schedule so we get updates once a day, then
		 * at 2 hours, 1 hour, 5 minutes, 2 minutes
		 */
		if (remain <= 150)	/* 2 minutes + 30 seconds (slop) */
			sleepTime = (unsigned int)remain;
		else if (remain < 720)	/* 5 minutes + 2 minutes (slop) */
			sleepTime = (unsigned int)remain - 120;
		else if (remain < 3900)	/* 1 hour + 5 minutes (slop) */
			sleepTime = (unsigned int)remain - 600;
		else if (remain < 10800)/* 2 hours + 1 hour (slop) */
			sleepTime = (unsigned int)remain - 3600;
		else if (remain < 97200)/* 1 day + 3 hours (slop) */
			sleepTime = (unsigned int)remain - 7200;
		else			/* knock off one day */
			sleepTime = 86400;

		printf("%s: ", timestamp());
		if (sleepTime >= 86400)
			printLog(stdout, "Sleeping for a day\n");
		else if (sleepTime >= 3600)
			printLog(stdout, "Sleeping for %d hours %d minutes\n",
				sleepTime/3600, (sleepTime % 3600) / 60);
		else if (sleepTime >= 60)
			printLog(stdout, "Sleeping for %d minutes %d seconds\n",
				sleepTime/60, sleepTime % 60);
		else
			printLog(stdout, "Sleeping for %ld seconds\n", sleepTime);
		sleep(sleepTime);
		printf("\n");

		if ((remain=newRemain(aip)) <= 0)
			break;
	}
	return 0;
} /* watch() */

/*
 * parameters:
 * aip	auction to bid on
 *
 * return number of items won
 */
int
snipeAuction(auctionInfo *aip)
{
	int won = 0;
	char *tmpUsername;

	if (!aip)
		return 0;

	if (options.debug)
		logOpen(aip, options.logdir);

	tmpUsername = stars(strlen(options.username));
	log(("auction %s price %s quantity %d user %s bidtime %ld\n",
	     aip->auction, aip->bidPriceStr,
	     options.quantity, tmpUsername, options.bidtime));
	free(tmpUsername);

	if (ebayLogin(aip, 0)) {
		printAuctionError(aip, stderr);
		return 0;
	}

	/* 0 means "now" */
	if ((options.bidtime == 0) ? preBid(aip) : watch(aip)) {
		printAuctionError(aip, stderr);
		if (aip->auctionError != ae_highbidder)
			return 0;
	}

	/* ran out of time! */
	if (aip->endTime <= time(NULL)) {
		(void)auctionError(aip, ae_ended, NULL);
		printAuctionError(aip, stderr);
		return 0;
	}

	if (aip->auctionError != ae_highbidder) {
		printLog(stdout, "\nAuction %s: Bidding...\n", aip->auction);
		for (;;) {
			if (bid(aip)) {
				/* failed bid */
				if (aip->auctionError == ae_mustsignin) {
					if (!forceEbayLogin(aip))
						continue;
				}
				printAuctionError(aip, stderr);
				return 0;
			}
			break;
		}
	}

	/* view auction after bid.
	 * Stick it in a loop in case our timing is a bit off (due
	 * to wild swings in latency, for instance).
	 */
	for (;;) {
		if (options.bidtime > 0 && options.bidtime < 60) {
			time_t seconds = aip->endTime - time(NULL);

			if (seconds < 0)
				seconds = 0;
			/* extra 2 seconds to make sure auction is over */
			seconds += 2;
			printLog(stdout, "Auction %s: Waiting %d seconds for auction to complete...\n", aip->auction, seconds);
			sleep((unsigned int)seconds);
		}

		printLog(stdout, "\nAuction %s: Post-bid info:\n",
			 aip->auction);
		if (getInfo(aip))
			printAuctionError(aip, stderr);
		if (aip->remain > 0 && aip->remain < 60 &&
		    options.bidtime > 0 && options.bidtime < 60)
			continue;
		break;
	}

	if (aip->won == -1) {
		won = options.quantity < aip->quantity ?
			options.quantity : aip->quantity;
		printLog(stdout, "\nunknown outcome, assume that you have won %d items\n", won);
	} else {
		won = aip->won;
		printLog(stdout, "\nwon %d item(s)\n", won);
	}
	options.quantity -= won;
	return won;
}

/* Max \td in the description table (is 8 on 02 of May 2010): */
#define MAX_TDS 8
#define MAX_TDS_LENGTH 8

/*
 * On first call, use printNewline to 0.  On subsequent calls, use return
 * value from previous call.
 */
static int
printMyItemsRow(char **row, int printNewline)
{
	const char *myitems_description[MAX_TDS][MAX_TDS_LENGTH] = {
		{0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0},
		{"Description:\t%s\n", 0, "Seller:\t\t%s", 0, 0, " ( %s", 0, " | %s )\n" },
		{ "Time left:\t%s\n", 0, 0, 0, 0, 0, 0, 0},
		{ "Price:\t\t%s\n", 0,  "Bids:\t\t%s\n", 0, "Shipping:\t%s\n", 0, 0, 0},
		{ 0, 0, 0, 0, 0, 0, 0, 0},
		{ 0, 0, 0, 0, 0, 0, 0, 0},
		{ 0, 0, 0, 0, 0, 0, 0, 0},
	};
	int column = 0;
	int ret = printNewline;
	int item_nr=0;	/* count no_tag item */

	for (; row[column]; ++column) {
		memBuf_t buf;
		char *value = NULL;

		if (column == 0) { /* item nr on checkbox in 1st (-1) column */
			static const char search[] = "value=";
			char *tmp = strstr(row[column], search);

			if (tmp) {
				int i;

				tmp += sizeof(search) - 1;
				for (; !isdigit(*tmp); ++tmp)
					;
				for (i = 1; isdigit(tmp[i]); ++i)
					;
				value = myStrndup(tmp, (size_t)(i));
				printLog(stdout, "ItemNr:\t\t%s\n", value);
				free(value);
			}
		}
		strToMemBuf(row[column], &buf); /* load new row */
		for (item_nr = 0; item_nr < MAX_TDS_LENGTH; item_nr++) {
			value = getNonTag(&buf);

			/* there may be a "ENDING SOON" message */
			if ((column==2)&&(item_nr==0)&&strstr(value,"ENDING SOON"))
				value = getNonTag(&buf);
			/* when nothing interesting in row */
			if (column >= MAX_TDS || !myitems_description[column][item_nr])
				continue;
			/* print the entry */
			printLog(stdout, myitems_description[column][item_nr], value ? value : "");
		}
	}
	printf("\n");	/* for spacing */
	return ret;
}

static const char MYITEMS_URL[] = "http://%s/ws/eBayISAPI.dll?MyeBay&CurrentPage=MyeBayWatching";

/*
 * TODO: allow user configuration of myItems.
 */
int
printMyItems(void)
{
	memBuf_t *mp = NULL;
	const char *table;
	char **row;
	auctionInfo *dummy = newAuctionInfo("0", "0");
	char *url;
	size_t urlLen;

	if (ebayLogin(dummy, 0)) {
		printAuctionError(dummy, stderr);
		freeAuction(dummy);
		return 1;
	}
	urlLen = sizeof(MYITEMS_URL) + strlen(options.myeBayHost) - (1*2);
	url = (char *)myMalloc(urlLen);
	sprintf(url, MYITEMS_URL, options.myeBayHost);
	mp = httpGet(url, NULL);
	free(url);
	if (!mp) {
		httpError(dummy);
		printAuctionError(dummy, stderr);
		freeAuction(dummy);
		freeMembuf(mp);
		return 1;
	}
	while ((table = getTableStart(mp))) {
		int printNewline = 0;

		/* search for table containing my itmes */
		if (!strstr(table, "class=\"my_itl-iT\""))
			continue;
		/* skip first descriptive table row */
		if ((row = getTableRow(mp)))
			freeTableRow(row);
		else {
			freeAuction(dummy);
			return 0; /* error? */
		}
		while ((row = getTableRow(mp))) {
			printNewline = printMyItemsRow(row, printNewline);
			freeTableRow(row);
		}
	}
	freeAuction(dummy);
	freeMembuf(mp);
	return 0;
}

/* secret option - test parser */
void
testParser(int flag)
{
	memBuf_t *mp = readFile(stdin);

	switch (flag) {
	case 1:
	    {
		/* print pagename */
		char *line;

		/* dump non-tag data */
		while ((line = getNonTag(mp)))
			printf("\"%s\"\n", line);

		/* pagename? */
		memReset(mp);
		if ((line = getPageName(mp)))
			printf("\nPAGENAME is \"%s\"\n", line);
		else
			printf("\nPAGENAME is NULL\n");
		break;
	    }
	case 2:
	    {
		/* run through bid history parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		time_t start = time(NULL), end;
		int ret = parseBidHistory(mp, aip, start, &end, 1);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	case 3:
	    {
		/* run through bid result parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		int ret = parseBid(mp, aip);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	case 4:
	    {
		/* print bid history table */
		const char *table;
		char **row;
		char *cp;
		int rowNum = 0;

		while ((cp = getNonTag(mp))) {
			if (!strcmp(cp, "Time left:"))
				break;
			if (!strcmp(cp, "Time Ended:"))
				break;
		}
		if (!cp) {
			printf("time left not found!\n");
			break;
		}
		(void)getTableStart(mp); /* skip one table */
		table = getTableStart(mp);
		if (!table) {
			printf("no table found!\n");
			break;
		}

		printf("table: %s\n", table);
		while ((row = getTableRow(mp))) {
			int columnNum = 0;

			printf("\trow %d:\n", rowNum++);
			for (; row[columnNum]; ++columnNum) {
				memBuf_t buf;

				strToMemBuf(row[columnNum], &buf);
				printf("\t\tcolumn %d: %s\n", columnNum, getNonTag(mp));
				free(row[columnNum]);
			}
		}
		break;
	    }
	case 5:
		{
		/* run through prebid parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		int ret = parsePreBid(mp, aip);

		printf("ret = %d\n", ret);
		printf("uiid = %s\n", aip->biduiid);
		printAuctionError(aip, stdout);
		break;
		}
	}
}
