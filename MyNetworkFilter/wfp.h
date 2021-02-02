#pragma once
#ifndef WFP_H_ //* Include guard */
#define WFP_H_

#define INITGUID
#include <guiddef.h>
#include <Ip2string.h>
#include <ntintsafe.h> 

//96ebd471-62ea-4b06-8c3e-33ab71b6c6d7
DEFINE_GUID
(
	WFP_ALE_REDIRECT_CALLOUT_V4,
	0x96ebd471,
	0x62ea,
	0x4b06,
	0x8c, 0x3e, 0x33, 0xab, 0x71, 0xb6, 0xc6, 0xd7
);

//e69fd434-1b88-4334-b207-7ac1e8d3fd97
DEFINE_GUID
(
	WFP_SAMPLE_SUBLAYER_GUID,
	0xe69fd434,
	0x1b88,
	0x4334,
	0xb2, 0x07, 0x7a, 0xc1, 0xe8, 0xd3, 0xfd, 0x97
);

#define WFP_CALLOUT_DRIVER_TAG (UINT32)'MDRU'

#define htonl(x) (((((ULONG)(x))&0xffL)<<24) | \
((((ULONG)(x))&0xff00L)<<8) | \
((((ULONG)(x))&0xff0000L)>>8) | \
((((ULONG)(x))&0xff000000L)>>24))

#define HLPR_NEW_ARRAY(pPtr, object, count, tag)               \
   for(;                                                       \
       pPtr == 0;                                              \
      )                                                        \
   {                                                           \
      size_t SAFE_SIZE = 0;                                    \
      if(count &&                                              \
         RtlSizeTMult(sizeof(object),                          \
                      (size_t)count,                           \
                      &SAFE_SIZE) == STATUS_SUCCESS &&         \
         SAFE_SIZE >= (sizeof(object) * count))                \
      {                                                        \
         pPtr = (object*)ExAllocatePoolWithTag(NonPagedPoolNx, \
                                               SAFE_SIZE,      \
                                               tag);           \
         if(pPtr)                                              \
            RtlZeroMemory(pPtr,                                \
                          SAFE_SIZE);                          \
      }                                                        \
      else                                                     \
      {                                                        \
         pPtr = 0;                                             \
         break;                                                \
      }                                                        \
   }

#define HLPR_DELETE_ARRAY(pPtr, tag)       \
   if(pPtr)                          \
   {                                 \
      ExFreePoolWithTag((VOID*)pPtr, \
                        tag);        \
      pPtr = 0;                      \
   }

#define REDIRECT_IP L"127.0.0.1"
#define REDIRECT_PORT 1598

NTSTATUS 
InitializeWfp(PDEVICE_OBJECT);

VOID 
UnInitializeWfp(void);

#endif
