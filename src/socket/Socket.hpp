#ifndef _XSILON_SOCKET_HPP_
#define _XSILON_SOCKET_HPP_

#define UBP_READ				(0)
#define UBP_WRITE				(1)

enum SocketReadStatus
{
	SOCK_READ_CONN_CLOSED = 2,
	SOCK_READ_UNBLOCKED = 1,
	SOCK_READ_OK = 0,
	SOCK_READ_TIMEOUT = -1,
	SOCK_READ_ERROR = -2
};



class SocketUnblocker_impl;
class SocketUnblocker
{
public:
	SocketUnblocker();
	virtual ~SocketUnblocker();

	void unblock();
	int getReadPipe();

private:
	SocketUnblocker_impl * pimpl;

	/* Disable copy constructor and assigned operator */
	SocketUnblocker(SocketUnblocker const&) = delete;
	void operator=(SocketUnblocker const&) = delete;

	void
	openPipes();
	void
	closePipes();
};

class Socket_pimpl;
class Socket
{
public:
	Socket();
	Socket(int fd);
	virtual ~Socket();

	int
	getSockFd();
	void
	setSockFd(int fd);

	int
	sendMsg(char* msg, int msglen);
	SocketReadStatus
	recvReply(char * replyMsgBuf, int replyMsgLen, int msTimeout,
		  int *bytesReadOut, SocketUnblocker * unblocker);

	int
	setBlocking(bool blocking);
	int
	setCloseOnExec(bool closeOnExec);
	int
	setReuseAddress(bool reuse);

private:
	Socket_pimpl * pimpl;

	/* Disable copy constructor and assigned operator */
	Socket(Socket const&) = delete;
	void operator=(Socket const&) = delete;

	int
	setupReadFdSet(SocketUnblocker * unblocker);
};

class ServerSocket_pimpl;
class ServerSocket : public Socket
{
public:
	ServerSocket(int port);
	virtual ~ServerSocket();

	void
	bindAnyAddress();
	void
	setPassive(int backlog);

private:
	ServerSocket_pimpl * pimpl;

	/* Disable copy constructor and assigned operator */
	ServerSocket(ServerSocket const&) = delete;
	void operator=(ServerSocket const&) = delete;
};


#endif
