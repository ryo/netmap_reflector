#include <stdio.h>
#include <sysexits.h>
#include <poll.h>

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

static void
swapto(struct nm_desc *desc, struct netmap_slot *rxslot)
{
	struct netmap_ring *txring;
	int i;
	uint32_t t, cur;

	for (i = desc->first_tx_ring; i <= desc->last_tx_ring; i++) {
		txring = NETMAP_TXRING(desc->nifp, i);
		if (nm_ring_empty(txring))
			continue;

		cur = txring->cur;

		/* swap buf_idx */
		t = txring->slot[cur].buf_idx;
		txring->slot[cur].buf_idx = rxslot->buf_idx;
		rxslot->buf_idx = t;

		/* set len */
		txring->slot[cur].len = rxslot->len;
		if (txring->slot[cur].len < 64)
			txring->slot[cur].len = 64;

		/* update flags */
		txring->slot[cur].flags |= NS_BUF_CHANGED;
		rxslot->flags |= NS_BUF_CHANGED;

		/* update ring pointer */
		cur = nm_ring_next(txring, cur);
		txring->head = txring->cur = cur;

		break;
	}
}

int
main(int argc, char *argv[])
{
	unsigned int cur, n, i;
	struct netmap_ring *rxring;
	struct pollfd pollfd[1];
	struct nm_desc *nm_desc;
	char netmap_devname[128];


	if (argc != 2) {
		fprintf(stderr, "usage: netmap_reflector [interface]\n");
		exit(EX_USAGE);
	}

	snprintf(netmap_devname, sizeof(netmap_devname), "netmap:%s", argv[1]);
	nm_desc = nm_open(netmap_devname, NULL, 0, NULL);
	if (nm_desc == NULL) {
		fprintf(stderr, "cannot open %s\n", netmap_devname);
		exit(EX_IOERR);
	}

	for (;;) {
		pollfd[0].fd = nm_desc->fd;
		pollfd[0].events = POLLIN;
		poll(pollfd, 1, 500);

		/* from RXring to TXring */
		for (i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; i++) {
			rxring = NETMAP_RXRING(nm_desc->nifp, i);
			cur = rxring->cur;
			for (n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {
				swapto(nm_desc, &rxring->slot[cur]);
			}
			rxring->head = rxring->cur = cur;
		}
	}

	exit(EX_SOFTWARE);
}
