/* Socket based Layer1 <-> Layer23 communication over L1CTL primitives. */

/* (C) 2016 Sebastian Stumpf
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/serial.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>

#include <arpa/inet.h>

#include <l1ctl_proto.h>

#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/logging.h>

#define L1CTL_SOCK_MSGB_SIZE	256

/**
 * @brief L1CTL socket file descriptor callback function.
 *
 * @param ofd The osmocom file descriptor.
 * @param what Indicates if the fd has a read, write or exception request. See select.h.
 *
 * Will be called by osmo_select_main() if data on fd is pending.
 */
static int l1ctl_sock_data_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct l1ctl_sock_inst *lsi = ofd->data;
	// Check if request is really read request
	if (what & BSC_FD_READ) {
		struct msgb *msg = msgb_alloc(L1CTL_SOCK_MSGB_SIZE,
		                "L1CTL sock rx");
		int rc;
		uint16_t len;
		struct l1ctl_hdr *l1h;
		// read length of the message first and convert to host byte order
		rc = read(ofd->fd, &len, sizeof(len));
		if (rc < sizeof(len)) {
			goto ERR;
		}
		// convert to host byte order
		len = ntohs(len);
		if (len <= 0 || len > L1CTL_SOCK_MSGB_SIZE) {
			goto ERR;
		}
		rc = read(ofd->fd, msgb_data(msg), len);

		if (rc == len) {
			msgb_put(msg, rc);
			l1h = msgb_data(msg);
			msg->l1h = l1h;
			lsi->recv_cb(lsi, msg);
			return 0;
		}
		ERR: perror(
		                "Failed to receive msg from l2. Connection will be closed.\n");
		l1ctl_sock_disconnect(lsi);
	}
	return 0;

}

static int l1ctl_sock_accept_cb(struct osmo_fd *ofd, unsigned int what)
{

	struct l1ctl_sock_inst *lsi = ofd->data;
	struct sockaddr_un local_addr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	int fd;

	fd = accept(ofd->fd, (struct sockaddr *)&local_addr, &addr_len);
	if (fd < 0) {
		fprintf(stderr, "Failed to accept connection to l2.\n");
		return -1;
	}

	lsi->connection.fd = fd;
	lsi->connection.when = BSC_FD_READ;
	lsi->connection.cb = l1ctl_sock_data_cb;
	lsi->connection.data = lsi;

	if (osmo_fd_register(&lsi->connection) != 0) {
		fprintf(stderr, "Failed to register the l2 connection fd.\n");
		return -1;
	}
	return 0;
}

struct l1ctl_sock_inst *l1ctl_sock_init(
                void *ctx,
                void (*recv_cb)(struct l1ctl_sock_inst *lsi, struct msgb *msg),
                char *path)
{
	struct l1ctl_sock_inst *lsi;
	struct sockaddr_un local_addr;
	int fd, rc;

	if (!path)
		path = L1CTL_SOCK_PATH;

	if ((fd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "Failed to create Unix Domain Socket.\n");
		return NULL;
	}

	local_addr.sun_family = AF_LOCAL;
	strcpy(local_addr.sun_path, path);
	unlink(local_addr.sun_path);

	if ((rc = bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)))
	                != 0) {
		fprintf(stderr, "Failed to bind the unix domain socket. '%s'\n",
		                local_addr.sun_path);
		return NULL;
	}

	if (listen(fd, 0) != 0) {
		fprintf(stderr, "Failed to listen.\n");
		return NULL;
	}

	lsi = talloc_zero(ctx, struct l1ctl_sock_inst);
	lsi->priv = NULL;
	lsi->recv_cb = recv_cb;
	lsi->ofd.data = lsi;
	lsi->ofd.fd = fd;
	lsi->ofd.when = BSC_FD_READ;
	lsi->ofd.cb = l1ctl_sock_accept_cb;
	// no connection -> invalid filedescriptor and not 0 (==std_in)
	lsi->connection.fd = -1;
	lsi->l1ctl_sock_path = path;

	osmo_fd_register(&lsi->ofd);

	return lsi;
}

void l1ctl_sock_destroy(struct l1ctl_sock_inst *lsi)
{
	struct osmo_fd *ofd = &lsi->ofd;

	osmo_fd_unregister(ofd);
	close(ofd->fd);
	ofd->fd = -1;
	ofd->when = 0;

	talloc_free(lsi);
}

void l1ctl_sock_disconnect(struct l1ctl_sock_inst *lsi)
{
	struct osmo_fd *ofd = &lsi->connection;
	osmo_fd_unregister(ofd);
	close(ofd->fd);
	ofd->fd = -1;
	ofd->when = 0;
}

int l1ctl_sock_write_msg(struct l1ctl_sock_inst *lsi, struct msgb *msg)
{
	int rc;
	rc = write(lsi->connection.fd, msgb_data(msg), msgb_length(msg));
	msgb_free(msg);
	return rc;
}
