sntpc (Simple NTP Client)
=========================

This client queries an NTP server for the current time, and sets the local
clock to the time reported by the server.

This was built out of necessity for an OpenBSD client running inside vmm. Under
some conditions, the local clock runs slow enough that ntpd cannot adjust the
time fast enough with adjtime. The result is that the local clock loses time
and cannot recover.

Usage
-----

    sntpc [-bhnv] [-p port] [-s server] [-t threshold]

            -b  Allow time shift backwards (default forward only)
            -h  Show this help message
            -n  No set time (dry run)
            -p  Set server port number (default 123)
            -s  Set server name or IPv4 address (default pool.ntp.org)
            -t  Set maximum time offset threshold (default 300 seconds)
            -v  Verbose (default silent)

Set this up to run as `root` from a cron job, every 30 minutes or whatever is
necessary.

Things to do
------------

* IPv6 support
