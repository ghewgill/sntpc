#include <arpa/inet.h>
#include <err.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define SECONDS_1900_1970 (25567 * 86400U)

#pragma pack(1)
struct ntp_packet_t {
    uint32_t flags;
    uint32_t root_delay;
    uint32_t root_dispersion;
    char reference_identifier[4];
    uint64_t reference_timestamp;
    uint32_t originate_timestamp_hi;
    uint32_t originate_timestamp_lo;
    uint64_t receive_timestamp;
    uint32_t transmit_timestamp_hi;
    uint32_t transmit_timestamp_lo;
    uint32_t key_identifier;
    unsigned char message_digest[16];
};
#pragma pack()

int backwards = 0;
int settime = 1;
int port = 123;
const char *server = "pool.ntp.org";
int threshold = 300;
int verbose = 0;

void usage()
{
    printf("Usage: sntpc [-bhnv] [-p port] [-s server] [-t threshold]\n");
    printf("\n");
    printf("        -b  Allow time shift backwards (default forward only)\n");
    printf("        -h  Show this help message\n");
    printf("        -n  No set time (dry run)\n");
    printf("        -p  Set server port number (default 123)\n");
    printf("        -s  Set server name or IPv4 address (default pool.ntp.org)\n");
    printf("        -t  Set maximum time offset threshold (default 300 seconds)\n");
    printf("        -v  Verbose (default silent)\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    if (sizeof(struct ntp_packet_t) != 68) {
        errx(1, "Structure size mismatch (got %lu, expected 68)", sizeof(struct ntp_packet_t));
    }

    if (pledge("stdio inet dns settime", NULL) < 0) {
        err(1, "pledge");
    }

    int ch;
    while ((ch = getopt(argc, argv, "bhnp:s:t:v")) != -1) {
        switch (ch) {
            case 'b':
                backwards = 1;
                break;
            case 'h':
                usage();
                break;
            case 'n':
                settime = 0;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 's':
                server = optarg;
                break;
            case 't':
                threshold = atoi(optarg);
                break;
            case 'v':
                verbose = 1;
                break;
        }
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        err(1, "socket");
    }

    in_addr_t addr = inet_addr(server);
    if (addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(server);
        if (he == NULL) {
            errx(1, "gethostbyname: %s", hstrerror(h_errno));
        }
        int n = 0;
        while (he->h_addr_list[n] != NULL) {
            n++;
        }
        addr = *(in_addr_t *)he->h_addr_list[arc4random() % n];
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = addr;
    if (verbose) {
        printf("sntpc: server %s address %s\n", server, inet_ntoa(sin.sin_addr));
    }
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        err(1, "connect");
    }

    struct ntp_packet_t request;
    bzero(&request, sizeof(request));
    request.flags = htonl((4 << 27) | (3 << 24));
    request.transmit_timestamp_hi = htonl(time(NULL) + SECONDS_1900_1970);
    request.transmit_timestamp_lo = arc4random();
    int tries = 0;
    for (;;) {
        if (send(sock, &request, sizeof(request), 0) < 0) {
            err(1, "send");
        }
        fd_set fds;
        FD_SET(sock, &fds);
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int r = select(sock+1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            err(1, "select");
        }
        if (r > 0) {
            break;
        }
        tries++;
        if (tries >= 3) {
            errx(1, "no response after 3 tries");
        }
    }

    struct ntp_packet_t reply;
    ssize_t n = recv(sock, &reply, sizeof(reply), 0);
    if (n < 0) {
        err(1, "recv");
    }
    if (n < (ssize_t)sizeof(reply) - 20) {
        errx(1, "short reply received (got %zd, expected %lu)", n, sizeof(reply) - 20);
    }

    if (((ntohl(reply.flags) & 0x07000000) >> 24) != 4) {
        errx(1, "unexpected reply mode from server");
    }
    int stratum = (ntohl(reply.flags) & 0x00ff0000) >> 16;
    if (stratum == 0) {
        errx(1, "got kiss-o-death message");
    }
    if (verbose) {
        printf("sntpc: server stratum %d\n", stratum);
    }
    if (abs((int32_t)ntohl(reply.root_delay)) >= 0x10000) {
        errx(1, "root delay exceeds 1 second");
    }
    if (abs((int32_t)ntohl(reply.root_dispersion)) >= 0x10000) {
        errx(1, "root dispersion exceeds 1 second");
    }

    if (reply.originate_timestamp_hi != request.transmit_timestamp_hi
     || reply.originate_timestamp_lo != request.transmit_timestamp_lo) {
        errx(1, "unexpected originate timestamp in reply packet");
    }

    uint32_t seconds_since_1900 = ntohl(reply.transmit_timestamp_hi);
    uint32_t seconds_since_1970 = seconds_since_1900 - SECONDS_1900_1970;
    if (verbose) {
        time_t t = seconds_since_1970;
        printf("sntpc: server timestamp %u (%.24s)\n", seconds_since_1970, ctime(&t));
    }

    time_t local_now = time(NULL);
    if (verbose) {
        printf("sntpc: local clock %lld (%.24s)\n", local_now, ctime(&local_now));
    }
    if (local_now > seconds_since_1970 && !backwards) {
        errx(1, "not stepping clock backwards (use -b to allow this)");
    }
    int delta = local_now - seconds_since_1970;
    if (verbose) {
        printf("sntpc: local clock offset is %d\n", delta);
    }
    if (abs(delta) > threshold) {
        errx(1, "clock absolute offset %d exceeds threshold %d", delta, threshold);
    }

    if (settime) {
        struct timeval new_clock;
        new_clock.tv_sec = seconds_since_1970;
        new_clock.tv_usec = 0;
        if (settimeofday(&new_clock, NULL) < 0) {
            err(1, "settimeofday");
        }
        if (verbose) {
            printf("sntpc: local clock set to %lld (%.24s)\n", new_clock.tv_sec, ctime(&new_clock.tv_sec));
        }
    } else {
        printf("sntpc: not setting clock because of -n\n");
    }

    close(sock);
}
