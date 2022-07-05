/*
 * mreceive.c  -- Prints UDP messages received from a multicast group. 
 * 
 * (c)  Jianping Wang, Yvan Pointurier, Jorg Liebeherr, 2002
 *      Multimedia Networks Group, University of Virginia
 *
 * SOURCE CODE RELEASED TO THE PUBLIC DOMAIN
 * 
 * version 2.0 - 5/20/2002
 * version 2.1 - 12/4/2002
 *	Update version display. 
 * version 2.2 - 05/17/2003
 *      Assign default values to parameters . The usage information is 
 *      changed according to README_mreceive.txt
 * 
 * Based on this public domain program:
 * u_mctest.c            (c) Bob Quinn           2/4/97
 * 
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/errno.h>

#define TRUE 1
#define FALSE 0
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif
#define BUFSIZE   1500
#define TTL_VALUE 2
#define LOOPMAX   20
#define MAXIP     16

char *TEST_ADDR = "224.1.1.1";
char *TEST_ADDR_SRV = NULL;
int TEST_PORT = 4444;
unsigned long IP[MAXIP];
int NUM = 0;
int isExpectBinary = 0;
int outf = -1;
time_t runUntilTic = 0L;

void printHelp(void)
{
	fprintf(stderr, "mreceive version %s\n\
Usage: mreceive [-g GROUP] [-p PORT] [-i ADDRESS ] ... [-i ADDRESS] [-n]\n\
       mreceive [-v | -h]\n\
\n\
  -g GROUP[:PORT]        IP multicast group address to listen to.  Default: 224.1.1.1\n\
  -g SERVER@GROUP[:PORT] IP multicast group address and source to listen to (IGMPv3).\n\
  -p PORT                UDP port number used in the multicast packets.  Default: 4444\n\
  -i ADDRESS             IP addresses of one or more interfaces to listen for the given\n\
                         multicast group.  Default: the system default interface.\n\
  -n                     Interpret the contents of the message as a number instead of\n\
                         a string of characters.  Use this with `msend -n`\n\
  -b                     Expect binary contents: Display only the size of the received buffers every 10 secs\n\
  -o FILE                write received data to FILE, implies -b. use \"-\" for FILE to write to stdout\n\
  -t SEC                 run for SEC seconds (default 0 => forever)\n\
  -v                     Print version information.\n\
  -h                     Print the command usage.\n\n", VERSION);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in stLocal, stFrom;
	unsigned char receiveBuf[BUFSIZE + 1];
	int s, i;
	struct ip_mreq stMreq;
	struct ip_mreq_source stMreqSrc;
	int iTmp, iRet;
	int ipnum = 0;
	int ii;
	unsigned int numreceived;
	int rcvCountOld = 0;
	int rcvCountNew = 1;
	int starttime;
	int curtime;
	struct timeval tv;

	memset (receiveBuf, 0, sizeof (receiveBuf));
	memset (&stMreq, 0, sizeof (stMreq));
	memset (&stMreqSrc, 0, sizeof (stMreqSrc));

	if (argc < 2) {
		printHelp();
		return 1;
	}

	ii = 1;

	if ((argc == 2) && (strcmp(argv[ii], "-v") == 0)) {
		fprintf(stderr, "mreceive version 3.1/jr\n");
		return 0;
	}
	if ((argc == 2) && (strcmp(argv[ii], "-h") == 0)) {
		printHelp();
		return 0;
	}

	while (ii < argc) {
		if (strcmp(argv[ii], "-g") == 0) {
			ii++;
			if ((ii < argc) && !(strchr(argv[ii], '-'))) {
				TEST_ADDR = argv[ii];
				ii++;
			}
		} else if (strcmp(argv[ii], "-p") == 0) {
			ii++;
			if ((ii < argc) && !(strchr(argv[ii], '-'))) {
				TEST_PORT = atoi(argv[ii]);
				ii++;
			}
		} else if (strcmp(argv[ii], "-i") == 0) {
			ii++;
			if ((ii < argc) && !(strchr(argv[ii], '-'))) {
				IP[ipnum] = inet_addr(argv[ii]);
				ii++;
				ipnum++;
			}
		} else if (strcmp(argv[ii], "-n") == 0) {
			ii++;
			NUM = 1;
		} else if (strcmp(argv[ii], "-b") == 0) {
			ii++;
			isExpectBinary = 1;
		} else if (strcmp(argv[ii], "-o") == 0) {
			ii++;
			isExpectBinary = 1;
			NUM = 0;
			if ((ii < argc) && argv[ii][0]) {
				if (argv[ii][0] != '-') {
					outf = open(argv[ii], O_CREAT | O_TRUNC | O_WRONLY | O_NONBLOCK, 0666);
					if (outf < 0) {
						perror(argv[ii]);
						sleep(10);
						outf = open(argv[ii], O_CREAT | O_TRUNC | O_WRONLY | O_NONBLOCK, 0666);
					}
					if (outf < 0) {
						perror(argv[ii]);
						return 1;
					}
				} else {
					if (argv[ii][1] == 0) { // FILE "-" denotes stdout
						outf = dup(1);
						fcntl(outf, F_SETFL, O_NONBLOCK);
					} else {
						fprintf(stderr, "+++ parameter error: -o needs a file name or a single -\n");
						printHelp();
						return 9;
					}
				}
				ii++;
				ipnum++;
			}
		} else if (strcmp(argv[ii], "-t") == 0) {
			ii++;
			if ((ii < argc) && !(strchr(argv[ii], '-'))) {
				long runForSec = atol(argv[ii]);
				if (runForSec > 0) {
					runUntilTic = time(NULL) + runForSec;
				} else {
					runUntilTic = 0L;
				}
				ii++;
			}
		} else {
			fprintf(stderr, "wrong parameters!\n\n");
			printHelp();
			return 1;
		}
	}

	char *cp = strrchr(TEST_ADDR, ':');
	if (cp && *cp) { // extract PORT
		*cp++ = 0;
		if (*cp) {
			TEST_PORT = atoi(cp);
		}
		//exit(0);
	}

	cp = strchr(TEST_ADDR, '@');
	if (cp && *cp) { // subscribe to a specific multicast server in this group (IGMPv3)
		TEST_ADDR_SRV = TEST_ADDR;
		*cp++ = 0;
		if (*cp) {
			TEST_ADDR = cp;
			fprintf(stderr, "subscribe to server %s in group %s port %d\n", TEST_ADDR_SRV, TEST_ADDR, TEST_PORT);
		}
		//exit(0);
	}

	/* get a datagram socket */
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == INVALID_SOCKET) {
		fprintf(stderr, "socket() failed.\n");
		exit(1);
	}

	/* avoid EADDRINUSE error on bind() */
	iTmp = TRUE;
	iRet = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&iTmp, sizeof(iTmp));
	if (iRet == SOCKET_ERROR) {
		fprintf(stderr, "setsockopt() SO_REUSEADDR failed.\n");
		exit(1);
	}

	/* name the socket */
	stLocal.sin_family = AF_INET;
	stLocal.sin_addr.s_addr = htonl(INADDR_ANY);
	stLocal.sin_port = htons(TEST_PORT);
	iRet = bind(s, (struct sockaddr *)&stLocal, sizeof(stLocal));
	if (iRet == SOCKET_ERROR) {
		fprintf(stderr, "bind() failed.\n");
		exit(1);
	}

	/* join the multicast group. */
	if (!ipnum) {		/* single interface */
		stMreq.imr_multiaddr.s_addr = inet_addr(TEST_ADDR);
		stMreq.imr_interface.s_addr = INADDR_ANY;
	} else {
		for (i = 0; i < ipnum; i++) {
			stMreq.imr_multiaddr.s_addr = inet_addr(TEST_ADDR);
			stMreq.imr_interface.s_addr = IP[i];
		}
	}

	stMreqSrc.imr_multiaddr = stMreq.imr_multiaddr;
	stMreqSrc.imr_interface = stMreq.imr_interface;

	if (TEST_ADDR_SRV && *TEST_ADDR_SRV) {
		stMreqSrc.imr_sourceaddr.s_addr = inet_addr(TEST_ADDR_SRV);

		iRet = setsockopt(s, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, (char *)&stMreqSrc, sizeof(stMreqSrc));
		if (iRet == SOCKET_ERROR) {
			fprintf(stderr, "setsockopt() IP_ADD_SOURCE_MEMBERSHIP.\n");
			exit(1);
		}
	} else {
		iRet = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&stMreq, sizeof(stMreq));
		if (iRet == SOCKET_ERROR) {
			fprintf(stderr, "setsockopt() IP_ADD_MEMBERSHIP failed.\n");
			exit(1);
		}
	}

	// see https://man7.org/linux/man-pages/man7/ip.7.html for optnames
	/* set TTL to traverse up to multiple routers */
	iTmp = TTL_VALUE;
	iRet = setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&iTmp, sizeof(iTmp));
	if (iRet == SOCKET_ERROR) {
		fprintf(stderr, "setsockopt() IP_MULTICAST_TTL failed.\n");
		exit(1);
	}

	/* disable loopback */
	iTmp = FALSE;
	iRet = setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&iTmp, sizeof(iTmp));
	if (iRet == SOCKET_ERROR) {
		fprintf(stderr, "setsockopt() IP_MULTICAST_LOOP failed.\n");
		exit(1);
	}

	/* don't blindly receive all multicats messages, only the ones we're subscribed to */
	// see https://stackoverflow.com/questions/2741611/receiving-multiple-multicast-feeds-on-the-same-port-c-linux/2741989#2741989
	// and https://bugzilla.redhat.com/show_bug.cgi?id=231899 for a possibly other solution (by not binding the receiving socket to INADDR_ANY)
	iTmp = FALSE;
	iRet = setsockopt(s, IPPROTO_IP, IP_MULTICAST_ALL, (char *)&iTmp, sizeof(iTmp));
	if (iRet == SOCKET_ERROR) {
		fprintf(stderr, "setsockopt() IP_MULTICAST_ALL failed.\n");
		exit(1);
	}

	fprintf(stderr, "Now receiving from multicast group: %s@%s:%d\n", (TEST_ADDR_SRV ? TEST_ADDR_SRV : "*"), TEST_ADDR, TEST_PORT);

	if (setpriority(PRIO_PROCESS, 0, 0) != 0) {
		fprintf(stderr, "setpriority 0 failed.\n");
	} else if (setpriority(PRIO_PROCESS, 0, -1) != 0) {
		fprintf(stderr, "setpriority -1 failed. Must live with prio 0\n");
	}
	errno = 0;
	int curPrio = getpriority(PRIO_PROCESS, 0);
	if (errno == 0) fprintf(stderr, "running with process priority %d\n", curPrio);

	long sumReceivedBytes = 0L;
	long sumProgressReceivedBytes = 0L;
	time_t lastProgress = time(NULL);

	for (i = 0;; i++) {
		socklen_t addr_size = sizeof(stFrom);
		static int iCounter = 1;

		/* receive from the multicast address */
		iRet = recvfrom(s, receiveBuf, BUFSIZE, 0, (struct sockaddr *)&stFrom, &addr_size);
		if (iRet < 0) {
			if (outf > 0) {
				close(outf);
				outf = -1;
			}
			perror("recvfrom failed");
			fprintf(stderr, "received %ld\n", sumReceivedBytes);
			exit(1);
		}

		if (outf > 0) {
			write(outf, receiveBuf, iRet);
		}

		sumReceivedBytes += iRet;
		sumProgressReceivedBytes += iRet;
		time_t now = time(NULL);

		if (NUM) {
			gettimeofday(&tv, NULL);

			if (i == 0)
				starttime = tv.tv_sec * 1000000 + tv.tv_usec;
			curtime = tv.tv_sec * 1000000 + tv.tv_usec - starttime;
			numreceived = (unsigned int) receiveBuf[0] + ((unsigned int) (receiveBuf[1]) << 8) + ((unsigned int) (receiveBuf[2]) << 16) +
			              ((unsigned int) (receiveBuf[3]) >> 24);
			fprintf(stdout, "%5d\t%s:%5d\t%d.%03d\t%5d\n", iCounter, inet_ntoa(stFrom.sin_addr), ntohs(stFrom.sin_port),
			        curtime / 1000000, (curtime % 1000000) / 1000, numreceived);
			fflush(stdout);
			rcvCountNew = numreceived;
			if (rcvCountNew > rcvCountOld + 1) {
				if (rcvCountOld + 1 == rcvCountNew - 1)
					fprintf(stderr, "****************\nMessage not received: %d\n****************\n", rcvCountOld + 1);
				else
					fprintf(stderr, "****************\nMessages not received: %d to %d\n****************\n",
					       rcvCountOld + 1, rcvCountNew - 1);
			}
			if (rcvCountNew == rcvCountOld) {
				fprintf(stderr, "Duplicate message received: %d\n", rcvCountNew);
			}
			if (rcvCountNew < rcvCountOld) {
				fprintf(stderr, "****************\nGap detected: %d from %d\n****************\n", rcvCountNew, rcvCountOld);
			}
			rcvCountOld = rcvCountNew;
		} else if (isExpectBinary) {
			if (now >= lastProgress+10) {
				fprintf(stderr, "Receive msg %d %d bytes (total %ld) from %s:%d (%.3f KB/sec)\n",
				       iCounter, iRet, sumReceivedBytes, inet_ntoa(stFrom.sin_addr), ntohs(stFrom.sin_port),
				        ((double)sumProgressReceivedBytes / 1024.0) / (double)(now - lastProgress)
				);
				lastProgress = now;
				sumProgressReceivedBytes = 0L;
			}
		} else {
			fprintf(stderr, "Receive msg %d bytes from %s:%d: %s\n",
			        iCounter, inet_ntoa(stFrom.sin_addr), ntohs(stFrom.sin_port), receiveBuf);
		}
		iCounter++;

		if (runUntilTic && now > runUntilTic) break;
	}
	if (outf > 0) {
		close(outf);
		outf = -1;
	}

	return 0;
}				/* end main() */

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
