/*
    htpdate v1.3.5

    Eddy Vervest <eddy@vervest.org>
    http://www.vervest.org/htp

    Synchronize local system with time offered by remote web servers

    This program works with the timestamps return by web servers,
    formatted as specified by HTTP/1.1 (RFC 2616, RFC 1123).

    Example usage:

    Debug mode (shows raw timestamps, round trip time (RTT) and
    time difference):

      htpdate -d www.example.com

    Adjust time smoothly:

      htpdate -a www.example.com

    ...see man page for more details


    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    http://www.gnu.org/copyleft/gpl.html
*/

/* Needed to avoid implicit warnings from strptime */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <syslog.h>
#include <stdarg.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <float.h>

#if defined __NetBSD__ || defined __FreeBSD__ || defined __APPLE__
#define adjtimex ntp_adjtime
#endif

#ifdef ENABLE_HTTPS
#include <openssl/ssl.h>
#endif

#define VERSION                  "1.3.5"
#define MAX_HTTP_HOSTS           16                /* 16 web servers */
#define DEFAULT_HTTP_PORT        "80"
#define DEFAULT_PROXY_PORT       "8080"
#define DEFAULT_IP_VERSION       PF_UNSPEC         /* IPv6 and IPv4 */
#define DEFAULT_HTTP_VERSION     "1"               /* HTTP/1.1 */
#define DEFAULT_TIME_LIMIT       31536000          /* 1 year */
#define NO_TIME_LIMIT            -1
#define ERR_TIMESTAMP            DBL_MAX          /* Err fetching date in getHTTPdate */
#define DEFAULT_PRECISION        4                 /* 4 request per host */
#define DEFAULT_MIN_SLEEP        1800              /* 30 minutes */
#define DEFAULT_MAX_SLEEP        115200            /* 32 hours */
#define MAX_DRIFT                32768000          /* 500 PPM */
#define DEFAULT_PID_FILE         "/var/run/htpdate.pid"
#define HEADREQUESTSIZE          1024
#define URLSIZE                  128
#define BUFFERSIZE               8192
#define PRINTBUFFERSIZE          BUFFERSIZE

#define sign(x) (x < 0 ? (-1) : 1)


/* By default turn off "debug" and "log" mode  */
static int debug   = 0;
static int logmode = 0;
static int verifycert = 0;


/* Insertion sort is more efficient (and smaller) than qsort for small lists */
static void insertsort(double a[], int length) {
    long i, j;

    for (i = 1; i < length; i++) {
        double value = a[i];
        for (j = i - 1; j >= 0 && a[j] > value; j--)
            a[j+1] = a[j];
        a[j+1] = value;
    }
}


/* Printlog is a slighty modified version from the one used in rdate */
static void printlog(int is_error, char *format, ...) {
    va_list args;
    char buf[PRINTBUFFERSIZE];

    va_start(args, format);
    (void) vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (logmode)
        syslog(is_error?LOG_WARNING:LOG_INFO, "%s", buf);
    else
        fprintf(is_error?stderr:stdout, "%s\n", buf);
}


/* Split argument in hostname/IP-address and TCP port
   Supports IPv6 literal addresses, RFC 2732.
*/
static void splitURL(char **scheme, char **host, char **port, char **path) {
    char *rb, *rc, *lb, *lc, *ps;

    *path = "";
    *scheme = NULL;
    *port = DEFAULT_HTTP_PORT;

    if ((ps = strcasestr(*host, "https://")) != NULL) {
        #ifndef ENABLE_HTTPS
        printlog(1, "HTTPS not supported, %s", *host);
        return;
        #endif
        *scheme = "https://";
        *port = "443";
        *host = ps + 8;
    }

    if ((ps = strcasestr(*host, "http://")) != NULL) {
        *host = ps + 7;
    }

    lb = strchr(*host, '[');
    rb = strrchr(*host, ']');
    lc = strchr(*host, ':');
    rc = strrchr(*host, ':');
    ps = strchr(*host, '/');

    /* Extract URL path */
    if (ps != NULL) {
        ps[0] = '\0';
        *path = ps + 1;
    }

    /* A (literal) IPv6 address with portnumber */
    if (rb < rc && lb != NULL && rb != NULL) {
        rb[0] = '\0';
        *port = rc + 1;
        *host = lb + 1;
        return;
    }

    /* A (literal) IPv6 address without portnumber */
    if (rb != NULL && lb != NULL) {
        rb[0] = '\0';
        *host = lb + 1;
        return;
    }

    /* A IPv4 address or hostname with portnumber */
    if (rc != NULL && lc == rc) {
        rc[0] = '\0';
        *port = rc + 1;
        return;
    }
}


static void swuid(int id) {
    if (seteuid(id)) {
        printlog(1, "seteuid() %i", id);
        exit(1);
    }
}


static void swgid(int id) {
    if (setegid(id)) {
        printlog(1, "setegid() %i", id);
        exit(1);
    }
}


static long long getoffset(char remote_time[25]) {
    struct timeval  timevalue = {0, 0};
    struct timespec now;
    struct tm       tm;

    clock_gettime(CLOCK_REALTIME, &now);
    memset(&tm, 0, sizeof(struct tm));
    if (strptime(remote_time, "%d %b %Y %T", &tm) != NULL) {
        timevalue.tv_sec = timegm(&tm);
    } else {
        printlog(1, "unknown time format");
    }
    return now.tv_sec - timevalue.tv_sec;
}


static int sendHEAD(int server_s, char *headrequest, char *buffer) {
    int ret = send(server_s, headrequest, strlen(headrequest), 0);

    if (ret < 0) {
        printlog(1, "Error sending");
        return -1;
    }

    /* Receive data from the web server
       The return code from recv() is the number of bytes received
    */
    ret = recv(server_s, buffer, BUFFERSIZE - 1, 0) > 0;

    return ret;
}


#ifdef ENABLE_HTTPS
static int sendHEADTLS(SSL *conn, char *headrequest, char *buffer) {
    int ret = SSL_write(conn, headrequest, strlen(headrequest));

    if (ret < 0) {
        printlog(1, "Error sending: %i", ret);
        return -1;
    }

    /* Receive data from the web server
       The return code is the number of bytes received
    */
    ret = SSL_read(conn, buffer, BUFFERSIZE - 1) > 0;

    return ret;
}


static int proxyCONNECT(
    int server_s,
    char *host, char *port,
    char *proxy, char *proxyport,
    char *httpversion) {

    char buffer[BUFFERSIZE] = {'\0'};
    char connectrequest[URLSIZE] = {'\0'};

    snprintf(connectrequest, URLSIZE,
       "CONNECT %s:%s HTTP/1.%s\r\n\r\n", host, port, httpversion);

    send(server_s, connectrequest, strlen(connectrequest), 0);
    int ret = recv(server_s, buffer, BUFFERSIZE - 1, 0) > 0;
    if (strstr(buffer, " 200 ") == NULL) {
        printlog(1, "Proxy error: %s:%s\r\n%s", proxy, proxyport, buffer);
    }
    return ret;
}
#endif


static double getHTTPdate(
    char *scheme,
    char *host, char *port, char *path,
    char *proxy, char *proxyport,
    char *httpversion, int ipversion, int precision) {

    int                 server_s;
    int                 rc;
    int                 polls = 0;
    struct addrinfo     hints, *res;
    struct timespec     sleepspec, now;
    char                headrequest[HEADREQUESTSIZE] = {'\0'};
    char                buffer[BUFFERSIZE] = {'\0'};
    char                url[URLSIZE] = {'\0'};
    char                *pdate = NULL;

    /* Connect to web server via proxy server or directly */
    memset(&hints, 0, sizeof(hints));
    switch(ipversion) {
        case 4:                     /* IPv4 only */
            hints.ai_family = AF_INET;
            break;
        case 6:                     /* IPv6 only */
            hints.ai_family = AF_INET6;
            break;
        default:                    /* Support IPv6 and IPv4 name resolution */
            hints.ai_family = PF_UNSPEC;
    }
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if (proxy == NULL) {
        rc = getaddrinfo(host, port, &hints, &res);
    } else {
        snprintf(url, URLSIZE, "http://%s:%s", host, port);
        rc = getaddrinfo(proxy, proxyport, &hints, &res);
    }

    /* Was the hostname and service resolvable? */
    if (rc) {
        printlog(1, "%s host or service unavailable", host);
        return(ERR_TIMESTAMP);
    }

    /* Build a combined HTTP/1.0 and 1.1 HEAD request
       Pragma: no-cache, "forces" an HTTP/1.0 and 1.1 compliant
       web server to return a fresh timestamp
       Connection: keep-alive, for multiple requests
    */
    snprintf(headrequest, HEADREQUESTSIZE,
        "HEAD %s/%s HTTP/1.%s\r\n"
        "Host: %s\r\n"
        "User-Agent: htpdate/"VERSION"\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n\r\n",
        url, path, httpversion, host);

    /* Loop through the available canonical names */
    do {
        server_s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (server_s < 0) {
            continue;
        }

        rc = connect(server_s, res->ai_addr, res->ai_addrlen);
        if (rc) {
            close(server_s);
            server_s = -1;
            continue;
        }

        break;
    } while ((res = res->ai_next));

    freeaddrinfo(res);

    if (rc) {
        printlog(1, "%s connection failed", host);
        return(ERR_TIMESTAMP);
    }

    #ifdef ENABLE_HTTPS
    SSL_CTX *tls_ctx = SSL_CTX_new(TLS_method());
    SSL_CTX_set_default_verify_paths(tls_ctx);
    SSL_CTX_set_verify_depth(tls_ctx, 4);
    if (verifycert) SSL_CTX_set_verify(tls_ctx, SSL_VERIFY_PEER, NULL);
    SSL *conn = SSL_new(tls_ctx);
    if (scheme) {
        if (proxy) {
            rc = proxyCONNECT(server_s, host, port, proxy, proxyport, httpversion);
            if (rc != 1) {
                printlog(1, "Proxy error: %i", rc);
                return(ERR_TIMESTAMP);
            }
        }
        if (! SSL_set_fd(conn, server_s)) {
            printlog(1, "TLS error1");
            return(ERR_TIMESTAMP);
        } else {
           if (SSL_connect(conn) != 1) {
               printlog(1, "TLS error2");
               return(ERR_TIMESTAMP);
           }
        }
    }
    #endif

    long long offset = 0, first_offset = 0, prev_offset = 0;
    long nap = 1e9L;
    long when = nap >> precision;
    do {
        if (debug > 1)
            printlog(0, "bisect: %i, when: %09li", polls, when);

        /* Initialize timer */
        clock_gettime(CLOCK_REALTIME, &now);

        /* Initialize RTT (start of measurement) */
        long rtt = now.tv_sec;

        /* Wait till we reach the desired time, "when" */
        sleepspec.tv_sec = 0;
        if (when >= now.tv_nsec) {
            sleepspec.tv_nsec = when - now.tv_nsec;
        } else {
            sleepspec.tv_nsec = 1e9 + when - now.tv_nsec;
            rtt++;
        }
        nanosleep(&sleepspec, NULL);

        /* Send HEAD request */
        #ifdef ENABLE_HTTPS
        if (scheme)
            rc = sendHEADTLS(conn, headrequest, buffer);
        else
        #endif
            rc = sendHEAD(server_s, headrequest, buffer);

        if (!rc) {
            printlog(1, "error from %s:%s", host, port );
            offset = LLONG_MAX;
            break;
        } else {
            clock_gettime(CLOCK_REALTIME, &now);

            /* rtt contains round trip time in nanoseconds */
            rtt = (now.tv_sec - rtt) * 1e9 + now.tv_nsec - when;

            /* Look for the line that contains [dD]ate: */
            if ((pdate = strcasestr(buffer, "date: ")) != NULL && strlen(pdate) >= 35) {

                if (debug > 2) printlog(0, "%s", buffer);
                char remote_time[25] = {'\0'};
                strncpy(remote_time, pdate + 11, 24);

                polls++;
                offset = getoffset(remote_time);

                nap >>= 1;
                if (polls > 1) {
                    if (offset != prev_offset) nap = -nap;
                } else {
                    first_offset=offset;
                }
                prev_offset=offset;
                /* Print host, raw timestamp, round trip time */
                if (debug)
                    printlog(0, "%-25s %s, %s (%li ms) => %li", host, port,
                    remote_time, rtt / (long)1e6, offset);
            } else {
                printlog(1, "%s no timestamp", host);
                offset = LLONG_MAX;
                break;
            }
        }                           /* bytes received */
        precision--;
        when += nap;
    } while (precision >= 1);
    close(server_s);

    #ifdef ENABLE_HTTPS
    if (scheme) SSL_shutdown(conn);
    SSL_CTX_free(tls_ctx);
    SSL_free(conn);
    #endif

    /* Rounding */
    if (debug) printlog(0, "when: %ld, nap: %ld", when, nap);
    if (offset == LLONG_MAX) return(ERR_TIMESTAMP);
    if (when + nap == 1e9 && offset == 0) return 0;

    /* Return the time delta between web server time (timevalue)
       and system time (now)
    */
    if (first_offset < 0) {
        return(-first_offset + (1e9L-when)/(double)1e9L);
    } else {
        return(-first_offset + 1 - when/(double)1e9L);
    }
}


static int setstatus(int precision) {
    struct timex txc = {0};

    txc.modes = MOD_STATUS | (1000000 >> precision) | MOD_MAXERROR;
    txc.status &= ~STA_UNSYNC;
    printlog(0, "Set clock synchronized");

    return(adjtimex(&txc));
}


static int setclock(double timedelta, int setmode) {
    struct timespec now;
    struct timeval  timeofday;
    char   buffer[32] = {'\0'};

    if (timedelta == 0) {
        printlog(0, "No time correction needed");
        return(0);
    }

    switch (setmode) {
        case 0:                        /* No time adjustment, just print time */
            printlog(0, "Offset %.3f seconds", timedelta);
            return(0);
        case 1:                        /* Adjust time smoothly */
            timeofday.tv_sec  = (long)timedelta;
            timeofday.tv_usec = (long)((timedelta - timeofday.tv_sec) * 1e6);

            printlog(0, "Adjusting %.3f seconds", timedelta);

            /* Become root */
            swuid(0);
            return(adjtime(&timeofday, NULL));
        case 2:                        /* Set time */
            printlog(0, "Setting %.3f seconds", timedelta);

            clock_gettime(CLOCK_REALTIME, &now);
            timedelta += (now.tv_sec + now.tv_nsec * 1e-9);

            now.tv_sec  = (long)timedelta;
            now.tv_nsec = (long)(timedelta - now.tv_sec) * 1e9;

            strftime(buffer, sizeof(buffer), "%c", localtime(&now.tv_sec));
            printlog(0, "Set time: %s", buffer);

            /* Become root */
            swuid(0);
            return(clock_settime(CLOCK_REALTIME, &now));
        case 3:                        /* Set frequency, but first an adjust */
            return(setclock(timedelta, 1));
        default:
            return(-1);
    }
}


static int init_frequency(char *driftfile) {
    struct timex    tmx;
    FILE            *fp;

    fp = fopen(driftfile, "r");
    if (fp != NULL) {
        if (fscanf(fp, "%li", &tmx.freq)) {
            printlog(0, "Reading frequency from file %li", tmx.freq);
            fclose(fp);
        } else {
            printlog(1, "Error reading frequency from %s", driftfile);
            fclose(fp);
            return -1;
        }
    } else {
        printlog(1, "Error reading frequency from %s", driftfile);
        return -1;
    }

    if ((tmx.freq < -MAX_DRIFT) || (tmx.freq > MAX_DRIFT))
        tmx.freq = sign(tmx.freq) * MAX_DRIFT;

    printlog(0, "Set frequency: %li", tmx.freq);
    tmx.modes = MOD_FREQUENCY;

    /* Become root */
    swuid(0);
    return(adjtimex(&tmx));
}


static int htpdate_adjtimex(double drift, char *driftfile, float confidence) {
    struct timex    tmx;
    long            freq;
    FILE            *fp;

    /* Read current clock frequency */
    tmx.modes = 0;
    adjtimex(&tmx);

    /* Calculate new frequency */
    freq = (long)(65536e6 * drift);

    /* Weighted average of current and new frequency */
    tmx.freq = tmx.freq + freq * confidence;
    if ((tmx.freq < -MAX_DRIFT) || (tmx.freq > MAX_DRIFT))
        tmx.freq = sign(tmx.freq) * MAX_DRIFT;

    printlog(0, "Set frequency %li", tmx.freq);
    tmx.modes = MOD_FREQUENCY;

    if (driftfile) {
       fp = fopen(driftfile, "w");
       if (fp != NULL) {
           printlog(0, "Update %s", driftfile);
           fprintf(fp, "%li", tmx.freq);
           fclose(fp);
       } else {
           printlog(1, "Error writing frequency to %s", driftfile);
       }
    }

    /* Become root */
    swuid(0);
    return(adjtimex(&tmx));
}


static void showhelp() {
    puts("htpdate version "VERSION"\n\
Usage: htpdate [-046acdhlnqstvxDF] [-f driftfile] [-i pidfile] [-m minpoll]\n\
         [-M maxpoll] [-p precision] [-P <proxyserver>[:port]]\n\
         [-u user[:group]] <URL> ...\n\n\
  -0    HTTP/1.0 request\n\
  -4    Force IPv4 name resolution only\n\
  -6    Force IPv6 name resolution only\n\
  -a    adjust time smoothly\n\
  -c    verify server certificate\n\
  -d    debug mode\n\
  -D    daemon mode\n\
  -f    drift/frequency file\n\
  -F    run daemon in foreground\n\
  -h    help\n\
  -i    pidfile\n\
  -l    use syslog for output\n\
  -m    minimum poll interval\n\
  -M    maximum poll interval\n\
  -n    no proxy (ignore http_proxy environment variable)\n\
  -p    precision (1..9, default 4)\n\
  -P    proxy server\n\
  -q    query only, don't make time changes (default)\n\
  -s    set time\n\
  -t    turn off sanity time check\n\
  -u    run daemon as user\n\
  -v    version\n\
  -x    adjust system clock frequency\n\
  URL   one of more URLs (max. 16), e.g. www.example.com\n");

    return;
}


/* Run htpdate in daemon mode */
static void runasdaemon(char *pidfile) {
    FILE  *pid_file;
    pid_t pid;

    /* Check if htpdate is already running (pid exists)*/
    pid_file = fopen(pidfile, "r");
    if (pid_file) {
        fputs("htpdate already running\n", stderr);
        exit(1);
    }

    pid = fork();
    if (pid < 0) {
        fputs("fork()\n", stderr);
        exit(1);
    }

    if (pid > 0) exit(0);

    /* Create a new SID for the child process */
    if (setsid () < 0) exit(1);

    /* Close out the standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    signal(SIGHUP, SIG_IGN);

    /* Change the file mode mask */
    umask(0);

    /* Change the current working directory */
    if (chdir("/") < 0) {
        printlog(1, "chdir()");
        exit(1);
    }

    /* Second fork, to become the grandchild */
    pid = fork();
    if (pid < 0) {
        printlog(1, "fork()");
        exit(1);
    }

    if (pid > 0) {
        /* Write a pid file */
        pid_file = fopen(pidfile, "w");
        if (!pid_file) {
            printlog(1, "Error writing pid file");
            exit(1);
        } else {
            fprintf(pid_file, "%d\n", pid);
            fclose(pid_file);
        }
        exit(0);
    }
}


int main(int argc, char *argv[]) {
    char            *host = NULL, *proxy = NULL, *proxyport = NULL;
    char            *port = NULL;
    char            *path = NULL;
    char            *scheme = NULL;
    char            *httpversion = DEFAULT_HTTP_VERSION;
    char            *pidfile = DEFAULT_PID_FILE;
    char            *user = NULL, *userstr = NULL, *group = NULL;
    double          timeavg, drift = 0;
    double          timedelta[MAX_HTTP_HOSTS-1];
    int             numservers;
    int             precision = DEFAULT_PRECISION;
    int             setmode = 0;
    int             i, param;
    int             daemonize = 0, foreground = 0;
    int             noproxyenv = 0;
    int             ipversion = DEFAULT_IP_VERSION;
    long long       timelimit = DEFAULT_TIME_LIMIT;
    int             minsleep = DEFAULT_MIN_SLEEP;
    int             maxsleep = DEFAULT_MAX_SLEEP;
    int             sleeptime = minsleep;
    int             sw_uid = 0, sw_gid = 0;
    time_t          starttime = 0;

    struct passwd   *pw;
    struct group    *gr;

    extern char     *optarg;
    extern int      optind;

    char            *driftfile = NULL;

    /* Parse the command line switches and arguments */
    while ((param = getopt(argc, argv, "046acdf:hi:lm:np:qstu:vxDFM:P:")) != -1)
    switch(param) {
        case '0':               /* HTTP/1.0 */
            httpversion = "0";
            break;
        case '4':               /* IPv4 only */
            ipversion = 4;
            break;
        case '6':               /* IPv6 only */
            ipversion = 6;
            break;
        case 'a':               /* adjust time */
            setmode = 1;
            break;
        case 'c':               /* server certificat verification */
            verifycert = 1;
            break;
        case 'd':               /* turn debug on */
            if (debug <= 3) debug++;
            break;
        case 'f':               /* drift file */
            driftfile = (char *)optarg;
            init_frequency(driftfile);
            break;
        case 'h':               /* show help */
            showhelp();
            exit(0);
        case 'i':               /* pid file */
            pidfile = (char *)optarg;
            break;
        case 'l':               /* log mode */
            logmode = 1;
            break;
        case 'm':               /* minimum poll interval */
            if ((minsleep = atoi(optarg)) <= 0) {
                fputs("Invalid sleep time\n", stderr);
                exit(1);
            }
            sleeptime = minsleep;
            break;
        case 'n':               /* don't get proxy from environment */
            noproxyenv = 1;
            break;
        case 'p':               /* precision */
            precision = atoi(optarg) ;
            if ((precision < 1) || (precision > 9)) {
                fputs("Invalid precision\n", stderr);
                exit(1);
            }
            break;
        case 'q':               /* query only (default) */
            break;
        case 's':               /* set time */
            setmode = 2;
            break;
        case 't':               /* disable "sanity" time check */
            timelimit = NO_TIME_LIMIT;
            break;
        case 'u':               /* drop root privileges and run as user */
            user = (char *)optarg;
            userstr = strchr(user, ':');
            if (userstr != NULL) {
                userstr[0] = '\0';
                group = userstr + 1;
            }
            if ((pw = getpwnam(user)) != NULL) {
                sw_uid = pw->pw_uid;
                sw_gid = pw->pw_gid;
            } else {
                printf("Unknown user %s\n", user);
                exit(1);
            }
            if (group != NULL) {
                if ((gr = getgrnam(group)) != NULL) {
                    sw_gid = gr->gr_gid;
                } else {
                    printf("Unknown group %s\n", group);
                    exit(1);
                }
            }
            break;
        case 'v':               /* print version */
            printf("htpdate version %s\n", VERSION);
            exit(0);
        case 'x':               /* adjust time and clock frequency */
            setmode = 3;
            if (maxsleep > 14400) maxsleep = 14400;
            if (precision < 7) precision = 7;
            break;
        case 'D':               /* run as daemon */
            daemonize = 1;
            logmode = 1;
            break;
        case 'F':               /* run daemon in foreground, don't fork */
            foreground = 1;
            break;
        case 'M':               /* maximum poll interval */
            if ((maxsleep = atoi(optarg)) <= 0) {
                fputs("Invalid sleep time\n", stderr);
                exit(1);
            }
            break;
        case 'P':
            proxy = (char *)optarg;
            proxyport = DEFAULT_PROXY_PORT;
            splitURL(&scheme, &proxy, &proxyport, &path);
            break;
        case '?':
            return 1;
        default:
            abort();
    }

    /* Display help page, if no servers are specified */
    if (argv[optind] == NULL) {
        showhelp();
        exit(1);
    }

    /* Exit if too many servers are specified */
    numservers = argc - optind;
    if (numservers > MAX_HTTP_HOSTS) {
        fputs("Too many servers\n", stderr);
        exit(1);
    }

    /* Use http_proxy environment variable */
    if (getenv("http_proxy") && !noproxyenv) {
        if ((proxy = strstr(getenv("http_proxy"), "http://")) == NULL) {
            printlog(1, "Invalid proxy specified: %s", getenv("http_proxy"));
            exit(1);
        }
        if (debug) printlog(0, "Proxy: %s", proxy);
        proxy += 7;
        splitURL(&scheme, &proxy, &proxyport, &path);
    }

    /* One must be "root" to change the system time */
    if ((getuid() != 0) && (setmode || daemonize || foreground)) {
        fputs("Only root can change time\n", stderr);
        exit(1);
    }

    /* Run as a daemonize when -D is set */
    if (daemonize) {
        runasdaemon(pidfile);
    }

    /* Query only mode doesn't exist in daemon or foreground mode */
    if (daemonize || foreground) {
        printlog(0, "htpdate version "VERSION" started");
        if (!setmode) setmode = 1;
    }

    /* Now we are root, we drop the privileges (if specified) */
    if (sw_gid) swgid(sw_gid);
    if (sw_uid) swuid(sw_uid);

    #ifdef ENABLE_HTTPS
    SSL_library_init();
    #endif

    /* Infinite poll cycle loop in daemonize or foreground mode */
    do {

        /* Initialize number of received valid timestamps, good timestamps
           and the average of the good timestamps
        */
        int    validtimes = 0, goodtimes = 0;
        double sumtimes = 0, mean = 0;

        /* Loop through the time sources (web servers); poll cycle */
        for (i = optind; i < argc; i++) {

            /* host:port is stored in argv[i] */
            char *hostport = strdup(argv[i]);
            host = hostport;
            splitURL(&scheme, &host, &port, &path);

            double offset = getHTTPdate(scheme, host, port, path,
                proxy, proxyport, httpversion, ipversion, precision);
            free(hostport);
            if (debug && offset != ERR_TIMESTAMP) {
                printlog(0, "offset: %.6f", offset);
            }

            /* Only include valid responses in timedelta[] */
            if ((timelimit == NO_TIME_LIMIT && offset != ERR_TIMESTAMP) || (offset < timelimit && offset > -timelimit)) {
                timedelta[validtimes] = offset;
                validtimes++;
            }
        }

        /* Sort the timedelta results */
        insertsort(timedelta, validtimes);

        /* Mean time value */
        mean = timedelta[validtimes/2];

        /* Filter out the bogus timevalues. A timedelta which is more than
           1 second off from mean, is considered a 'false ticker'.
           NTP synced web servers can never be more off than a second.
        */
        for (i = 0; i < validtimes; i++) {
            if ((timedelta[i]-mean) < .5 && (timedelta[i]-mean) > -.5) {
                sumtimes += timedelta[i];
                goodtimes++;
            }
        }

        /* Check if we have at least one valid response */
        if (goodtimes) {

            timeavg = sumtimes / goodtimes;

            if (debug > 1)
                printlog(0, "#: %d, mean: %.3f, average: %.3f", goodtimes, mean, timeavg);

            /* Do I really need to change the time?  */
            if (sumtimes || !(daemonize || foreground)) {
                if (setclock(timeavg, setmode) < 0)
                    printlog(1, "Time change failed");

                /* Drop root privileges again */
                if (sw_uid) swuid(sw_uid);

                if (daemonize || foreground) {
                    if (starttime) {
                        /* Calculate systematic clock drift */
                        drift = timeavg / (time(NULL) - starttime);
                        printlog(0, "Drift %.2f PPM, %.2f s/day", drift*1e6, drift*86400);

                        /* Adjust system clock */
                        if (setmode == 3) {
                            starttime = time(NULL);
                            /* Adjust the clock frequency */
                            if (htpdate_adjtimex(drift, driftfile, (float)sleeptime / (float)maxsleep) < 0)
                                printlog(1, "Frequency change failed");

                            /* Drop root privileges again */
                            if (sw_uid) swuid(sw_uid);
                        }
                    } else {
                        starttime = time(NULL);
                    }

                    /* Decrease polling interval to minimum */
                    sleeptime = minsleep;

                    /* Sleep for 30 minutes after a time adjust or set */
                    sleep(DEFAULT_MIN_SLEEP);
                }
            } else {
                /* Increase polling interval */
                if (sleeptime < maxsleep) sleeptime <<= 1;
                if (setmode == 3) setstatus(precision);
            }

            if (daemonize || foreground) {
                printlog(0, "sleep for %ld s", sleeptime);
                sleep(sleeptime);
            }

            /* After first successful poll cycle do not step through time, only adjust */
            if (setmode != 3) setmode = 1;

        } else {
            printlog(1, "No server suitable for synchronization found");
            /* Sleep for minsleep to avoid flooding */
            if (daemonize || foreground)
                sleep(minsleep);
            else
                exit(1);
        }

    } while (daemonize || foreground);         /* end of infinite while loop */

    exit(0);
}
