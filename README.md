### Short

```
date -s "`curl --head -s https://example.com | grep -i "Date: " | cut -d' ' -f2-`"
```

### Solution... htpdate

The above one-liner might give unexpected results when,
* site is not reachable
* site has wrong time
* steps/jumps (backwards!) in time

htpdate solves that by allowing multiple URLs as time source, eliminating
'false tickers', gradually adjusts time and (optionally) run indefinitely
as daemon.

The HTTP Time Protocol (HTP) is used to synchronize a computer's time
with web servers as reference time source. Htpdate will synchronize your
computer's time by extracting timestamps from HTTP headers found
in web server responses. Htpdate can be used as a daemon, to keep your
computer synchronized.
The accuracy of htpdate is at least -+0.5 seconds (better with multiple
servers). If this is not good enough for you, try the ntpd package.

Install the htpdate package if you need tools for keeping your system's
time synchronized via the HTP protocol. Htpdate works also through
proxy servers.


### Installation

```
make
make install
```

Another option is to use htpdate in a cronjob and start it periodically
from cron. For a daily time sync it would look something like this:
5 3 * * * /usr/sbin/htpdate -a www.example.com


### Usage

Usage: htpdate [-046abdhlnqstxD] [-i pid file] [-m minpoll] [-M maxpoll]
	[-p precision] [-P <proxyserver>[:port]] [-u user[:group]]
	<host[:port][/path]> ...

	E.g. htpdate www.example.com example.com

In general, if more web servers are specified, the accuracy will increase.

See manpage for more details.

### See also

* https://www.vervest.org/htp, home of HTTP Time Protocol
* https://github.com/twekkel/httpdate, non daemon version using libcurl
* https://github.com/angeloc/htpdate, forked from 1.2.2 with HTTPS support
* http://www.rkeene.org/oss/htp/
