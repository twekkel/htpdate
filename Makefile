prefix = $(DESTDIR)/usr
bindir = ${prefix}/sbin
mandir = ${prefix}/share/man

CC     ?= gcc
CFLAGS += -Wall -std=c11 -pedantic -O2
PKG_CONFIG ?= pkg-config

INSTALL ?= install -c
STRIP   ?= strip -s

all: htpdate

htpdate: htpdate.c
	$(CC) $(CFLAGS) -o htpdate htpdate.c

https: htpdate.c
	$(CC) $(CFLAGS) -DENABLE_HTTPS -o htpdate htpdate.c  $(shell $(PKG_CONFIG) --libs libssl)

install: all
	$(STRIP) htpdate
	mkdir -p $(bindir)
	$(INSTALL) -m 755 htpdate $(bindir)/htpdate
	mkdir -p $(mandir)/man8
	$(INSTALL) -m 644 htpdate.8 $(mandir)/man8/htpdate.8
	gzip -f -9 $(mandir)/man8/htpdate.8

test:
	./htpdate -v
	./htpdate -p 1 www.example.com http://www.example.com https://example.com
	./htpdate -p 1 -d www.example.com www.example.com:80/htpdate.html
	./htpdate -p 2 -04q www.example.com/
	./htpdate -6q www.example.com
	./htpdate -t -dd www.example.com
	./htpdate -p1 https://93.184.216.34 https://93.184.216.34:443
	./htpdate -p1 93.184.216.34:80
	./htpdate -p1 [2606:2800:220:1:248:1893:25c8:1946]
	./htpdate -h

clean:
	rm -rf htpdate

uninstall:
	rm -rf $(bindir)/htpdate
	rm -rf $(mandir)/man8/htpdate.8.gz
