#include <stdio.h>
#include <windows.h>
#include "KernelCommunicator.h"

BOOL UpdateKernelWithUserAppProcessId() 
{
	BOOL status = TRUE;
	DWORD processId;
	PUSERAPP_IOCTL_IN_BUF pUserAppData;

	HANDLE device;

	processId = GetCurrentProcessId();
	printf("\nCurrent process ID: %ld\n", processId);

	pUserAppData = (PUSERAPP_IOCTL_IN_BUF)malloc(sizeof(USERAPP_IOCTL_IN_BUF));
	memset(pUserAppData, 0, sizeof(USERAPP_IOCTL_IN_BUF));
	pUserAppData->UserModeAppProcessId = (LONG)processId;
	
	device = CreateFileW(WFP_DEVICE_FILE_NAME,
						 GENERIC_ALL,
						 0,
						 0,
						 OPEN_EXISTING,
						 FILE_ATTRIBUTE_SYSTEM, 
						 0);

	if (device == INVALID_HANDLE_VALUE)
	{
		printf_s("> Could not open device: 0x%x\n", GetLastError());
		status = FALSE;
		goto Exit;
	}

	status = DeviceIoControl(device,
							 IOCTL_WFP_SET_DEVICE_CONTEXT_DATA,
							 pUserAppData,
							 sizeof(USERAPP_IOCTL_IN_BUF),
							 NULL,
							 0,
							 NULL,
							 (LPOVERLAPPED)NULL);

	if (FALSE == status)
	{
		printf_s("> Error sending device IO control code: 0x%x\n", GetLastError());
		status = FALSE;
		goto Exit;
	}

Exit:
	free(pUserAppData);
	
	if (device)
		CloseHandle(device);

	return status;
}
