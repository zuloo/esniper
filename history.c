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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "html.h"
#include "auction.h"
#include "auctioninfo.h"
#include "history.h"
#include "esniper.h"

static long getSeconds(char *timestr);
static int checkPageType(auctionInfo *aip, int pageType, int auctionState, int auctionResult);
static int parseBidHistoryInternal(pageInfo_t *pp, memBuf_t *mp, auctionInfo *aip, time_t start, int debugMode);

static const char PRIVATE[] = "private auction - bidders' identities protected";

/* pageType */
#define VIEWBIDS 1
#define VIEWTRANSACTIONS 2

/* auctionState */
#define STATE_ACTIVE 1
#define STATE_CLOSED 2

/* auctionResult */
#define RESULT_HIGH_BIDDER 1
#define RESULT_NONE 2
#define RESULT_OUTBID 3

#define NOTHING 0
#define PRICE 1
#define QUANTITY 2
#define SHIPPING 4
#define EVERYTHING (PRICE | QUANTITY | SHIPPING)

/*
 * parseBidHistory(): parses bid history page (pageName: PageViewBids)
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) - sets auctionError
 */
int
parseBidHistory(memBuf_t *mp, auctionInfo *aip, time_t start, time_t *timeToFirstByte, int debugMode)
{
	pageInfo_t *pp;
	int ret = 0;

	resetAuctionError(aip);

	if (timeToFirstByte)
		*timeToFirstByte = getTimeToFirstByte(mp);

	if ((pp = getPageInfo(mp))) {
		ret = parseBidHistoryInternal(pp, mp, aip, start, debugMode);
		freePageInfo(pp);
	} else {
		log(("parseBidHistory(): pageinfo is NULL\n"));
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "pageInfo is NULL");
		ret = auctionError(aip, ae_notitle, NULL);
	}
	return ret;
}

int
parseBidHistoryInternal(pageInfo_t *pp, memBuf_t *mp, auctionInfo *aip, time_t start, int debugMode)
{
	char *line;
	char **row = NULL;
	int ret = 0;		/* 0 = OK, 1 = failed */
	int foundHeader = 0;	/* found bid history table header */
	int pageType = 0;
	int auctionState = 0;
	int auctionResult = 0;
	int got;
	const char *delim = "_";



	if ((pp->srcId && !strcmp(pp->srcId, "Captcha.xsl")) ||
		(pp->pageName && !strncmp(pp->pageName, "Security Measure", 16)))
		return auctionError(aip, ae_captcha, NULL);
	if (pp->pageName && !strncmp(pp->pageName, "PageViewBids", 12)) {
		char *tmpPagename = myStrdup(pp->pageName);
		char *token;

		pageType = VIEWBIDS;

		/* this must be PageViewBids */
		token = strtok(tmpPagename, delim);
		token = strtok(NULL, delim);
		if (token != NULL) {
			if(!strcmp(token, "Active")) auctionState = STATE_ACTIVE;
			else if(strcmp(token, "Closed")) auctionState = STATE_CLOSED;
			token = strtok(NULL, delim);
			if (token != NULL) {
				if(!strcmp(token, "None")) auctionResult = RESULT_NONE;
				else if(!strcmp(token, "HighBidder")) auctionResult = RESULT_HIGH_BIDDER;
				else if(!strcmp(token, "Outbid")) auctionResult = RESULT_OUTBID;
			}
		}
		free(tmpPagename);

		/* bid history or expired/bad auction number */
		while ((line = getNonTag(mp))) {
			if (!strcmp(line, "Bid History")) {
				log(("parseBidHistory(): got \"Bid History\"\n"));
				break;
			}
			if (!strcmp(line, "Unknown Item")) {
				log(("parseBidHistory(): got \"Unknown Item\"\n"));
				return auctionError(aip, ae_baditem, NULL);
			}
		}
	} else if (pp->pageName && !strncmp(pp->pageName, "PageViewTransactions", 20)) {
		/* transaction history -- buy it now only */
		pageType = VIEWTRANSACTIONS;
	} else if (pp->pageName && !strcmp(pp->pageName, "PageSignIn")) {
		return auctionError(aip, ae_mustsignin, NULL);
	} else {
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "unknown pagename");
		return auctionError(aip, ae_notitle, NULL);
	}

	/* Auction number */
	memReset(mp);
	if (memStr(mp, "\"BHCtBidLabel\"") ||
		memStr(mp, "\"vizItemNum\"") ||
		memStr(mp, "\"BHitemNo\"")) { /* obsolete as of 2.22 */
		memChr(mp, '>');
		memSkip(mp, 1);
		line = getNonTag(mp);	/* Item number: */
		line = getNonTag(mp);	/* number */
		if (!line) {
			log(("parseBidHistory(): No item number"));
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "no item number");
			return auctionError(aip, ae_baditem, NULL);
		}
	} else {
		log(("parseBidHistory(): BHitemNo not found"));
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "no item number");
		return auctionError(aip, ae_baditem, NULL);
	}
	if (debugMode) {
		free(aip->auction);
		aip->auction = myStrdup(line);
	} else {
		if (strcmp(aip->auction, line)) {
			log(("parseBidHistory(): auction number %s does not match given number %s", line, aip->auction));
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "mismatched item number");
			return auctionError(aip, ae_baditem, NULL);
		}
	}

	/* Auction title */
	memReset(mp);
	if (memStr(mp, "\"itemTitle\"") ||
		memStr(mp, "\"BHitemTitle\"") || /* obsolete as of 2.22 */
		memStr(mp, "\"BHitemDesc\"")) {	/* obsolete before 2.22 */
		memChr(mp, '>');
		memSkip(mp, 1);
		line = getNonTag(mp);	/* Item title: */
		line = getNonTag(mp);	/* title */
		if (!line) {
			log(("parseBidHistory(): No item title"));
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "item title not found");
			return auctionError(aip, ae_baditem, NULL);
		}
	} else {
		log(("parseBidHistory(): BHitemTitle not found"));
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "item title or description not found");
		return auctionError(aip, ae_baditem, NULL);
	}
	free(aip->title);
	aip->title = myStrdup(line);
	printLog(stdout, "Auction %s: %s\n", aip->auction, aip->title);

	/* price, shipping, quantity */
	memReset(mp);
	aip->quantity = 1;	/* If quantity not found, assume 1 */
	got = NOTHING;
	while (got != EVERYTHING && memStr(mp, "\"BHCtBid\"")) {
		memChr(mp, '>');
		memSkip(mp, 1);
		line = getNonTag(mp);

		/* Can sometimes get starting bid, but that's not the price
		 * we are looking for.
		 */
		if (!strcasecmp(line, "Current bid:") ||
		    !strcasecmp(line, "Winning bid:") ||
		    !strcasecmp(line, "Your maximum bid:") ||
		    !strcasecmp(line, "price:")) {
			char *saveptr;

			line = getNonTag(mp);
			if (!line) {
				bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "item price not found");
				return auctionError(aip, ae_noprice, NULL);
			}
			log(("Currently: %s\n", line));
			aip->price = atof(priceFixup(line, aip));
			if (aip->price < 0.01) {
				bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "item price could not be converted");
				return auctionError(aip, ae_convprice, line);
			}
			got |= PRICE;

			/* reserve not met? */
			saveptr = mp->readptr;
			line = getNonTag(mp);
			aip->reserve = !strcasecmp(line, "Reserve not met");
			if (!aip->reserve)
				mp->readptr = saveptr;
		} else if (!strcasecmp(line, "Quantity:")) {
			line = getNonTag(mp);
			if (!line) {
				bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "item quantity not found");
				return auctionError(aip, ae_noquantity, NULL);
			}
			errno = 0;
			if (isdigit(*line)) {
				aip->quantity = (int)strtol(line, NULL, 10);
				if (aip->quantity < 0 || (aip->quantity == 0 && errno == EINVAL)) {
					bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "item quantity could not be converted");
					return auctionError(aip, ae_noquantity, NULL);
				}
			} else
				aip->quantity = 1;
			log(("quantity: %d", aip->quantity));
			got |= QUANTITY;
		} else if (!strcasecmp(line, "Shipping:")) {
			line = getNonTag(mp);
			if (line) {
				free(aip->shipping);
				aip->shipping = myStrdup(line);
			}
			got |= SHIPPING;
		}
	}

	/* Time Left */
	memReset(mp);
	if (aip->quantity == 0 || memStr(mp, "Time Ended:")) {
		free(aip->remainRaw);
		aip->remainRaw = myStrdup("--");
		aip->remain = 0;
	} else if (memStr(mp, "timeLeft")) {
		memChr(mp, '>');
		memSkip(mp, 1);
		free(aip->remainRaw);
		aip->remainRaw = myStrdup(getNonTag(mp));
		if (!strcasecmp(aip->remainRaw, "Duration:")) {
			/* Duration may follow Time left.  If we
			 * see this, time left must be empty.  Assume 1 second.
			 */
			free(aip->remainRaw);
			aip->remainRaw = myStrdup("");
			aip->remain = 1;
		}
		if (!strcasecmp(aip->remainRaw, "Refresh")) {
			/* Refresh is the label on the next button.  If we
			 * see this, time left must be empty.  Assume 1 second.
			 */
			free(aip->remainRaw);
			aip->remainRaw = myStrdup("");
			aip->remain = 1;
		} else if (!strncasecmp(aip->remainRaw, "undefined", 9)) {
			/* Shows up very rarely, seems to be an intermediate
			 * step between emtpy time left and time ended.
			 * Assume 1 second.
			 */
			aip->remain = 1;
		} else
			aip->remain = getSeconds(aip->remainRaw);
		if (aip->remain < 0) {
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "remaining time could not be converted");
			return auctionError(aip, ae_badtime, aip->remainRaw);
		}
	} else {
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "remaining time not found");
		return auctionError(aip, ae_notime, NULL);
	}
	printLog(stdout, "Time remaining: %s (%ld seconds)\n", aip->remainRaw, aip->remain);
	if (aip->remain) {
		struct tm *tmPtr;
		char timestr[20];

		aip->endTime = start + aip->remain;
		/* formated time/date output */
		tmPtr = localtime(&(aip->endTime));
		strftime(timestr , 20, "%d/%m/%Y %H:%M:%S", tmPtr);
		if (!debugMode)
			printLog(stdout, "End time: %s\n", timestr);
	} else
		aip->endTime = aip->remain;

	/* bid history */
	memReset(mp);
	aip->bids = -1;
	if (memStr(mp, "Total Bids:")) {
		line = getNonTag(mp);	/* Total Bids: */
		line = getNonTag(mp);	/* number */
		log(("bids: %d", line));
		if (line) {
			errno = 0;
			aip->bids = (int)strtol(line, NULL, 10);
			if (aip->bids < 0 || (aip->bids == 0 && errno == EINVAL))
				aip->bids = -1;
			else if (aip->bids == 0) {
				aip->quantityBid = 0;
				aip->price = 0;
				printf("# of bids: %d\n"
					"Currently: --  (your maximum bid: %s)\n",
					aip->bids, aip->bidPriceStr);
				if (*options.username)
					printf("High bidder: -- (NOT %s)\n", options.username);
				else
					printf("High bidder: --\n");
				return 0;
			}
		}
	}

	/*
	 * Determine high bidder
	 *
	 * Format of high bidder table is:
	 *
	 *	Single item auction:
	 *	    Header line:
	 *		""
	 *		"Bidder"
	 *		"Bid Amount"
	 *		"Bid Time"
	 *		"Action" (sometimes)
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<date>
	 *			[links for feedback and other actions] (sometimes)
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No bids have been placed."
	 *
	 *	    If the auction is private, the user names are:
	 *		"private auction - bidders' identities protected"
	 *
	 *	Purchase (buy-it-now only):
	 *	    Header line:
	 *		""
	 *		"User ID"
	 *		"Price"
	 *		"Qty"
	 *		"Date"
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<price>
	 *			<quantity>
	 *			<date>
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No purchases have been made."
	 *
	 *	    If the auction is private, the user names are:
	 *		"private auction - bidders' identities protected"
	 *
	 *	If there are no bids, the text "No bids have been placed."
	 *	will be the first entry in the table.  If there are bids,
	 *	the last bidder might be "Starting Price", which should
	 *	not be counted.
	 */

	/* find bid history table */
	memReset(mp);
	while (!foundHeader && getTableStart(mp)) {
		int ncolumns;
		char *saveptr = mp->readptr;

		row = getTableRow(mp);
		ncolumns = numColumns(row);
		if (ncolumns >= 5) {
			char *rawHeader = row[1];
			char *header = getNonTagFromString(rawHeader);

			foundHeader = header &&
					(!strncmp(header, "Bidder", 6) ||
					 !strncmp(header, "User ID", 7));
			free(header);
		}
		if (!foundHeader)
			mp->readptr = saveptr;
		freeTableRow(row);
	}
	if (!foundHeader) {
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "Cannot find bid table header");
		return auctionError(aip, ae_nohighbid, NULL);
	}

	/* skip over initial single-column rows */
	while ((row = getTableRow(mp))) {
		if (numColumns(row) != 1)
			break;
		freeTableRow(row);
	}

	log(("numColumns=%d", numColumns(row)));
	/* roll through table */
	switch (numColumns(row)) {
	case 2:	/* auction with no bids */
	    {
		char *s = getNonTagFromString(row[1]);

		if (!strcmp("No bids have been placed.", s) ||
		    !strcmp("No purchases have been made.", s)) {
			aip->quantityBid = 0;
			aip->bids = 0;
			aip->price = 0;
			printf("# of bids: %d\n"
				"Currently: --  (your maximum bid: %s)\n",
				aip->bids, aip->bidPriceStr);
			if (*options.username)
				printf("High bidder: -- (NOT %s)\n", options.username);
			else
				printf("High bidder: --\n");
		} else {
			if (checkPageType(aip, pageType, auctionState, auctionResult) != 0)
			{
				bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "Unrecognized bid table line");
				ret = auctionError(aip, ae_nohighbid, NULL);
			}
		}
		freeTableRow(row);
		free(s);
		break;
	    }

	case 6:	/* purchase or maybe single auction */
		/* this case is before 5 because we will fall through if we know
		 * this is a normal auction.
		 */
	    if(pageType != VIEWBIDS)
	    {
			char *currently = getNonTagFromString(row[2]);

			aip->bids = 0;
			aip->quantityBid = 0;
			aip->won = 0;
			aip->winning = 0;
			/* find your purchase, count number of purchases */
			/* blank, user, price, quantity, date, blank */
			for (; row; row = getTableRow(mp)) {
				if (numColumns(row) == 6) {
					int quantity = getIntFromString(row[3]);
					char *bidder;

					++aip->bids;
					aip->quantityBid += quantity;
					bidder = getNonTagFromString(row[1]);
					if (!strcasecmp(bidder, options.username))
						aip->won = aip->winning = quantity;
					free(bidder);
				}
				freeTableRow(row);
			}
			printf("# of bids: %d\n", aip->bids);
			printf("Currently: %s  (your maximum bid: %s)\n",
				currently, aip->bidPriceStr);
			free(currently);
			switch (aip->winning) {
			case 0:
				if (*options.username)
					printLog(stdout, "High bidder: various purchasers (NOT %s)\n", options.username);
				else
					printLog(stdout, "High bidder: various purchasers\n");
				break;
			case 1:
				printLog(stdout, "High bidder: %s!!!\n", options.username);
				break;
			default:
				printLog(stdout, "High bidder: %s!!! (%d items)\n", options.username, aip->winning);
				break;
			}
			break;
	    }
	    /* FALLTHROUGH */
	case 5: /* single auction with bids */
	    {
		/* blank, user, price, date, blank */
		char *winner = getNonTagFromString(row[1]);
		char *currently = getNonTagFromString(row[2]);

		if (!strcasecmp(winner, "Member Id:"))
		   winner = getNthNonTagFromString(row[1], 2);

		aip->quantityBid = 1;

		/* current price */
		aip->price = atof(priceFixup(currently, aip));
		if (aip->price < 0.01) {
			free(winner);
			free(currently);
			if (checkPageType(aip, pageType, auctionState, auctionResult) == 0)
			{
				freeTableRow(row);
				break;
			}
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "bid price could not be converted");
			return auctionError(aip, ae_convprice, currently);
		}
		printLog(stdout, "Currently: %s  (your maximum bid: %s)\n",
			 currently, aip->bidPriceStr);
		free(currently);

		/* winning user */
		if (!strcmp(winner, PRIVATE)) {
			free(winner);
			winner = myStrdup((aip->price <= aip->bidPrice &&
					    (aip->bidResult == 0 ||
					     (aip->bidResult == -1 && aip->endTime - time(NULL) < options.bidtime))) ?  options.username : "[private]");
		}
		freeTableRow(row);

		/* count bids */
		if (aip->bids < 0) {
			int foundStartPrice = 0;
			for (aip->bids = 1; !foundStartPrice && (row = getTableRow(mp)); ) {
				if (numColumns(row) == 5) {
					char *bidder = getNonTagFromString(row[1]);

					foundStartPrice = !strcmp(bidder, "Starting Price");
					if (!foundStartPrice)
						++aip->bids;
					free(bidder);
				}
				freeTableRow(row);
			}
		}
		printLog(stdout, "# of bids: %d\n", aip->bids);

		/* print high bidder */
		if (strcasecmp(winner, options.username)) {
			if (*options.username)
				printLog(stdout, "High bidder: %s (NOT %s)\n",
					 winner, options.username);
			else
				printLog(stdout, "High bidder: %s\n", winner);
			aip->winning = 0;
			if (!aip->remain)
				aip->won = 0;
		} else if (aip->reserve) {
			printLog(stdout, "High bidder: %s (reserve not met)\n",
				 winner);
			aip->winning = 0;
			if (!aip->remain)
				aip->won = 0;
		} else {
			printLog(stdout, "High bidder: %s!!!\n", winner);
			aip->winning = 1;
			if (!aip->remain)
				aip->won = 1;
		}
		free(winner);
		break;
	    }
	default:
		if (checkPageType(aip, pageType, auctionState, auctionResult) != 0)
		{
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, optiontab, "%d columns in bid table", numColumns(row));
			ret = auctionError(aip, ae_nohighbid, NULL);
		}
		freeTableRow(row);
	}

	return ret;
} /* parseBidHistory() */

static long
getSeconds(char *timestr)
{
	static char second[] = "sec";
	static char minute[] = "min";
	static char hour[] = "hour";
	static char day[] = "day";
	static char ended[] = "ended";
	long accum = 0;
	long num;

	/* skip leading space */
	while (*timestr && isspace((int)*timestr))
		++timestr;

	/* Time is blank in transition between "Time left: 1 seconds" and
	 * "Time left: auction has ended".  I don't know if blank means
	 * the auction is over, or it still running with less than 1 second.
	 * I'll make the safer assumption and say that there is 1 second
	 * remaining.
	 * bomm: The transition seems to have changed to "--". I will accept
	 * any string starting with "--".
	 */
	if (!*timestr || !strncmp(timestr, "--", 2))
		return 1;
	if (strstr(timestr, ended))
		return 0;
	while (*timestr) {
		num = strtol(timestr, &timestr, 10);
		while (isspace((int)*timestr))
			++timestr;
		if (!strncmp(timestr, second, sizeof(second) - 1))
			return(accum + num);
		else if (!strncmp(timestr, minute, sizeof(minute) - 1))
			accum += num * 60;
		else if (!strncmp(timestr, hour, sizeof(hour) - 1))
			accum += num * 3600;
		else if (!strncmp(timestr, day, sizeof(day) - 1))
			accum += num * 86400;
		else
			return -1;
		while (*timestr && !isdigit((int)*timestr))
			++timestr;
	}

	return accum;
}
/* This function is called when we could not successfully parse the bids
 * or purchases table.
 *
 * If we can find out the result based on the auction state and result codes
 * the result will be written to the auctionInfo structure and the return
 * code will be 0.
 * If we cannot find out the result the return code will be != 0.
 *
 */
static int checkPageType(auctionInfo *aip, int pageType, int auctionState, int auctionResult)
{
	if((pageType == 0) || (auctionState == 0) || (auctionResult == 0)) return -1;

	switch(pageType)
	{
	case VIEWBIDS:
		switch(auctionState)
		{
		case STATE_ACTIVE:
			switch(auctionResult)
			{
			case RESULT_HIGH_BIDDER:
				/* assume we have bid on one item */
				aip->quantityBid = 1;
				aip->winning = 1;
				aip->won = 0;
				aip->quantity = 0;
				printLog(stdout, "High bidder: %s!!!\n", options.username);
				break;
			case RESULT_NONE:
				aip->quantityBid = 0;
				aip->winning = 0;
				aip->won = 0;
				aip->quantity = 0;
				printLog(stdout, "High bidder: (unknown) (NOT %s)\n",
						 options.username);
			default:
				return -1;
			}
		case STATE_CLOSED:
			switch(auctionResult)
			{
			case RESULT_HIGH_BIDDER:
				/* assume we have bid on one item */
				aip->quantityBid = 1;
				aip->winning = 1;
				aip->won = 1;
				aip->quantity = 1;
				printLog(stdout, "High bidder: %s!!!\n", options.username);
				break;
			case RESULT_NONE:
			case RESULT_OUTBID:
				aip->quantityBid = 0;
				aip->winning = 0;
				aip->won = 0;
				aip->quantity = 0;
				printLog(stdout, "High bidder: (unknown) (NOT %s)\n",
						 options.username);
				break;
			default:
				return -1;
			}
			break;
		default:
			/* unknown state */
			return -1;
		}
		break;
	case VIEWTRANSACTIONS:
		/* currently we cannot handle this other than parsing the table */
		return -1;
		break;
	default:
		return -1;
	}
	return 0;
}
