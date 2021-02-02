//#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <string.h>
#include <winsock2.h>
#include <vector>
//#include <unordered_map>
#include <Mstcpip.h>
#include <intsafe.h>
#include <memory>
#pragma comment(lib, "Ws2_32.lib")

#include "MdProxyService.h"
#include "KernelCommunicator.h"
#include "SocketContext.h"

HANDLE g_hShutdownEvent = NULL;
int g_nThreads = 0;
HANDLE* g_phWorkerThreads = NULL;
HANDLE g_hAcceptThread = NULL;
CRITICAL_SECTION g_csConsole; 
CRITICAL_SECTION g_csClientList; 
CRITICAL_SECTION g_numSockCntxt;
CRITICAL_SECTION g_csSockCntxtMap;
CRITICAL_SECTION g_csSockCntxtCleanUp;
HANDLE g_hIOCompletionPort = NULL;
std::vector<std::shared_ptr<SocketContext>> g_ClientContext;
WSAEVENT g_hAcceptEvent;

int SOCK_CNTXT_COUNT = 0;

int main(int argc, char* argv[])
{
	//Update user application process id to WFP kernel driver
	if (TRUE != UpdateKernelWithUserAppProcessId())
	{
		printf_s("\nError updating data to kernel driver.");
		return 1;
	}

	/*
	//Validate the input
	if (argc < 2)
	{
		printf("\nUsage: %s port.", argv[0]);
		return 1;
	}*/
	printf("\nUsage: %s port.", PROXY_SERVER_PORT);

	if (false == Initialize())
	{
		return 1;
	}

	SOCKET ListenSocket;

	struct sockaddr_in ServerAddress;

	//Overlapped I/O follows the model established in Windows and can be performed only on 
	//sockets created through the WSASocket function 
	ListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	if (INVALID_SOCKET == ListenSocket)
	{
		printf("\nError occurred while opening socket: %d.", WSAGetLastError());
		goto error;
	}
	else
	{
		printf("\nWSASocket() successful.");
	}

	//Cleanup and Init with 0 the ServerAddress
	ZeroMemory((char*)&ServerAddress, sizeof(ServerAddress));

	//Port number will be supplied as a command line argument
	int nPortNo;
	nPortNo = atoi(PROXY_SERVER_PORT);

	//Fill up the address structure
	ServerAddress.sin_family = AF_INET;
	ServerAddress.sin_addr.s_addr = INADDR_ANY; //WinSock will supply address
	ServerAddress.sin_port = htons(nPortNo);    //comes from commandline

	//Assign local address and port number
	if (SOCKET_ERROR == bind(ListenSocket, (struct sockaddr*) & ServerAddress, sizeof(ServerAddress)))
	{
		closesocket(ListenSocket);
		printf("\nError occurred while binding.");
		goto error;
	}
	else
	{
		printf("\nbind() successful.");
	}

	//Make the socket a listening socket
	if (SOCKET_ERROR == listen(ListenSocket, SOMAXCONN))
	{
		closesocket(ListenSocket);
		printf("\nError occurred while listening.");
		goto error;
	}
	else
	{
		printf("\nlisten() successful.");
	}

	g_hAcceptEvent = WSACreateEvent();

	if (WSA_INVALID_EVENT == g_hAcceptEvent)
	{
		printf("\nError occurred while WSACreateEvent().");
		goto error;
	}

	if (SOCKET_ERROR == WSAEventSelect(ListenSocket, g_hAcceptEvent, FD_ACCEPT))
	{
		printf("\nError occurred while WSAEventSelect().");
		WSACloseEvent(g_hAcceptEvent);
		goto error;
	}

	printf("\nTo exit this server, hit a key at any time on this console...");

	DWORD nThreadID;
	g_hAcceptThread = CreateThread(0, 0, AcceptThread, (void*)ListenSocket, 0, &nThreadID);

	//Hang in there till a key is hit
	while (!_kbhit())
	{
		Sleep(0);  //switch to some other thread
	}

	WriteToConsole("\nServer is shutting down...");

	//Start cleanup
	CleanUp();

	//Close open sockets
	closesocket(ListenSocket);

	DeInitialize();

	return 0; //success

error:
	closesocket(ListenSocket);
	DeInitialize();
	return 1;
}

bool Initialize()
{
	//Find out number of processors and threads
	g_nThreads = WORKER_THREADS_PER_PROCESSOR * GetNoOfProcessors();

	printf("\nNumber of processors on host: %d", GetNoOfProcessors());

	printf("\nThe following number of worker threads will be created: %d", g_nThreads);

	//Allocate memory to store thread handless
	g_phWorkerThreads = new HANDLE[g_nThreads];

	//Initialize the Console Critical Section
	InitializeCriticalSection(&g_csConsole);

	//Initialize the Client List Critical Section
	InitializeCriticalSection(&g_csClientList);

	//Initialize the Client List Critical Section
	InitializeCriticalSection(&g_numSockCntxt);

	//Initialize the Socket context map Critical Section
	InitializeCriticalSection(&g_csSockCntxtMap);

	//Initialize the Socket context map Critical Section
	InitializeCriticalSection(&g_csSockCntxtCleanUp);

	//Create shutdown event
	g_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// Initialize Winsock
	WSADATA wsaData;

	int nResult;
	nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (NO_ERROR != nResult)
	{
		printf("\nError occurred while executing WSAStartup().");
		return false; //error
	}
	else
	{
		printf("\nWSAStartup() successful.");
	}

	if (false == InitializeIOCP())
	{
		printf("\nError occurred while initializing IOCP");
		return false;
	}
	else
	{
		printf("\nIOCP initialization successful.");
	}

	return true;
}

//Function to Initialize IOCP
bool InitializeIOCP()
{
	//Create I/O completion port
	g_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	if (NULL == g_hIOCompletionPort)
	{
		printf("\nError occurred while creating IOCP: %d.", WSAGetLastError());
		return false;
	}

	DWORD nThreadID;

	//Create worker threads
	for (int ii = 0; ii < g_nThreads; ii++)
	{
		g_phWorkerThreads[ii] = CreateThread(0, 0, WorkerThread, (void*)(ii + 1), 0, &nThreadID);
	}

	return true;
}

void CleanUp()
{
	//Ask all threads to start shutting down
	SetEvent(g_hShutdownEvent);
	
	//Let Accept thread go down
	WaitForSingleObject(g_hAcceptThread, INFINITE);
	
	for (int i = 0; i < g_nThreads; i++)
	{
		//Help threads get out of blocking - GetQueuedCompletionStatus()
		PostQueuedCompletionStatus(g_hIOCompletionPort, 0, (DWORD)NULL, NULL);
	}
	
	//Let Worker Threads shutdown
	WaitForMultipleObjects(g_nThreads, g_phWorkerThreads, TRUE, INFINITE);
	
	//We are done with this event
	WSACloseEvent(g_hAcceptEvent);
	
	//Cleanup dynamic memory allocations, if there are any.
	CleanClientList();
}

void DeInitialize()
{
	//Delete the Console Critical Section.
	DeleteCriticalSection(&g_csConsole);

	//Delete the Client List Critical Section.
	DeleteCriticalSection(&g_csClientList);

	//Delete the Client List Critical Section.
	DeleteCriticalSection(&g_numSockCntxt);

	//Delete the Socket Context map Critical Section.
	DeleteCriticalSection(&g_csSockCntxtMap);

	//Delete the Socket Context map Critical Section.
	DeleteCriticalSection(&g_csSockCntxtCleanUp);

	//Cleanup IOCP.
	CloseHandle(g_hIOCompletionPort);

	//Clean up the event.
	CloseHandle(g_hShutdownEvent);

	//Clean up memory allocated for the storage of thread handles
	delete[] g_phWorkerThreads;

	//Cleanup Winsock
	WSACleanup();
}

//This thread will look for accept event
DWORD WINAPI AcceptThread(LPVOID lParam)
{
	SOCKET ListenSocket = (SOCKET)lParam;

	WSANETWORKEVENTS WSAEvents;

	//Accept thread will be around to look for accept event, until a Shutdown event is not Signaled.
	while (WAIT_OBJECT_0 != WaitForSingleObject(g_hShutdownEvent, 0))
	{
		if (WSA_WAIT_TIMEOUT != WSAWaitForMultipleEvents(1, &g_hAcceptEvent, FALSE, WAIT_TIMEOUT_INTERVAL, FALSE))
		{
			WSAEnumNetworkEvents(ListenSocket, g_hAcceptEvent, &WSAEvents);
			if ((WSAEvents.lNetworkEvents & FD_ACCEPT) && (0 == WSAEvents.iErrorCode[FD_ACCEPT_BIT]))
			{
				//Process it
				AcceptConnection(ListenSocket);
			}
		}
	}

	return 0;
}

//This function will process the accept event
void AcceptConnection(SOCKET ListenSocket)
{
	BOOL status = TRUE;
	std::shared_ptr<SocketContext> pClientContext = nullptr;
	std::shared_ptr<SocketContext> pRemoteSocketContext = nullptr;
	SOCKET Socket = NULL;

	IncrementSocketContextCount();

	sockaddr_in ClientAddress;
	int nClientLength = sizeof(ClientAddress);
	
	//Accept remote connection attempt from the client
	Socket = WSAAccept(ListenSocket, (sockaddr*)&ClientAddress, &nClientLength, NULL, 0);

	if (INVALID_SOCKET == Socket)
	{
		WriteToConsole("\nError occurred while accepting socket: %ld.", WSAGetLastError());
		status = FALSE;
		goto Exit;
	}

	//Display Client's IP
	WriteToConsole("\nClient connected from: %s", inet_ntoa(ClientAddress.sin_addr));

	//Create a new ClientContext for this newly accepted client
	pClientContext = std::make_shared<SocketContext>();
	pClientContext->SetSocket(Socket);
	pClientContext->SetId(SOCK_CNTXT_COUNT);

	//Create remote connection
	pRemoteSocketContext = InitRemoteConnection(pClientContext);

	if (pRemoteSocketContext == NULL) 
	{
		WriteToConsole("\nShutting down client socket.");
		status = FALSE;
		goto Exit;
	}

	//This is the right place to link two sockets
	pClientContext->SetBuddySocketContext(pRemoteSocketContext);
	pRemoteSocketContext->SetBuddySocketContext(pClientContext);

	//Associate both local and remote socket with IOCP.
	if (false == AssociateWithIOCP(pClientContext))
	{
		WriteToConsole("\nError associating local socket with IOCP.");
		status = FALSE;
		goto Exit;
	}

	if (false == AssociateWithIOCP(pRemoteSocketContext))
	{
		WriteToConsole("\nError associating remote proxy socket with IOCP.");
		status = FALSE;
		goto Exit;
	}
	
	//This is the right place to store socket contexts
	AddToClientList(pClientContext);
	AddToClientList(pRemoteSocketContext);
	
	//Post initial Recv
	//This is a right place to post a initial Recv
	//Posting a initial Recv in WorkerThread will create scalability issues.
	if (FALSE == pClientContext->Recv())
	{
		WriteToConsole("\nError in Initial Post.");
		status = FALSE;
		goto Exit;
	}

	if (FALSE == pRemoteSocketContext->Recv())
	{
		WriteToConsole("\nError in Initial Post.");
		status = FALSE;
		goto Exit;
	}
	
	WriteToConsole("\nNumber of objects sharing Client socket: %ld", pClientContext.use_count());
	WriteToConsole("\nNumber of objects sharing Remote socket: %ld", pRemoteSocketContext.use_count());

Exit:
	if (FALSE == status) 
	{
		if (Socket)
		{
			closesocket(Socket);
			Socket = NULL;
		}
		if (pClientContext || pRemoteSocketContext)
		{
			RemoveFromClientListAndCleanUpMemory(pClientContext);
			pClientContext = nullptr;

			WriteToConsole("\nClient list size after socket context deletion %d", g_ClientContext.size());

			RemoveFromClientListAndCleanUpMemory(pRemoteSocketContext);
			pRemoteSocketContext = nullptr;

			WriteToConsole("\nClient list size after socket context deletion %d", g_ClientContext.size());
		}
	}
}

//Create connection to remote server socket
//TODO: get destination server address here
std::shared_ptr<SocketContext> InitRemoteConnection(std::shared_ptr<SocketContext> pClientContext)
{
	BOOL status = TRUE;

	IncrementSocketContextCount();

	const SIZE_T REDIRECT_RECORDS_SIZE = 2048;
	const SIZE_T REDIRECT_CONTEXT_SIZE = sizeof(SOCKADDR_STORAGE) * 2;

	UINT32 wsaStatus;
	SOCKET remoteProxySocket = NULL;
	std::shared_ptr<SocketContext> pRemoteSockContext = nullptr;

	BYTE** pRedirectRecords = 0;
	BYTE** pRedirectContext = 0;
	SIZE_T redirectRecordsSize = 0;
	SIZE_T redirectContextSize = 0;
	SIZE_T redirectRecordsSet = 0;
	
	SOCKADDR_STORAGE* pRemoteSockAddrStorage = 0;

	//Allocate memory in heap else winsock will throw 10014 error
	//Initialization on top causes memory to be allocated in stack
	HLPR_NEW_ARRAY(pRedirectRecords, BYTE*, REDIRECT_RECORDS_SIZE);
	HLPR_NEW_ARRAY(pRedirectContext, BYTE*, REDIRECT_CONTEXT_SIZE);
	HLPR_NEW_ARRAY(pRemoteSockAddrStorage, SOCKADDR_STORAGE, 1);
	
	//Opaque data to be set on proxy connection
	wsaStatus = WSAIoctl(pClientContext->GetSocket(),
					  SIO_QUERY_WFP_CONNECTION_REDIRECT_RECORDS,
					  0, 
					  0,
					  (BYTE*)pRedirectRecords,
					  REDIRECT_RECORDS_SIZE,
					  (LPDWORD)&redirectRecordsSize,
					  0, 
					  0);

	if (NO_ERROR != wsaStatus)
	{
		WriteToConsole("\nUnable to get redirect records from socket: %ld", WSAGetLastError());
		status = FALSE;
		goto Exit;
	}
	
	//Callout allocated data, contains original destination information
	wsaStatus = WSAIoctl(pClientContext->GetSocket(),
					  SIO_QUERY_WFP_CONNECTION_REDIRECT_CONTEXT,
					  0,
				      0,
					  (BYTE*)pRedirectContext,
					  REDIRECT_CONTEXT_SIZE,
					  (LPDWORD)&redirectContextSize,
					  0,
					  0);

	if (NO_ERROR != wsaStatus)
	{
		WriteToConsole("\nUnable to get redirect context from socket: %ld", WSAGetLastError());
		status = FALSE;
		goto Exit;
	}
	
	// copy remote address
	RtlCopyMemory(pRemoteSockAddrStorage, 
				  &(((SOCKADDR_STORAGE*)pRedirectContext)[0]), 
				  sizeof(SOCKADDR_STORAGE));
	
	remoteProxySocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	
	if (INVALID_SOCKET == remoteProxySocket)
	{
		WriteToConsole("\nError occured while opening socket: %ld", WSAGetLastError());
		status = FALSE;
		goto Exit;
	}
	
	wsaStatus = WSAIoctl(remoteProxySocket,
					  SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS,
					  (BYTE*)pRedirectRecords,
					  (DWORD)redirectRecordsSize,
					  0,
					  0,
					  (LPDWORD)&redirectRecordsSet,
					  0,
					  0);

	if (NO_ERROR != wsaStatus)
	{
		WriteToConsole("\nUnable to set redirect records on socket: %ld", WSAGetLastError());
		status = FALSE;
		goto Exit;
	}

	/*
		Check no longer valid:
		Refer this: https://social.msdn.microsoft.com/Forums/en-US/be79cc7f-e2ff-47ce-bc83-f79307680042/seting-redirect-records-on-the-proxy-connection-socket-correctly?forum=wfp
	*/
	if (redirectRecordsSize != redirectRecordsSet)
	{
		WriteToConsole("\nRedirect record size mismatch. %ld and %ld", 
					   redirectRecordsSize, redirectRecordsSet);
		//goto Exit;
	}

	
	//RemoteAddr.sin_addr.s_addr = inet_addr("18.136.42.214");

	wsaStatus = WSAConnect(remoteProxySocket, (SOCKADDR*)pRemoteSockAddrStorage, sizeof(SOCKADDR_STORAGE), 0, 0, 0, 0);

	if (SOCKET_ERROR == wsaStatus)
	{
		WriteToConsole("\nError occured while connecting to remote server: %ld", WSAGetLastError());
		status = FALSE;
		goto Exit;
	}
	
	pRemoteSockContext = std::make_shared<SocketContext>();
	
	pRemoteSockContext->SetProxySocket(TRUE);
	pRemoteSockContext->SetSocket(remoteProxySocket);
	pRemoteSockContext->SetId(SOCK_CNTXT_COUNT);

Exit:
	if (FALSE == status)
	{
		if (remoteProxySocket)
		{
			closesocket(remoteProxySocket);
			remoteProxySocket = NULL;
		}
		if (NULL != pRemoteSockContext)
		{
			//delete pRemoteSockContext;
			//pRemoteSockContext = NULL;
			pRemoteSockContext = nullptr;
		}
	}
	
	HLPR_DELETE_ARRAY(pRedirectContext);
	HLPR_DELETE_ARRAY(pRedirectRecords);
	HLPR_DELETE_ARRAY(pRemoteSockAddrStorage);

	return pRemoteSockContext;
}

bool AssociateWithIOCP(std::shared_ptr<SocketContext> pClientContext)
{
	//Associate the socket with IOCP
	//here we are only passing reference to our client context
	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pClientContext->GetSocket(), g_hIOCompletionPort, (DWORD)&pClientContext, 0);

	if (NULL == hTemp)
	{
		WriteToConsole("\nError occurred while executing CreateIoCompletionPort().");

		//Let's not work with this client
		RemoveFromClientListAndCleanUpMemory(pClientContext);

		return false;
	}

	return true;
}


//Function to synchronize console output
//Threads need to be synchronized while they write to console.
//WriteConsole() API can be used, it is thread-safe, I think.
//I have created my own function.
void WriteToConsole(const char* szFormat, ...)
{
	EnterCriticalSection(&g_csConsole);

	va_list args;
	va_start(args, szFormat);

	vprintf(szFormat, args);

	va_end(args);

	LeaveCriticalSection(&g_csConsole);
}

void IncrementSocketContextCount()
{
	EnterCriticalSection(&g_numSockCntxt);

	SOCK_CNTXT_COUNT++;

	LeaveCriticalSection(&g_numSockCntxt);
}

void DecrementSocketContextCount()
{
	EnterCriticalSection(&g_numSockCntxt);

	SOCK_CNTXT_COUNT--;

	LeaveCriticalSection(&g_numSockCntxt);
}

//Store client related information in a vector
void AddToClientList(std::shared_ptr<SocketContext> pClientContext)
{
	EnterCriticalSection(&g_csClientList);

	//Store these structures in vectors
	g_ClientContext.push_back(pClientContext);

	LeaveCriticalSection(&g_csClientList);
}

//This function will allow to remove socket contexts out of the list
void RemoveFromClientListAndCleanUpMemory(std::shared_ptr<SocketContext> pClientContext)
{
	EnterCriticalSection(&g_csClientList);
	
	std::vector <std::shared_ptr<SocketContext>>::iterator IterClientContext;
	
	//Remove the supplied ClientContext from the list and release the memory
	for (IterClientContext = g_ClientContext.begin(); IterClientContext != g_ClientContext.end(); IterClientContext++)
	{
		if (pClientContext == *IterClientContext)
		{
			g_ClientContext.erase(IterClientContext);
			
			WriteToConsole("\nNumber of objects sharing this socket: %ld", pClientContext.use_count());
			
			if (pClientContext.use_count() > 0)
			{
				//i/o will be cancelled and socket will be closed by destructor.
				//delete pClientContext;
				pClientContext = nullptr;
			}
			
			WriteToConsole("\nNumber of objects sharing this socket: %ld", pClientContext.use_count());
			break;
		}
	}

	LeaveCriticalSection(&g_csClientList);
}

//Clean up the list, this function will be executed at the time of shutdown
void CleanClientList()
{
	EnterCriticalSection(&g_csClientList);

	std::vector <std::shared_ptr<SocketContext>>::iterator IterClientContext;

	for (IterClientContext = g_ClientContext.begin(); IterClientContext != g_ClientContext.end(); IterClientContext++)
	{
		//i/o will be cancelled and socket will be closed by destructor.
		//delete* IterClientContext;
		*IterClientContext = nullptr;
	}

	g_ClientContext.clear();

	LeaveCriticalSection(&g_csClientList);
}

/*
	Functions for socket context map operations
*/
/*
void StoreInSocketContextMap(SocketContext* localSocket, SocketContext* remoteSocket)
{
	EnterCriticalSection(&g_csSockCntxtMap);

	g_SockCntxtMap[localSocket] = remoteSocket;
	g_SockCntxtMap[remoteSocket] = localSocket;
	
	LeaveCriticalSection(&g_csSockCntxtMap);
}

SocketContext* GetFwdSockCntxtFromMap(SocketContext* s)
{
	SocketContext* fwdSockCntxt = NULL;

	EnterCriticalSection(&g_csSockCntxtMap);

	fwdSockCntxt = g_SockCntxtMap[s];

	LeaveCriticalSection(&g_csSockCntxtMap);

	return fwdSockCntxt;
}

void RemoveSockCntxtFromMap(SocketContext* sToDelete)
{
	EnterCriticalSection(&g_csSockCntxtMap);
	
	g_SockCntxtMap.erase(sToDelete);
	
	LeaveCriticalSection(&g_csSockCntxtMap);
}*/

//The use of static variable will ensure that 
//we will make a call to GetSystemInfo() 
//to find out number of processors, 
//only if we don't have the information already.
//Repeated use of this function will be efficient.
int GetNoOfProcessors()
{
	static int nProcessors = 0;

	if (0 == nProcessors)
	{
		SYSTEM_INFO si;

		GetSystemInfo(&si);

		nProcessors = si.dwNumberOfProcessors;
	}

	return nProcessors;
}