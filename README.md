# htpdate

[![main](https://github.com/twekkel/htpdate/actions/workflows/main.yml/badge.svg)](https://github.com/twekkel/htpdate/actions/workflows/main.yml)

### Time synchronization... quick and dirty

```
date -s "`curl --head -s https://example.com | grep -i "Date: " | cut -d' ' -f2-`"
```

### Better solution... htpdate

The above one-liner might result in unexpected behavior,
* site is not reachable
* site has wrong time
* steps/jumps (backwards!) in time

htpdate solves that by allowing multiple URLs as time source, eliminating
'false tickers', gradually adjusts time and run indefinitely as daemon.

The HTTP Time Protocol (HTP) is used to synchronize a computer's time
with web servers as reference time source. Htpdate will synchronize your
computer's time by extracting timestamps from HTTP headers found
in web server responses. Htpdate can be used as a daemon, to keep your
computer synchronized.
The accuracy of htpdate is at least -+0.5 seconds, but can be in the range
of ~10 ms (see -p option). If this is not good enough for you, use a ntp package.

Install the htpdate package if you need tools for keeping your system's
time synchronized via the HTP protocol. Htpdate works also through
proxy servers.

### Installation

build:
```
make
```
or for HTTPS support (OpenSSL is required)
```
make https
```
install:
```
make install
```

### Packages

Many Linux distributions and FreeBSD system provide a htpdate package, which should be preferred over manual compilation and installation from downloaded source code as the packages are likely integrated with the rest of the system (e.g. service files and networking scripts).
* [Arch Linux][https://aur.archlinux.org/packages/htpdate]
* [Debian][https://tracker.debian.org/pkg/htpdate]
* [Gentoo][https://packages.gentoo.org/packages/net-misc/htpdate]
* [Ubuntu][https://packages.ubuntu.com/search?suite=all&arch=any&searchon=names&keywords=htpdate]
* [FreeBSD][https://www.freshports.org/net/htpdate/]

### Usage

Htpdate can be used to query the time of one or multiple web servers,
```
htpdate www.example.com http://www.example.com https://example.com
```
Htpdate can run as daemon,
```
htpdate -D http://www.example.com
```
Another option is to run htpdate periodically from cron.
For a daily time synchronization,
```
5 3 * * * /usr/sbin/htpdate -a www.example.com
```
All htpdate options,

```
Usage: htpdate [-046acdhlnqstvxDF] [-f driftfile] [-i pidfile] [-m minpoll]
         [-M maxpoll] [-p precision] [-P <proxyserver>[:port]]
         [-u user[:group]] <URL> ...
```

See man page for more details.

### See also

* https://www.vervest.org/htp, home of HTTP Time Protocol
* https://www.ntppool.org/scores/ntp6.vervest.org, htpdate as part of NTP pool
* https://github.com/twekkel/httpdate, non daemon version using libcurl
* https://github.com/angeloc/htpdate, forked from htpdate 1.2.2
