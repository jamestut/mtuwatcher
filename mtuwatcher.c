#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>

uint8_t rtmsgbuff[sizeof(struct rt_msghdr) + sizeof(struct if_msghdr)];

int main(int argc, const char ** argv) {
	if (argc != 3) {
		puts("Usage: mtuwatcher (ifname) (target MTU)");
		return 1;
	}

	const char * target_ifname = argv[1];
	const u_int32_t target_mtu = atoi(argv[2]);

	if (target_mtu < 72) {
		errx(1, "Minimum MTU is 72.");
	} else if (target_mtu > 65535) {
		errx(1, "Maximum MTU is 65535.");
	}

	// check root
	if (getuid() != 0) {
		// try setuid
		if (setuid(0) < 0) {
			errx(1, "Error escalating permission to root. Either run this app as root"
				" or set setuid bit with root permission.");
		}
	}

	// get interface ID
	int ifidx = if_nametoindex(target_ifname);
	if (!ifidx) {
		err(1, "Error getting interface name");
	}

	// socket to monitor network interface changes
	int rtfd = socket(AF_ROUTE, SOCK_RAW, 0);
	if (rtfd < 0) {
		err(1, "Error creating AF_ROUTE socket");
	}
	if (fcntl(rtfd, F_SETFL, O_NONBLOCK) < 0) {
		err(1, "Error setting nonblock on AF_ROUTE socket");
	}

	// socket to perform ioctl to set interface flags
	int iocfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (iocfd < 0) {
		err(1, "Error creating AF_INET socket");
	}

	struct ifreq ifr = {0};
	strlcpy(ifr.ifr_name, target_ifname, IFNAMSIZ);

	// first startup: check and set MTU if necessary
	u_int32_t current_mtu;
	if (ioctl(iocfd, SIOCGIFMTU, &ifr) < 0) {
		err(1, "Error getting initial MTU value.");
	}
	if ((u_int32_t)ifr.ifr_mtu != target_mtu) {
		printf("Current interface MTU is %u. Setting MTU.\n", ifr.ifr_mtu);
		if (ioctl(iocfd, SIOCSIFMTU, &ifr) < 0) {
			err(1, "Error setting interface MTU");
		}
		ifr.ifr_mtu = target_mtu;
	}

	// watch for any if changes
	struct pollfd prt;
	prt.fd = rtfd;
	prt.events = POLLIN;
	for(;;) {
		if (poll(&prt, 1, -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			err(1, "Error polling AF_ROUTE socket");
		}

		// for all queued reads, we take the final MTU, decide from there
		// repeat forever until we're "blocked"
		for(ssize_t len = 0;;) {
			len = read(rtfd, rtmsgbuff, sizeof(rtmsgbuff));
			if (len < 0) {
				if (errno == EINTR) {
					continue;
				} else if (errno == EAGAIN) {
					break;
				}
				err(1, "Error reading AF_ROUTE socket");
			}

			struct rt_msghdr * rtmsg = (void *)rtmsgbuff;
			if (rtmsg->rtm_type != RTM_IFINFO) {
				continue;
			}

			struct if_msghdr * ifmsg = (void *)rtmsg;
			if (ifmsg->ifm_index != ifidx) {
				// not the interface that we want
				continue;
			}

			current_mtu = ifmsg->ifm_data.ifi_mtu;
		}

		if (current_mtu != target_mtu) {
			printf("Interface MTU changed to %u. Reverting back.\n", current_mtu);
			if (ioctl(iocfd, SIOCSIFMTU, &ifr) < 0) {
				err(1, "Error setting interface MTU");
			}
		}
	}
}
