AM_CFLAGS = @CURLCFLAGS@
LDADD = @CURLLIBS@

bin_PROGRAMS = esniper
esniper_SOURCES = auction.c auctionfile.c auctioninfo.c buffer.c esniper.c \
		history.c html.c http.c options.c util.c \
		auction.h auctionfile.h auctioninfo.h buffer.h esniper.h history.h \
		html.h http.h options.h util.h

man_MANS = esniper.1

EXTRA_DIST = getopt.c sample_auction.txt sample_config.txt COPYRIGHT \
		ReleaseBuild  esniper.dsp esniper.dsw esniper.mak esniper_man.html \
		$(man_MANS)
