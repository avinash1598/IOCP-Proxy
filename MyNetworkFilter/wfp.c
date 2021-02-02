#include "wdf_main.h"
#include "wfp.h"
#include "ioctl.h"
#include "wfp_driver_src.h"

HANDLE FilterEngineHandle;
PDEVICE_OBJECT gWdmDevice;

UINT32 RegCalloutId = 0;
UINT32 AddCalloutId = 0;
UINT64 FilterId = 0;

LONG GetUserAppProcessId()
{
	PWFP_DEVICE_CONTEXT devContext;
	devContext = WfpGetContextFromDevice(*GGetWDFDevice());
	return devContext->UserModeAppProcessId;
}

VOID
UnInitializeWfp()
{
	KdPrint(("UnInitializeWfp -> Uninitializing WFP"));

	if (FilterEngineHandle != NULL) {
		if (FilterId != 0) {
			FwpmFilterDeleteById(FilterEngineHandle, FilterId);
			//FwpmSubLayerDeleteByKey(FilterEngineHandle, &WFP_SAMPLE_SUBLAYER_GUID);
		}

		FwpmSubLayerDeleteByKey(FilterEngineHandle, &WFP_SAMPLE_SUBLAYER_GUID);

		if (AddCalloutId != 0) {
			FwpmCalloutDeleteById(FilterEngineHandle, AddCalloutId);
		}

		if (RegCalloutId != 0) {
			FwpsCalloutUnregisterById(RegCalloutId);
		}

		FwpmEngineClose(FilterEngineHandle);
	}
}

/*
	Registered Callout callback function
*/
VOID
ClassifyCallback
(
	const FWPS_INCOMING_VALUES0* inFixedValues,
	const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
	VOID* layerData,
	const VOID* classifyContext,
	const FWPS_FILTER1* filter,
	UINT64  flowContext,
	FWPS_CLASSIFY_OUT0* classifyOut
)
{
	NTSTATUS status;
	HANDLE redirectHandle = NULL;
	HANDLE redirectRecords = inMetaValues->redirectRecords;
	FWPS_CONNECTION_REDIRECT_STATE redirectState;
	VOID *redirectContext;
	UINT64 classifyHandle = 0;
	FWPS_CONNECT_REQUEST0* pModifiedLayer;

	UNICODE_STRING LOCAL_REDIRECT_IP;
	UINT16 LOCAL_REDIRECT_PORT = REDIRECT_PORT;

	PWSTR terminator;
	
	LONG proxyAppProcessID = GetUserAppProcessId();
	LONG incomingProcessID = 0;
	UINT32 timesRedirected = 0;
	SOCKADDR_STORAGE* pSockAddrStorage = 0;
	
	UNREFERENCED_PARAMETER(inFixedValues);
	UNREFERENCED_PARAMETER(inMetaValues);
	UNREFERENCED_PARAMETER(layerData);
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(flowContext);
	UNREFERENCED_PARAMETER(classifyOut);

	KdPrint(("\n==========================================================================\n"));
	KdPrint(("\nReceived Network Packet\n"));
	KdPrint(("\n==========================================================================\n"));

	incomingProcessID = (DWORD)inMetaValues->processId;
	KdPrint(("\nProcess ID: %ld\n", incomingProcessID));

	ULONG remoteIP = inFixedValues->incomingValue
		[FWPS_FIELD_ALE_CONNECT_REDIRECT_V4_IP_REMOTE_ADDRESS].value.uint32;
	UINT16 remotePort = inFixedValues->incomingValue
		[FWPS_FIELD_ALE_CONNECT_REDIRECT_V4_IP_REMOTE_PORT].value.uint16;
	
	KdPrint(("Destionation address: %d.%d.%d.%d:%ld\n", ((remoteIP >> 24) & 0xff),
													    ((remoteIP >> 16) & 0xff),
													    ((remoteIP >> 8) & 0xff),
													    ((remoteIP >> 0) & 0xff),
													    remotePort));

	if (!(classifyOut->rights & FWPS_RIGHT_ACTION_WRITE))
	{
		// Return without specifying an action
		return;
	}
	
	UINT16 sourcePort = inFixedValues->incomingValue
		[FWPS_FIELD_ALE_CONNECT_REDIRECT_V4_IP_LOCAL_PORT].value.uint16;

	if (LOCAL_REDIRECT_PORT == sourcePort)
	{
		KdPrint(("Not redirecting packets on port 1598 to avoid circular loop.\n"));
		classifyOut->actionType = FWP_ACTION_PERMIT;
		goto Exit;
	}

	/*
		Check if packet is coming from proxy application.
	*/
	if (proxyAppProcessID == incomingProcessID)
	{
		KdPrint(("Not redirecting packets from proxy application.\n"));
		classifyOut->actionType = FWP_ACTION_PERMIT;
		goto Exit;
	}

	status = FwpsRedirectHandleCreate0(&WFP_ALE_REDIRECT_CALLOUT_V4, 
									   0, 
									   &redirectHandle);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create redirect handle.\n"));
		goto Exit;
	}
	
	redirectState = FwpsQueryConnectionRedirectState0(redirectRecords, 
													  redirectHandle, 
													  &redirectContext);
	if (redirectState == FWPS_CONNECTION_REDIRECTED_BY_SELF) {
		KdPrint(("Connection already redirected by self.\n"));
		classifyOut->actionType = FWP_ACTION_PERMIT;
		goto Exit;
	}

	status = FwpsAcquireClassifyHandle0((VOID*)classifyContext, 
										0, 
										&classifyHandle);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to acquire classify handle.\n"));
		goto Exit;
	}

	status = FwpsAcquireWritableLayerDataPointer0(classifyHandle, 
												  filter->filterId, 
												  0, 
												  (PVOID*)&pModifiedLayer, 
												  classifyOut);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to aquire writable data layer pointer.\n"));
		goto Exit;
	}

	/*
		Perform this check to avoid infinte redirection from multiple callouts.
	*/
	for (FWPS_CONNECT_REQUEST* pConnectRequest = pModifiedLayer->previousVersion;
		pConnectRequest;
		pConnectRequest = pConnectRequest->previousVersion)
	{
		if (pConnectRequest->modifierFilterId == filter->filterId)
			timesRedirected++;

		if (timesRedirected > 0)
		{
			KdPrint(("Redirection already done for this connection. Not redirecting this packet.\n"));
			classifyOut->actionType = FWP_ACTION_PERMIT;
			goto Exit;
		}
	}
	
	KdPrint(("Printing randomly for debugging %ld \n", pModifiedLayer->localRedirectTargetPID));

	//Packet modification step begins here.
	KdPrint(("Redirecting request to proxy service...\n"));
	KdPrint(("Redirecting traffic to IP: %s:%ld.\n", REDIRECT_IP, REDIRECT_PORT));

	//Store original destination ip and port for local proxy to query
	HLPR_NEW_ARRAY(pSockAddrStorage,
				   SOCKADDR_STORAGE,
				   2,
				   WFP_CALLOUT_DRIVER_TAG);

	if (pSockAddrStorage == 0)
	{
		classifyOut->actionType = FWP_ACTION_PERMIT;

		HLPR_DELETE_ARRAY(pSockAddrStorage, 
						  WFP_CALLOUT_DRIVER_TAG);
		goto Exit;
	}

	RtlCopyMemory(&(pSockAddrStorage[0]), 
				  &(pModifiedLayer->remoteAddressAndPort), 
				  sizeof(SOCKADDR_STORAGE));

	RtlCopyMemory(&(pSockAddrStorage[1]),
	 			  &(pModifiedLayer->localAddressAndPort),
				  sizeof(SOCKADDR_STORAGE));

	// WFP will take ownership of this memory and free it when the flow / redirection terminates
	pModifiedLayer->localRedirectContext = pSockAddrStorage;
	pModifiedLayer->localRedirectContextSize = sizeof(pSockAddrStorage) * 2;

	//Modify Remote IP address
	RtlInitUnicodeString(&LOCAL_REDIRECT_IP, REDIRECT_IP);
	status = RtlIpv4StringToAddressW((PCWSTR)LOCAL_REDIRECT_IP.Buffer,
									 TRUE, 
									 &terminator, 
									 &((SOCKADDR_IN*)&pModifiedLayer->remoteAddressAndPort)->sin_addr);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to modify ip address.\n"));
		goto Exit;
	}

	//Modify remote port to local port
	LOCAL_REDIRECT_PORT =  RtlUshortByteSwap(LOCAL_REDIRECT_PORT);
	((SOCKADDR_IN*)&pModifiedLayer->remoteAddressAndPort)->sin_port = LOCAL_REDIRECT_PORT;

	pModifiedLayer->localRedirectHandle = redirectHandle;
	pModifiedLayer->localRedirectTargetPID = proxyAppProcessID;
	
	//Apply changes
	FwpsApplyModifiedLayerData0(classifyHandle, 
								(PVOID)pModifiedLayer, 
								FWPS_CLASSIFY_FLAG_REAUTHORIZE_IF_MODIFIED_BY_OTHERS);

Exit:
	// Check whether the FWPS_RIGHT_ACTION_WRITE flag should be cleared
	if (filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
	{
		// Clear the FWPS_RIGHT_ACTION_WRITE flag
		classifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
	}

	if (classifyHandle) {
		FwpsReleaseClassifyHandle0(classifyHandle);
	}

	if (redirectHandle != NULL) {
		FwpsRedirectHandleDestroy0(redirectHandle);
	}

	KdPrint(("\n**************************************************************************\n"));
}

NTSTATUS
NotifyCallback
(
	FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	const GUID* filterKey,
	const FWPS_FILTER1* filter
)
{
	UNREFERENCED_PARAMETER(notifyType);
	UNREFERENCED_PARAMETER(filterKey);
	UNREFERENCED_PARAMETER(filter);

	return STATUS_SUCCESS;
}

NTSTATUS
FlowDeleteCallback
(
	UINT16  layerId,
	UINT32  calloutId,
	UINT64  flowContext
)
{
	UNREFERENCED_PARAMETER(layerId);
	UNREFERENCED_PARAMETER(calloutId);
	UNREFERENCED_PARAMETER(flowContext);

	return STATUS_SUCCESS;
}

NTSTATUS
WfpRegisterCallout()
{
	FWPS_CALLOUT1 sCallout = { 0 };

	sCallout.calloutKey = WFP_ALE_REDIRECT_CALLOUT_V4;
	sCallout.flags = 0;
	sCallout.classifyFn = ClassifyCallback;
	sCallout.notifyFn = NotifyCallback;
	sCallout.flowDeleteFn = FlowDeleteCallback;

	return FwpsCalloutRegister1(gWdmDevice, &sCallout, &RegCalloutId);

}

NTSTATUS
WfpAddCallout()
{
	FWPM_CALLOUT mCallout = { 0 };

	mCallout.flags = 0;
	mCallout.displayData.name = L"RanjanNetworkFilterWfpCallout";
	mCallout.displayData.description = L"RanjanNetworkFilterWfpCallout";
	//mCallout.calloutId = RegCalloutId;
	mCallout.calloutKey = WFP_ALE_REDIRECT_CALLOUT_V4;
	mCallout.applicableLayer = FWPM_LAYER_ALE_CONNECT_REDIRECT_V4;

	return FwpmCalloutAdd(FilterEngineHandle, &mCallout, NULL, &AddCalloutId);
}

NTSTATUS
WfpAddSublayer()
{
	FWPM_SUBLAYER mSubLayer = { 0 };

	mSubLayer.flags = 0;
	mSubLayer.displayData.name = L"RanjanNetworkFilterWfpSubLayer";
	mSubLayer.displayData.description = L"RanjanNetworkFilterWfpSubLayer";
	mSubLayer.subLayerKey = WFP_SAMPLE_SUBLAYER_GUID;
	mSubLayer.weight = 65500;

	return FwpmSubLayerAdd(FilterEngineHandle, &mSubLayer, NULL);
}

NTSTATUS
WfpAddFilter()
{
	FWPM_FILTER mFilter = { 0 };
	FWPM_FILTER_CONDITION filterConditions[2] = { 0 };
	//FWP_V4_ADDR_AND_MASK loopbackIP = { 3232236929, 4294967040 };
	//FWP_V4_ADDR_AND_MASK remoteIP = { 310913750, 4294967040 };
	
	mFilter.displayData.name = L"RanjanNetworkFilterWfpFilter";
	mFilter.displayData.description = L"RanjanNetworkFilterWfpFilter";
	mFilter.layerKey = FWPM_LAYER_ALE_CONNECT_REDIRECT_V4;
	mFilter.subLayerKey = WFP_SAMPLE_SUBLAYER_GUID;
	mFilter.weight.type = FWP_EMPTY; // auto-weight.
	mFilter.numFilterConditions = 2;
	mFilter.filterCondition = filterConditions;
	mFilter.action.type = FWP_ACTION_CALLOUT_TERMINATING;// use this for inspection -> FWP_ACTION_CALLOUT_INSPECTION
	mFilter.action.calloutKey = WFP_ALE_REDIRECT_CALLOUT_V4;

	
	filterConditions[1].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
	filterConditions[1].matchType = FWP_MATCH_EQUAL;
	filterConditions[1].conditionValue.type = FWP_UINT16;
	filterConditions[1].conditionValue.uint16 = 80;
	
	//filterConditions[2].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
	//filterConditions[2].matchType = FWP_MATCH_EQUAL;
	//filterConditions[2].conditionValue.type = FWP_UINT16;
	//filterConditions[2].conditionValue.uint16 = 443;

	filterConditions[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
	filterConditions[0].matchType = FWP_MATCH_EQUAL;
	filterConditions[0].conditionValue.type = FWP_UINT8;
	filterConditions[0].conditionValue.uint8 = IPPROTO_TCP;
	
	/*
	filterConditions[2].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS;
	filterConditions[2].matchType = FWP_MATCH_NOT_EQUAL;
	filterConditions[2].conditionValue.type = FWP_V4_ADDR_MASK;
	filterConditions[2].conditionValue.v4AddrMask = &loopbackIP;

	//Temporary filter for testing.
	filterConditions[3].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
	filterConditions[3].matchType = FWP_MATCH_EQUAL;
	filterConditions[3].conditionValue.type = FWP_V4_ADDR_MASK;
	filterConditions[3].conditionValue.v4AddrMask = &remoteIP;
	*/

	return FwpmFilterAdd(FilterEngineHandle, &mFilter, NULL, &FilterId);
}

/*
	Initialize Windows Filtering Platform
*/
NTSTATUS
InitializeWfp(
	PDEVICE_OBJECT pDeviceObject
)
{
	NTSTATUS status;
	gWdmDevice = pDeviceObject;

	KdPrint(("MyNetworkFilter: Opening Filter engine... \n"));
	status = FwpmEngineOpen0(
		NULL,
		RPC_C_AUTHN_WINNT,
		NULL,
		NULL,
		&FilterEngineHandle
	);

	if (!NT_SUCCESS(status))
	{
		goto Exit;
	}

	KdPrint(("MyNetworkFilter: Successfully obtained Filter Engine Handle. \n"));
	KdPrint(("MyNetworkFilter: Registering Callout... \n"));
	
	status = WfpRegisterCallout();

	if (!NT_SUCCESS(status))
	{
		goto Exit;
	}

	KdPrint(("MyNetworkFilter: Successfully Registered Callout.\n"));
	KdPrint(("MyNetworkFilter: Adding callout to the filter engine...\n"));
	
	status = WfpAddCallout();

	if (!NT_SUCCESS(status))
	{
		goto Exit;
	}

	KdPrint(("MyNetworkFilter: Successfully added callout to the filter engine.\n"));
	KdPrint(("MyNetworkFilter: Adding sublayer to the filter engine...\n"));
	
	status = WfpAddSublayer();

	if (!NT_SUCCESS(status))
	{
		goto Exit;
	}

	KdPrint(("MyNetworkFilter: Successfully added sublayer to the filter engine.\n"));
	KdPrint(("MyNetworkFilter: Adding filter to the filter engine...\n"));
	
	status = WfpAddFilter();

	if (!NT_SUCCESS(status))
	{
		goto Exit;
	}

	KdPrint(("MyNetworkFilter: Successfully added filter to the filter engine.\n"));

Exit:

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Error occured while initializing. Error code: 0x%x \n", status));
		UnInitializeWfp();
	}

	return status;
}
