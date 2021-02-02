#include <winsock2.h>
#include <vector>
#include <memory>

#include "SocketContext.h"


BOOL SocketContext::Recv()
{
	BOOL status = TRUE;
	DWORD dwFlags = 0;
	int nBytesRecv = 0;

	ZeroMemory(&m_IoRecv, sizeof(IO_OPERATION_DATA));

	m_IoRecv.IoType = OP_READ;
	m_IoRecv.dataBuf.buf = (char*)&m_IoRecv.buffer;
	m_IoRecv.dataBuf.len = MAX_BUFFER_LEN;

	dwFlags = 0;

	nBytesRecv = WSARecv(m_Socket, &m_IoRecv.dataBuf, 1, NULL, &dwFlags, &m_IoRecv.overlapped, NULL);

	if ((SOCKET_ERROR == nBytesRecv) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		status = FALSE;
	}

	return status;
}

BOOL SocketContext::Send(char* szBuffer, UINT16 length)
{
	BOOL status = TRUE;
	int nBytesSent = 0;
	DWORD dwFlags = 0;

	ZeroMemory(&m_IoSend, sizeof(IO_OPERATION_DATA));

	m_IoSend.IoType = OP_WRITE;
	strcpy(m_IoSend.buffer, szBuffer);
	m_IoSend.len = length;

	m_IoSend.dataBuf.buf = (char*)&m_IoSend.buffer;
	m_IoSend.dataBuf.len = m_IoSend.len;

	nBytesSent = WSASend(m_Socket, &m_IoSend.dataBuf, 1, NULL, dwFlags, &m_IoSend.overlapped, NULL);

	if ((SOCKET_ERROR == nBytesSent) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		status = FALSE;
	}

	return status;
}

/*
	Forward data from recv buffer of client socket to
	send buffer of fwd socket and send it to destination from
	fwd socket.
*/
BOOL SocketContext::Forward(UINT16 length)
{
	BOOL status = TRUE;

	if (nullptr == GetBuddySocketContext())
	{
		status = FALSE;
		goto Exit;
	}

	status = GetBuddySocketContext()->Send(m_IoRecv.buffer, length);

Exit:
	return status;
}

SocketContext::~SocketContext()
{
	//Cancel all Recv IO operations
	CancelIoEx((HANDLE)m_Socket, &m_IoRecv.overlapped);
	SleepEx(0, TRUE); // the completion will be called here

	//Cancel all Send IO operation
	CancelIoEx((HANDLE)m_Socket, &m_IoSend.overlapped);
	SleepEx(0, TRUE); // the completion will be called here

	closesocket(m_Socket);

	//m_pBuddy.Get().reset();
	/*
	if (pFwd_SocketContext && (FALSE == pFwd_SocketContext->s_CircularDestructor))
	{
		s_CircularDestructor = TRUE;
		delete pFwd_SocketContext;
	}

	pFwd_SocketContext = NULL;*/
}

std::shared_ptr<SocketContext> SocketContext::GetBuddySocketContext() const
{
	return m_pBuddy.Get();
}

void SocketContext::SetBuddySocketContext(const std::shared_ptr<SocketContext> sc)
{
	this->m_pBuddy = sc;
}

SocketContext::BuddySocketContextPtr::BuddySocketContextPtr()
{
	_strong = nullptr;
	_weak.reset();
}

SocketContext::BuddySocketContextPtr::BuddySocketContextPtr(const std::shared_ptr<SocketContext> sc)
{
	_strong = nullptr;
	_weak.reset();

	Set(sc);
}

SocketContext::BuddySocketContextPtr& SocketContext::BuddySocketContextPtr::operator=(const std::shared_ptr<SocketContext> sc) {
	return Set(sc);
}

std::shared_ptr<SocketContext> SocketContext::BuddySocketContextPtr::Get() const
{
	if (nullptr != _strong)
	{
		return _strong;
	}
	else 
	{
		return _weak.lock();
	}
}

SocketContext::BuddySocketContextPtr& SocketContext::BuddySocketContextPtr::Set(std::shared_ptr<SocketContext> sc)
{
	if (nullptr == sc->GetBuddySocketContext())
	{
		_strong = sc;
		_weak.reset();
	}
	else
	{
		_strong = nullptr;
		_weak = sc;
	}

	return *this;
}