#pragma once
#ifndef _KERNEL_COMMUNICATOR_H_
#define _KERNEL_COMMUNICATOR_H_

//Disable deprecation warnings
#pragma warning(disable: 4996)

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

#define WFP_DEVICE_FILE_NAME L"\\\\.\\MadrupamNetDeviceLink"

typedef struct _USERAPP_IOCTL_IN_BUF {
    LONG        Sequence;
    BOOL        success;
    LONG        UserModeAppProcessId;
} USERAPP_IOCTL_IN_BUF, * PUSERAPP_IOCTL_IN_BUF;

BOOL UpdateKernelWithUserAppProcessId();

#endif