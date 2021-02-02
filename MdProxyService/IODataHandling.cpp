#include <winsock2.h>
#include <vector>
#include <memory>
#include <thread>

#include "MdProxyService.h"
#include "SocketContext.h"


DWORD WINAPI WorkerThread(LPVOID lpParam)
{
	int nThreadNo = (int)lpParam;

	void* lpContext = nullptr;
	OVERLAPPED* pOverlapped = nullptr;
	//std::shared_ptr<SocketContext> pClientContext = nullptr;
	DWORD dwBytesTransfered = 0;
	char sock_type[] = "LOCAL SOCKET";

	//Worker thread will be around to process requests, until a Shutdown event is not Signaled.
	while (WAIT_OBJECT_0 != WaitForSingleObject(g_hShutdownEvent, 0))
	{
		BOOL status = TRUE;
		BOOL bReturn = GetQueuedCompletionStatus(
			g_hIOCompletionPort,
			&dwBytesTransfered,
			(LPDWORD)&lpContext,
			&pOverlapped,
			INFINITE);

		if (NULL == lpContext)
		{
			//We are shutting down
			status = FALSE;
			break;
		}
		WriteToConsole("\nThread %d: Inside worker thread: %ld", nThreadNo);
		std::shared_ptr<SocketContext> pClientContext = *static_cast<std::shared_ptr<SocketContext>*>(lpContext);
		WriteToConsole("\nThread %d: Number of objects sharing this socket: %ld", nThreadNo, pClientContext.use_count());
		//pClientContext = (SocketContext*)lpContext;
		PIO_OPERATION_DATA pIoData = (PIO_OPERATION_DATA)pOverlapped;

		//Socket type
		if (TRUE == pClientContext->GetProxySocket())
		{
			WriteToConsole("\nThread %d PROXY SOCKET: Socket id %d.", nThreadNo, pClientContext->GetId());
			strcpy(sock_type, "PROXY SOCKET");
		}
		else
		{
			WriteToConsole("\nThread %d LOCAL SOCKET: Socket Id %d.", nThreadNo, pClientContext->GetId());
			strcpy(sock_type, "LOCAL SOCKET");
		}

		//Check is socket connection is closed
		if ((FALSE == bReturn) || ((TRUE == bReturn) && (0 == dwBytesTransfered)))
		{
			WriteToConsole("\nThread %d %s: Client connection gone.", nThreadNo, sock_type);
			status = FALSE;
			goto Exit;
		}

		try
		{
			switch (pIoData->IoType)
			{
			case OP_READ:

				WriteToConsole("\nThread %d %s: Received %ld bytes.", nThreadNo, sock_type, dwBytesTransfered);

				//Forward data no longer can be a SocketContext class 
				//memebr function.
				//pClientContext->GetRecvBuffer();
				if (FALSE == pClientContext->Forward(dwBytesTransfered))
				{
					WriteToConsole("\nThread %d %s: Error occured while forwading data.", nThreadNo, sock_type);
					status = FALSE;
					goto Exit;
				}

				if (FALSE == pClientContext->Recv())
				{
					WriteToConsole("\nThread %d %s: Error occured while receiving data.", nThreadNo, sock_type);
					status = FALSE;
					goto Exit;
				}

				break;

			case OP_WRITE:

				WriteToConsole("\nThread %d %s: Sent %ld bytes.", nThreadNo, sock_type, dwBytesTransfered);

				break;

			default:
				break;
			}
		}
		catch (const char* message)
		{
			WriteToConsole("\nThread %d %s: Exception occured: %s.", message);
		}

	Exit:
		if (FALSE == status)
		{
			WriteToConsole("\nThread %d %s: Status is FALSE.", nThreadNo, sock_type);
			RemoveFromClientListAndCleanUpMemory(pClientContext);
		}

		WriteToConsole("\nThread %d: Number of objects sharing this socket: %ld", nThreadNo, pClientContext.use_count());
	} // while

	return 0;
}
