#ifndef _MD_PROXY_SERVICE_H_
#define _MD_PROXY_SERVICE_H_

//Disable deprecation warnings
#pragma warning(disable: 4996)

#define WORKER_THREADS_PER_PROCESSOR 2

//Time out interval for wait calls
#define WAIT_TIMEOUT_INTERVAL 100

//Proxy service port 
#define PROXY_SERVER_PORT "1598"

//Allocate memory with new and zero out its contents.
#define HLPR_NEW_ARRAY(pPtr, object, count)           \
   do                                                 \
   {                                                  \
      size_t SAFE_SIZE = 0;                           \
      HLPR_DELETE_ARRAY(pPtr);                        \
      if(SizeTMult(sizeof(object),                    \
                   (size_t)count,                     \
                   &SAFE_SIZE) == S_OK &&             \
         SAFE_SIZE >= (sizeof(object) * count))       \
      {                                               \
         pPtr = new object[count];                    \
         if(pPtr)                                     \
            SecureZeroMemory(pPtr,                    \
                             SAFE_SIZE);              \
      }                                               \
      else                                            \
      {                                               \
         break;                                       \
      }                                               \
   }while(pPtr == 0)

//Free memory allocated with new[] and set the pointer to 0
#define HLPR_DELETE_ARRAY(pPtr) \
   if(pPtr)                     \
   {                            \
      delete[] pPtr;            \
      pPtr = 0;                 \
   }

//Graceful shutdown Event
//For this simple implementation,
//We can use global variable as well.
//Wanted to demonstrate use of event
//for shutdown
extern HANDLE g_hShutdownEvent;

//Number of threads to be created.
extern int g_nThreads;

//To store handle of worker threads
extern HANDLE* g_phWorkerThreads;

//Handle for Accept related thread
extern HANDLE g_hAcceptThread;

//Network Event for Accept
extern WSAEVENT	g_hAcceptEvent;

extern CRITICAL_SECTION g_csConsole; //When threads write to console we need mutual exclusion
extern CRITICAL_SECTION g_csClientList; //Need to protect the client list
extern CRITICAL_SECTION g_numSockCntxt; //Need to protect the socket context count
extern CRITICAL_SECTION g_csSockCntxtMap; //Critical section for Socket context map
extern CRITICAL_SECTION g_csSockCntxtCleanUp;

//Global I/O completion port handle
extern HANDLE g_hIOCompletionPort;

//Global map to store socket context mappings
//extern std::unordered_map<SocketContext*, SocketContext*> g_SockCntxtMap;

//global functions
bool InitializeIOCP();
bool Initialize();
void CleanUp();
void DeInitialize();
DWORD WINAPI AcceptThread(LPVOID lParam);
void AcceptConnection(SOCKET ListenSocket);
DWORD WINAPI WorkerThread(LPVOID lpParam);
void WriteToConsole(const char* szFormat, ...);
int GetNoOfProcessors();
void IncrementSocketContextCount();
void DecrementSocketContextCount();

#endif