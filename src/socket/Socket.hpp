#ifndef _XSILON_SOCKET_HPP_
#define _XSILON_SOCKET_HPP_

#define UBP_READ				(0)
#define UBP_WRITE				(1)


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

	void
	openPipes();
	void
	closePipes();
};

class Socket_pimpl;
class Socket
{
public:
	Socket(int domain, int type, int protocol);
	virtual ~Socket();

	int
	getSockFd();

	int
	setBlocking(bool blocking);
	int
	setCloseOnExec(bool closeOnExec);
	int
	setReuseAddress(bool reuse);

	void
	bindAnyAddress(int port);
	void
	setPassive(int backlog);

private:
	Socket_pimpl * pimpl;
};

class ServerSocket
{
public:
	ServerSocket();
	virtual ~ServerSocket();
};


#endif
