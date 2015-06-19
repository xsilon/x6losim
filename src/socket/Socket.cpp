#include "Socket.hpp"
#include "log/log.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

// _________________________________________ Scoket Unblock Pipes Implementation

class SocketUnblocker_impl
{
public:
	SocketUnblocker_impl() {
		unblockPipes[UBP_READ] = -1;
		unblockPipes[UBP_WRITE] = -1;
	}
	int unblockPipes[2];
};

SocketUnblocker::SocketUnblocker() {
	pimpl = new SocketUnblocker_impl();
	this->openPipes();
}

SocketUnblocker::~SocketUnblocker() {
}

void
SocketUnblocker::openPipes() {
	int flags;
	int ret;

	/* Create pipe */
	ret = pipe(pimpl->unblockPipes);
	if(ret != 0) {
		xlog(LOG_ERR, "Failed to create pipe for unblocking server socket.");
		throw "ERROR: SocketUnblocker";
	}

	flags = fcntl(pimpl->unblockPipes[UBP_WRITE], F_GETFL, 0);
	if(flags == -1) {
		xlog(LOG_ERR, "get write unblock pipe flags failed (%s)", strerror(errno));
		throw "ERROR: SocketUnblocker";
	} else {
		flags |= O_NONBLOCK;
		ret = fcntl(pimpl->unblockPipes[UBP_WRITE], F_SETFL, flags);
		if (ret == -1) {
			xlog(LOG_ERR, "set write unblock pipe flags failed (%s)", strerror(errno));
			throw "ERROR: SocketUnblocker";
		}
	}
	flags = fcntl(pimpl->unblockPipes[UBP_WRITE], F_GETFD, 0);
	if(flags == -1) {
		xlog(LOG_ERR, "get write close on exec flags failed (%s)", strerror(errno));
		throw "ERROR: SocketUnblocker";
	} else {
		flags |= FD_CLOEXEC;
		ret = fcntl(pimpl->unblockPipes[UBP_WRITE], F_SETFD, flags);
		if (ret == -1) {
			xlog(LOG_ERR, "set write  close on exec flags failed (%s)", strerror(errno));
			throw "ERROR: SocketUnblocker";
		}
	}

	flags = fcntl(pimpl->unblockPipes[UBP_READ], F_GETFL, 0);
	if(flags == -1) {
		xlog(LOG_ERR, "get read unblock pipe flags failed (%s)", strerror(errno));
		throw "ERROR: SocketUnblocker";
	} else {
		flags |= O_NONBLOCK;
		ret = fcntl(pimpl->unblockPipes[UBP_READ], F_SETFL, flags);
		if (ret == -1) {
			xlog(LOG_ERR, "set read unblock pipe flags failed (%s)", strerror(errno));
			throw "ERROR: SocketUnblocker";
		}
	}
	flags = fcntl(pimpl->unblockPipes[UBP_READ], F_GETFD, 0);
	if(flags == -1) {
		xlog(LOG_ERR, "get read close on exec flags failed (%s)", strerror(errno));
		throw "ERROR: SocketUnblocker";
	} else {
		flags |= FD_CLOEXEC;
		ret = fcntl(pimpl->unblockPipes[UBP_READ], F_SETFD, flags);
		if (ret == -1) {
			xlog(LOG_ERR, "set read  close on exec flags failed (%s)", strerror(errno));
			throw "ERROR: SocketUnblocker";
		}
	}

	xlog(LOG_INFO, "Unblock pipes created WR:%d RD:%d",
			pimpl->unblockPipes[UBP_WRITE],
			pimpl->unblockPipes[UBP_READ]);
}

void
SocketUnblocker::unblock() {
	if(pimpl->unblockPipes[UBP_WRITE] >= 0) {
		int written;

		xlog(LOG_INFO, "Writing to unblock pipe.");
		/* First off we must try and gracefully stop the trap manager task before
		closing the session.  To achieve this we write to the close manager pipe. */
		written = write(pimpl->unblockPipes[UBP_WRITE], "1", 1);
		if(written != 1)
			xlog(LOG_ERR, "Failed to Write to unblock pipe %d (%d).",
				pimpl->unblockPipes[UBP_WRITE],
				written);
			if (written == -1)
				xlog(LOG_ERR, "%s", strerror(errno));
	} else {
		xlog(LOG_ERR, "Invalid write unblock pipe.");
	}
}

void
SocketUnblocker::closePipes()
{
	int rv;

	xlog(LOG_INFO, "Closing read unblock pipe (%d)", pimpl->unblockPipes[UBP_READ]);
	rv = close(pimpl->unblockPipes[UBP_READ]);
	pimpl->unblockPipes[UBP_READ] = -1;
	if(rv != 0)
		xlog(LOG_ERR, "Failed to close read unblock pipe (%s)",
		    strerror(errno));
	else
		xlog(LOG_INFO, "Closed read unblock pipe");

	xlog(LOG_INFO, "Closing write unblock pipe (%d)",
			pimpl->unblockPipes[UBP_WRITE]);
	rv = close(pimpl->unblockPipes[UBP_WRITE]);
	pimpl->unblockPipes[UBP_WRITE] = -1;
	if(rv != 0)
		xlog(LOG_ERR, "Failed to close write unblock pipe (%s)",
		    strerror(errno));
	else
		xlog(LOG_INFO, "Closed write unblock pipe");
}

// _______________________________________________________ Socket Implementation

class Socket_pimpl {
public:
	Socket_pimpl(int d) : domain(d)
	{
		fd = -1;
	}
	int fd;
	int domain;
};

Socket::Socket(int domain, int type, int protocol) {
	pimpl = new Socket_pimpl(domain);
	pimpl->fd = socket(domain, type, protocol);
	if (pimpl->fd == -1)
		throw "ERROR opening socket";
}
Socket::~Socket() {
	close(pimpl->fd);
	if (pimpl)
		delete pimpl;
}

int
Socket::getSockFd()
{
	return pimpl->fd;
}

int
Socket::setBlocking(bool blocking) {
	int flags;

	/* Sanity check on socket fd */
	if(pimpl->fd < 0) {
		xlog(LOG_ERR, "Invalid socket fd (%d)", pimpl->fd);
		return -1;
	}

	/* Attempt to set the client socket as NON blocking */
	flags = fcntl(pimpl->fd, F_GETFL, 0);
	if(flags == -1) {
		xlog(LOG_ERR, "set blocking failed (%s)", strerror(errno));
		return -1;
	} else {
		int rv;

		if(blocking) {
			flags &= ~O_NONBLOCK;
		} else {
			flags |= O_NONBLOCK;
		}
		rv = fcntl(pimpl->fd, F_SETFL, flags);

		return (rv == -1 ? -1 : 0);
	}
}

int
Socket::setCloseOnExec(bool closeOnExec) {
	int flags;

	/* Sanity check on socket fd */
	if(pimpl->fd < 0) {
		xlog(LOG_ERR, "Invalid socket fd (%d)", pimpl->fd);
		return -1;
	}

	/* Attempt to set the client socket as CLOSE on EXEC */
	flags = fcntl(pimpl->fd, F_GETFD, 0);
	if(flags == -1) {
		xlog(LOG_ERR, "set close on exec failed (%s)", strerror(errno));
		return -1;
	} else {
		int rv;

		if(closeOnExec) {
			flags |= FD_CLOEXEC;
		} else {
			flags &= ~FD_CLOEXEC;
		}
		rv = fcntl(pimpl->fd, F_SETFD, flags);

		return (rv == -1 ? -1 : 0);
	}
}

int
Socket::setReuseAddress(bool reuse) {
	int rv;
	int tr;

	/* Sanity check on socket fd */
	if(pimpl->fd < 0)
		throw "setReuseAddress: Invalid socket fd";

	tr = reuse ? 1 : 0;
	rv = setsockopt(pimpl->fd,
			SOL_SOCKET,
			SO_REUSEADDR,
			&tr,
			sizeof(int));

	return rv;
}

/* bind to the any address so we can accept connections to our loop back address
 * and our main IP address(es).
 */
void
Socket::bindAnyAddress(int port)
{
	struct sockaddr_in addr;
	int rc;

	bzero((char *) &addr, sizeof(addr));

	addr.sin_family = pimpl->domain;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);
	rc = bind(pimpl->fd, (struct sockaddr *) &addr,
			sizeof(addr));
	if (rc < 0) {
		xlog(LOG_ERR, "Bind failed (%d:%s", errno, strerror(errno));
		throw "ERROR on binding";
	}

}

void
Socket::setPassive(int backlog)
{
	if (listen(pimpl->fd, backlog) != 0)
		throw "ERROR on listen";
}

int
SocketUnblocker::getReadPipe()
{
	return pimpl->unblockPipes[UBP_READ];
}
