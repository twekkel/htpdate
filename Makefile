prefix = $(DESTDIR)/usr
bindir = ${prefix}/sbin
mandir = ${prefix}/share/man

CC     ?= gcc
CFLAGS += -Wall -std=c11 -pedantic -O2

INSTALL = /usr/bin/install -c
STRIP   = /usr/bin/strip -s

all: htpdate

htpdate: htpdate.c
	$(CC) $(CFLAGS) -o htpdate htpdate.c

install: all
	$(STRIP) htpdate
	mkdir -p $(bindir)
	$(INSTALL) -m 755 htpdate $(bindir)/htpdate
	mkdir -p $(mandir)/man8
	$(INSTALL) -m 644 htpdate.8 $(mandir)/man8/htpdate.8
	gzip -f -9 $(mandir)/man8/htpdate.8

test:
	./htpdate -v
	./htpdate www.example.com
	./htpdate www.example.com www.example.com:80/htpdate.html
	./htpdate -04q www.example.com/
	./htpdate -6q www.example.com
	./htpdate -t www.example.com
	./htpdate 93.184.216.34
	./htpdate 93.184.216.34:80
	./htpdate [2606:2800:220:1:248:1893:25c8:1946]
	./htpdate [2606:2800:220:1:248:1893:25c8:1946]:80
	./htpdate -h

clean:
	rm -rf htpdate

uninstall:
	rm -rf $(bindir)/htpdate
	rm -rf $(mandir)/man8/htpdate.8.gz
