# Makefile for miscellaneous stuff.

#
# check: fish for portability and obvious bugs by compiling with most
#	 of gcc's warning options enabled
#

SRC = auction.c auctionfile.c auctioninfo.c buffer.c esniper.c \
	history.c html.c http.c options.c util.c

# System dependencies
# HP-UX 10.20
#CFLAGS = -D_XOPEN_SOURCE_EXTENDED
# Digital UNIX (OSF1 V4.0)
#CFLAGS = -D_XOPEN_SOURCE_EXTENDED

# strict checking options
# Note: -O needed for uninitialized variable warning (part of -Wall)
#
# Flags not included:
#	-Wshadow -Wtraditional -Wid-clash-len -Wredundant-decls
CHECKFLAGS = -O -pedantic -Wall -Wpointer-arith -Wcast-qual \
	-Wcast-align -Wwrite-strings -Wconversion -Waggregate-return \
	-Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs

# Simple portability check - look for no warnings
check:
	gcc -c $(CFLAGS) -DVERSION=\"check\" `curl-config  --cflags` $(CHECKFLAGS) $(SRC)

# lint check
lint:
	lint $(CFLAGS) `curl-config  --cflags` $(SRC)



#
# configure: generate updated autotools files.
#
configure: Makefile.am configure.in
	automake -a
	aclocal
	autoconf



#
# esniper_man.html: generate new html-ized man file
#
esniper_man.html: esniper.1
	man2html esniper.1 >esniper_man.html
