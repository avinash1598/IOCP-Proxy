#pragma once
#ifndef IOCTL_H_ //* Include guard */
#define IOCTL_H_

VOID 
FileConfigInit(PWDFDEVICE_INIT);

NTSTATUS 
InitializeQueue(WDFDEVICE);

/*
   The following value is arbitrarily chosen from the space defined by Microsoft
   as being "for non-Microsoft use"
*/
#define FILE_DEVICE_WFP 0xCF54

/*
    Define Custom IO control codes for IOCTL.
*/
#define IOCTL_WFP_NOTIFICATION CTL_CODE(FILE_DEVICE_WFP, 2049, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WFP_SIMULATE_EVENT CTL_CODE(FILE_DEVICE_WFP, 2050, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WFP_SET_DEVICE_CONTEXT_DATA CTL_CODE(FILE_DEVICE_WFP, 2051, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// Inverted device context structure
//
// KMDF will associate this structure with each "Inverted" device that
// this driver creates.
//
typedef struct _WFP_DEVICE_CONTEXT {
    WDFQUEUE    NotificationQueue;
    LONG        Sequence;
    LONG        UserModeAppProcessId;
} WFP_DEVICE_CONTEXT, * PWFP_DEVICE_CONTEXT;

//
// Accessor structure
//
// Given a WDFDEVICE handle, we'll use the following function to return
// a pointer to our device's context area.
//
// Device context is needed probably to preserve sequence number
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WFP_DEVICE_CONTEXT, WfpGetContextFromDevice)

//VOID
//InvertedNotify(PINVERTED_DEVICE_CONTEXT DevContext);

typedef struct _USERAPP_IOCTL_IN_BUF {
    LONG        Sequence;
    BOOL        success;
    LONG        UserModeAppProcessId;
} USERAPP_IOCTL_IN_BUF, * PUSERAPP_IOCTL_IN_BUF;

#endif

// extern c is needed when using c code in c++ - 
// to avoid name mangling, a feature supported in c++ 
// to deal with function overloading
