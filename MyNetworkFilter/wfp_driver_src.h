#pragma once
#ifndef WFP_DRIVER_SRC_H_ //* Include guard */
#define WFP_DRIVER_SRC_H_

#define DEVICE_STRING L"\\Device\\MadrupamNetDevice"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD DeriverUnload;

WDFDEVICE* GGetWDFDevice();

#endif
