#include "wdf_main.h"
#include "ioctl.h"

UNICODE_STRING DEVICE_SYMBOLIC_NAME = RTL_CONSTANT_STRING(L"\\??\\MadrupamNetDeviceLink");

VOID
WfpNotify(WDFDEVICE device) {
	NTSTATUS status;
	SIZE_T bytesReturned;
	WDFREQUEST notifyRequest;
	PULONG bufferPointer;
	LONG valueToReturn;
	PWFP_DEVICE_CONTEXT devContext;

	devContext = WfpGetContextFromDevice(device);

	status = WdfIoQueueRetrieveNextRequest(devContext->NotificationQueue,
										   &notifyRequest);

	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to retrive request from notification queue.\n"));
		return;
	}
	
	status = WdfRequestRetrieveOutputBuffer(notifyRequest, 
											sizeof(LONG), 
											(PVOID*)&bufferPointer, 
											NULL);

	if (!NT_SUCCESS(status)) 
	{
		KdPrint(("Failed to retrive output buffer.\n"));
		status = STATUS_SUCCESS;
		bytesReturned = 0;
	}
	else
	{
		valueToReturn = InterlockedExchangeAdd(&devContext->Sequence, 
											   1);
		*bufferPointer = valueToReturn;

		status = STATUS_SUCCESS;
		bytesReturned = sizeof(LONG);
	}

	KdPrint(("Sending notification to userland application.\n"));
	WdfRequestCompleteWithInformation(notifyRequest, status, bytesReturned);
}

VOID 
EvtIoDeviceControl(
	_In_ WDFQUEUE     Queue,
	_In_ WDFREQUEST   Request,
	_In_ size_t       OutputBufferLength,
	_In_ size_t       InputBufferLength,
	_In_ ULONG        IoControlCode
)
{
	KdPrint(("IOCTL: Event IO device control.\n"));

	PWFP_DEVICE_CONTEXT devContext;
	NTSTATUS status;
	ULONG_PTR info;
	
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	devContext = WfpGetContextFromDevice(
									WdfIoQueueGetDevice(Queue));

	// Set the default completion status and information field 
	status = STATUS_INVALID_PARAMETER;
	info = 0;

	switch (IoControlCode) {
	
	case IOCTL_WFP_NOTIFICATION:
		KdPrint(("Notification IOCTL\n"));
		
		if (InputBufferLength >= sizeof(USERAPP_IOCTL_IN_BUF)) {
			KdPrint(("Data in input buffer received.\n"));
			
			PVOID buffer;
			PUSERAPP_IOCTL_IN_BUF pUserData;

			status = WdfRequestRetrieveInputBuffer(Request, 
												   sizeof(LONG),
												   &buffer,
												   NULL);

			if (!NT_SUCCESS(status)) {
				KdPrint(("Failed to retrive input buffer.\n"));
			}

			pUserData = (PUSERAPP_IOCTL_IN_BUF)buffer;
			KdPrint(("Sequence data received from useland application: %ld\n", pUserData->Sequence));
			//Resume clasifyout operation.
			break;
		}

		if (OutputBufferLength < sizeof(LONG)) {
			KdPrint(("Insufficient Output buffer length.\n"));
			break;
		}

		status = WdfRequestForwardToIoQueue(Request,
											devContext->NotificationQueue);

		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to forward request to notification queue: 0x%x \n", status));
			break;
		}
		//TODO: Push Packet or Request ID into buffer

		return;
	
	case IOCTL_WFP_SIMULATE_EVENT:
		KdPrint(("Event IOCTL\n"));
		
		WfpNotify(WdfIoQueueGetDevice(Queue));

		status = STATUS_SUCCESS;
		break;

	case IOCTL_WFP_SET_DEVICE_CONTEXT_DATA:
		KdPrint(("Setting device context data."));

		if (InputBufferLength >= sizeof(USERAPP_IOCTL_IN_BUF)) {
			KdPrint(("Data in input buffer received.\n"));

			PVOID buffer;
			PUSERAPP_IOCTL_IN_BUF pUserData;

			status = WdfRequestRetrieveInputBuffer(Request,
												   sizeof(LONG),
												   &buffer,
												   NULL);

			if (!NT_SUCCESS(status)) {
				KdPrint(("Failed to retrive input buffer.\n"));
			}

			pUserData = (PUSERAPP_IOCTL_IN_BUF)buffer;
			KdPrint(("PID received from useland application: %ld\n", pUserData->UserModeAppProcessId));

			InterlockedExchange(&devContext->UserModeAppProcessId, 
								pUserData->UserModeAppProcessId);

			status = STATUS_SUCCESS;
		}

		break;

	default:
		KdPrint(("IOCTL: Invalid device control code.\n"));
		break;
	}

	KdPrint(("Completing IOCTL request.\n"));
	WdfRequestCompleteWithInformation(Request, status, info);
}

NTSTATUS
InitializeQueue(
	WDFDEVICE device
)
{
	NTSTATUS status;
	PWFP_DEVICE_CONTEXT devContext;

	/*
	   Now that our device has been created, get our per-device-instance
	   storage area
	*/ 
	devContext = WfpGetContextFromDevice(device);

	/*
	   Initialize the sequence number we'll use for something to 
	   return during notification
	*/ 
	devContext->Sequence = 1;
	devContext->UserModeAppProcessId = 0;
	
	/*
		'WdfDeviceCreateSymbolicLink' can be used with both PnP and 
		non-PnP drivers. 'Device Interfaces' in WDF can be used only
		with PnP drivers. Assigning device name which creating device 
		is important for 'WdfDeviceCreateSymbolicLink' to create symbolic
		link(else driver initialization will fail).
	*/
	status = WdfDeviceCreateSymbolicLink(device,
		&DEVICE_SYMBOLIC_NAME);

	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	/*
		Setup default queue to handle incoming 
		IO requests. 

		Device IO control requests from user application
		are received in this queue. Depending upon the control
		code requests are either completed or forwaded to 
		notification queue.

		Set parallel dispatching level to handle multiple requests
		simultaneously.
	*/
	WDF_IO_QUEUE_CONFIG queueConfig;
	WDF_OBJECT_ATTRIBUTES attributes;
	//WDFQUEUE queue;

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, 
										   WdfIoQueueDispatchParallel);
	queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
	queueConfig.PowerManaged = WdfFalse;

	// Call in PASSIVE_LEVEL
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ExecutionLevel = WdfExecutionLevelPassive;

	status = WdfIoQueueCreate(
		device,
		&queueConfig,
		&attributes,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	/*
		Create Notification queue to handle event notification
		to user application.

		Use manual dispatching to hold the request.
	*/

	// Set queueConfig to default value
	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
							 WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device, 
							  &queueConfig, 
							  WDF_NO_OBJECT_ATTRIBUTES, 
							  &devContext->NotificationQueue);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	return status;
}