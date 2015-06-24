#include "Socket.hpp"
#include "log/log.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

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
	if (pimpl)
		delete pimpl;
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

class Socket_pimpl
{
public:
	Socket_pimpl()
	{
		fd = -1;
		FD_ZERO(&readFdSet);
	}
	int fd;
	fd_set readFdSet;
};

Socket::Socket()
{
	pimpl = new Socket_pimpl();
}

Socket::Socket(int fd) : Socket()
{
	setSockFd(fd);
}

Socket::~Socket() {
	if (pimpl->fd != -1)
		close(pimpl->fd);
	if (pimpl)
		delete pimpl;
}

int
Socket::getSockFd()
{
	return pimpl->fd;
}

void
Socket::setSockFd(int fd)
{
	pimpl->fd = fd;
}

int
Socket::sendMsg(char* msg, int msglen)
{
	char * m = msg;
	int left = msglen;
	int bytes_sent = 0;
	do {
		ssize_t n = send(pimpl->fd, m, left, MSG_NOSIGNAL);

		/* Check to see if send was successful */
		if(n < 0) {
			xlog(LOG_ERR, "failed to send message to socket");
			xlog(LOG_ERR, "message: %s", msg);
			xlog(LOG_ERR, "ErrorCode: %s", strerror(errno));
			return n;
		} else {
			left -= n;
			m += n;
			bytes_sent += n;
		}
	} while (left > 0);

	return bytes_sent;
}

int
Socket::setupReadFdSet(SocketUnblocker * unblocker)
{
	int fd_max = -1;
	int fd;

	FD_ZERO(&pimpl->readFdSet);

	fd = pimpl->fd;
	if(fd < 0) {
		xlog(LOG_ERR, "Invalid socket fd (%d)", fd);
		throw "Invalid Socket";
	}
	fd_max = fd;
	FD_SET(fd, &pimpl->readFdSet);

	if (unblocker) {
		fd = unblocker->getReadPipe();
		if(fd < 0) {
			xlog(LOG_WARNING, "Invalid unblock read pipe fd (%d)", fd);
			throw "Invalid Unblocker";
		}
		FD_SET(fd, &pimpl->readFdSet);
		if (fd > fd_max)
			fd_max = fd;
	}

	return fd_max;

}

SocketReadStatus
Socket::recvReply(char * replyMsgBuf, int replyMsgLen, int msTimeout,
		  int *bytesReadOut, SocketUnblocker * unblocker)
{
	struct timeval timeout, *timeout_p;  /* Timeout for read */
	enum SocketReadStatus rv = SOCK_READ_OK;

	if (bytesReadOut == NULL)
		throw "recvReply given NULL bytesReadOut";

	do {
		int fdmax;
		int count;

		/*
		* Setup the Read file descriptor set that will be used in the select when
		* listening.
		*/
		fdmax = setupReadFdSet(unblocker);
		if (fdmax > 0) {
			/*
			* Setup the timeout, if timeout_usecs_in is 0 then set we need to pass
			* NULL to the select call so we will use a pointer to the timeval
			* structure.
			*/
			if(msTimeout == 0) {
				timeout_p = NULL;
			} else {
				timeout.tv_sec = msTimeout / 1000;
				timeout.tv_usec = (msTimeout * 1000) % 1000000;
				xlog(LOG_DEBUG, "timeout set to (%d sec %d usec)",
					timeout.tv_sec, timeout.tv_usec);
				timeout_p = &timeout;
			}

			count = select(
					fdmax+1,
					&pimpl->readFdSet,
					NULL,   /* No write set */
					NULL,   /* No exception set */
					timeout_p);  /* Block indefinitely */
			xlog(LOG_INFO, "select finished.");

			if(count == -1) {
				if (errno == EINTR) {
					/* Select system call was interrupted by a signal so restart
					 * the system call */
					xlog(LOG_WARNING, "Select system call interrupted, restarting");
					continue;
				}
				xlog(LOG_ERR, "select failed (%s)", strerror(errno));
				rv = SOCK_READ_ERROR;
			} else if(count == 0) {
				xlog(LOG_ERR, "select timedout");
				rv = SOCK_READ_TIMEOUT;
			} else {
				if(FD_ISSET(pimpl->fd, &pimpl->readFdSet)) {
					/* recv will return a 0 value if the peer has closed its halfside of
					 * the connection, < 0 if there was an error */
					*bytesReadOut = recv(pimpl->fd, replyMsgBuf, replyMsgLen, MSG_NOSIGNAL);
					assert(*bytesReadOut < replyMsgLen);
					if (*bytesReadOut > 0) {
						xlog(LOG_DEBUG, "Rx from Client: (n=%d)\n",
								*bytesReadOut);
					} else if (*bytesReadOut == 0) {
						/* Peer closed connection */
						xlog(LOG_NOTICE, "Client has closed connection\n");
						rv = SOCK_READ_CONN_CLOSED;
					} else {
						/* Error whilst reading from socket */
						xlog(LOG_ERR, "Socket read error (%s)",
							strerror(errno));
						rv = SOCK_READ_ERROR;
					}
					count--;
				}
				if(unblocker && FD_ISSET(unblocker->getReadPipe(),
						    &pimpl->readFdSet)
				) {
					char buf[1];
					/* Task has been closed, read byte from pipe and set state to closing. */
					xlog(LOG_INFO, " Listen has been unblocked.");

					if(read(unblocker->getReadPipe(), buf, 1) != 1) {
						xlog(LOG_ERR, "Failed to read unblock pipe.");
					}
					xlog(LOG_INFO, "Unblock pipe flushed.");
					rv = SOCK_READ_UNBLOCKED;
					count--;
				}

				if(count != 0) {
					/* Serious Problem. */
					if (rv == SOCK_READ_OK)
						rv = SOCK_READ_ERROR;
					xlog(LOG_ERR, "Listen Failure, unknown fd from select");
				}
			}
		} else {
			xlog(LOG_ERR, "Accept Failure, invalid fdmax value");
			rv =  SOCK_READ_ERROR;
		}
	} while(0);

	return rv;
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

int
SocketUnblocker::getReadPipe()
{
	return pimpl->unblockPipes[UBP_READ];
}

// _________________________________________________ SocketServer Implementation

class ServerSocket_pimpl {
public:
	ServerSocket_pimpl(int port) : port(port)
	{

	}
	int port;
};

//
ServerSocket::ServerSocket(int port) : Socket()
{
	int fd;

	pimpl = new ServerSocket_pimpl(port);
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		throw "ERROR opening socket";
	setSockFd(fd);
}

ServerSocket::~ServerSocket()
{
	if (pimpl)
		delete pimpl;
}


/* bind to the any address so we can accept connections to our loop back address
 * and our main IP address(es).
 */
void ServerSocket::bindAnyAddress()
{
	struct sockaddr_in addr;
	int rc;

	bzero((char *) &addr, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(pimpl->port);
	rc = bind(getSockFd(), (struct sockaddr *) &addr,
			sizeof(addr));
	if (rc < 0) {
		xlog(LOG_ERR, "Bind failed (%d:%s", errno, strerror(errno));
		throw "ERROR on binding";
	}
}

void ServerSocket::setPassive(int backlog)
{
	if (listen(getSockFd(), backlog) != 0)
		throw "ERROR on listen";
}
