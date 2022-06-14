#define DESCRIPTOR_DEF
#include "driver.h"
#include "stdint.h"

#define bool int
#define MS_IN_US 1000

static ULONG CrosEcI2CDebugLevel = 100;
static ULONG CrosEcI2CDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
__in PDRIVER_OBJECT  DriverObject,
__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, CrosEcI2CEvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
		);

	if (!NT_SUCCESS(status))
	{
		CrosEcI2CPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

static NTSTATUS GetBusInformation(
	_In_ WDFDEVICE FxDevice
) {
	PCROSECI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	WDFMEMORY outputMemory = WDF_NO_HANDLE;

	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	char* propertyStr = "google,remote-bus";

	size_t inputBufferLen = sizeof(ACPI_GET_DEVICE_SPECIFIC_DATA) + strlen(propertyStr) + 1;
	ACPI_GET_DEVICE_SPECIFIC_DATA* inputBuffer = ExAllocatePoolWithTag(NonPagedPool, inputBufferLen, CROSECI2C_POOL_TAG);
	if (!inputBuffer) {
		goto Exit;
	}
	RtlZeroMemory(inputBuffer, inputBufferLen);

	inputBuffer->Signature = IOCTL_ACPI_GET_DEVICE_SPECIFIC_DATA_SIGNATURE;

	unsigned char uuidend[] = { 0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01 };

	inputBuffer->Section.Data1 = 0xdaffd814;
	inputBuffer->Section.Data2 = 0x6eba;
	inputBuffer->Section.Data3 = 0x4d8c;
	memcpy(inputBuffer->Section.Data4, uuidend, sizeof(uuidend)); //Avoid Windows defender false positive

	strcpy(inputBuffer->PropertyName, propertyStr);
	inputBuffer->PropertyNameLength = strlen(propertyStr) + 1;

	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 8;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + sizeof(ACPI_METHOD_ARGUMENT_V1) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;
	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		&outputBuffer);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, inputBuffer, (ULONG)inputBufferLen);
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_GET_DEVICE_SPECIFIC_DATA,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		CrosEcI2CPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Error getting device data - 0x%x\n",
			status);
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE_V1 &&
		outputBuffer->Count < 1 &&
		outputBuffer->Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER &&
		outputBuffer->Argument->DataLength < 1) {
		status = STATUS_ACPI_INVALID_ARGUMENT;
		goto Exit;
	}

	pDevice->busNumber = outputBuffer->Argument->Data[0];

Exit:
	if (inputBuffer) {
		ExFreePoolWithTag(inputBuffer, CROSECI2C_POOL_TAG);
	}
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

NTSTATUS ConnectToEc(
	_In_ WDFDEVICE FxDevice
) {
	PCROSECI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	WDF_OBJECT_ATTRIBUTES objectAttributes;

	WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
	objectAttributes.ParentObject = FxDevice;

	NTSTATUS status = WdfIoTargetCreate(FxDevice,
		&objectAttributes,
		&pDevice->busIoTarget
	);
	if (!NT_SUCCESS(status))
	{
		CrosEcI2CPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Error creating IoTarget object - 0x%x\n",
			status);
		if (pDevice->busIoTarget)
			WdfObjectDelete(pDevice->busIoTarget);
		return status;
	}

	DECLARE_CONST_UNICODE_STRING(busDosDeviceName, L"\\DosDevices\\GOOG0004");

	WDF_IO_TARGET_OPEN_PARAMS openParams;
	WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
		&openParams,
		&busDosDeviceName,
		(GENERIC_READ | GENERIC_WRITE));

	openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
	openParams.CreateDisposition = FILE_OPEN;
	openParams.FileAttributes = FILE_ATTRIBUTE_NORMAL;

	CROSEC_INTERFACE_STANDARD CrosEcInterface;
	RtlZeroMemory(&CrosEcInterface, sizeof(CrosEcInterface));

	status = WdfIoTargetOpen(pDevice->busIoTarget, &openParams);
	if (!NT_SUCCESS(status))
	{
		CrosEcI2CPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Error opening IoTarget object - 0x%x\n",
			status);
		WdfObjectDelete(pDevice->busIoTarget);
		return status;
	}

	status = WdfIoTargetQueryForInterface(pDevice->busIoTarget,
		&GUID_CROSEC_INTERFACE_STANDARD,
		(PINTERFACE)&CrosEcInterface,
		sizeof(CrosEcInterface),
		1,
		NULL);
	WdfIoTargetClose(pDevice->busIoTarget);
	pDevice->busIoTarget = NULL;
	if (!NT_SUCCESS(status)) {
		CrosEcI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfFdoQueryForInterface failed 0x%x\n", status);
		return status;
	}

	pDevice->CrosEcBusContext = CrosEcInterface.InterfaceHeader.Context;
	pDevice->CrosEcCmdXferStatus = CrosEcInterface.CmdXferStatus;
	return status;
}

NTSTATUS
OnPrepareHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesRaw,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PCROSECI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);
	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	status = GetBusInformation(FxDevice);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	status = ConnectToEc(FxDevice);
	if (!NT_SUCCESS(status)) {
		return status;
	}


	(*pDevice->CrosEcCmdXferStatus)(pDevice->CrosEcBusContext, NULL);

	return status;
}

NTSTATUS
OnReleaseHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	return status;
}

NTSTATUS
OnD0Entry(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);
	NTSTATUS status = STATUS_SUCCESS;

	return status;
}

NTSTATUS
OnD0Exit(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxTargetState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxTargetState - target power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxTargetState);

	NTSTATUS status = STATUS_SUCCESS;

	return status;
}

static int ec_i2c_count_message(SPBREQUEST SpbRequest, int Count) {
	int i;
	int size;

	size = sizeof(struct ec_params_i2c_passthru);
	size += Count * sizeof(struct ec_params_i2c_passthru_msg);

	SPB_TRANSFER_DESCRIPTOR descriptor;
	for (i = 0; i < Count; i++) {
		SPB_TRANSFER_DESCRIPTOR_INIT(&descriptor);
		SpbRequestGetTransferParameters(SpbRequest,
			i,
			&descriptor,
			NULL);

		if (descriptor.Direction == SpbTransferDirectionToDevice)
			size += descriptor.TransferLength;
	}
	return size;
}

static NTSTATUS ec_i2c_construct_message(
	UINT8* buf,
	SPBTARGET SpbTarget,
	SPBREQUEST SpbRequest,
	int Count,
	UINT8 busNum,
	int *BytesTransferred) {
	PPBC_TARGET pTarget = GetTargetContext(SpbTarget);

	if (pTarget->Settings.AddressMode != AddressMode7Bit) {
		return STATUS_INVALID_ADDRESS;
	}

	struct ec_params_i2c_passthru* params;

	uint8_t *out_data = buf + sizeof(struct ec_params_i2c_passthru) +
		Count * sizeof(struct ec_params_i2c_passthru_msg);

	params = (struct ec_params_i2c_passthru*)buf;
	params->port = busNum;
	params->num_msgs = Count;

	SPB_TRANSFER_DESCRIPTOR descriptor;
	for (int i = 0; i < Count; i++) {
		PMDL mdlChain;

		SPB_TRANSFER_DESCRIPTOR_INIT(&descriptor);
		SpbRequestGetTransferParameters(SpbRequest,
			i,
			&descriptor,
			&mdlChain);

		struct ec_params_i2c_passthru_msg* msg = &params->msg[i];
		msg->len = descriptor.TransferLength;
		msg->addr_flags = pTarget->Settings.Address;

		if (descriptor.Direction == SpbTransferDirectionFromDevice) {
			msg->addr_flags |= EC_I2C_FLAG_READ;
		}
		else {
			for (int j = 0; j < msg->len; j++) {
				if (!NT_SUCCESS(MdlChainGetByte(mdlChain, msg->len, j, out_data))) {
					CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
						"Failed to copy send byte %d of %d\n",
						j, msg->len);
					return STATUS_DATA_ERROR;
				}
				out_data++;
			}
			*BytesTransferred += msg->len;
		}
	}
	return STATUS_SUCCESS;
}

static int ec_i2c_count_response(SPBREQUEST SpbRequest, int Count)
{
	int size = sizeof(struct ec_response_i2c_passthru);

	SPB_TRANSFER_DESCRIPTOR descriptor;
	for (int i = 0; i < Count; i++) {
		SPB_TRANSFER_DESCRIPTOR_INIT(&descriptor);
		SpbRequestGetTransferParameters(SpbRequest,
			i,
			&descriptor,
			NULL);

		if (descriptor.Direction == SpbTransferDirectionFromDevice)
			size += descriptor.TransferLength;
	}

	return size;
}

static NTSTATUS ec_i2c_parse_response(const UINT8* buf,
	SPBREQUEST SpbRequest,
	int* Count,
	int *BytesTransferred)
{
	const struct ec_response_i2c_passthru* resp;
	const UINT8* in_data = buf + sizeof(struct ec_response_i2c_passthru);

	resp = (const struct ec_response_i2c_passthru*)buf;
	if (resp->i2c_status & EC_I2C_STATUS_TIMEOUT)
		return STATUS_IO_TIMEOUT;
	else if (resp->i2c_status & EC_I2C_STATUS_NAK)
		return STATUS_DEVICE_DOES_NOT_EXIST;
	else if (resp->i2c_status & EC_I2C_STATUS_ERROR)
		return STATUS_IO_DEVICE_ERROR;

	/* Other side could send us back fewer messages, but not more */
	if (resp->num_msgs > *Count)
		return STATUS_PROTOCOL_NOT_SUPPORTED;
	*Count = resp->num_msgs;

	SPB_TRANSFER_DESCRIPTOR descriptor;
	for (int i = 0; i < *Count; i++) {
		PMDL mdlChain;

		SPB_TRANSFER_DESCRIPTOR_INIT(&descriptor);
		SpbRequestGetTransferParameters(SpbRequest,
			i,
			&descriptor,
			&mdlChain);

		if (descriptor.Direction == SpbTransferDirectionFromDevice) {
			size_t len = descriptor.TransferLength;

			for (int j = 0; j < len; j++) {
				if (!NT_SUCCESS(MdlChainSetByte(mdlChain, len, j, *in_data))) {
					CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
						"Failed to copy recv byte %d of %d\n",
						j, len);
					return STATUS_DATA_ERROR;
				}
				in_data++;
			}
			*BytesTransferred += len;
		}
	}
	return STATUS_SUCCESS;
}

static NTSTATUS ec_i2c_xfer(
	_In_ PCROSECI2C_CONTEXT pDevice,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ ULONG TransferCount
) {
	int request_len;
	int response_len;
	int alloc_size;
	NTSTATUS result;
	struct cros_ec_command* msg;
	int bytes_transferred = 0;

	request_len = ec_i2c_count_message(SpbRequest, TransferCount);
	if (request_len < 0) {
		CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Error constructing message %d\n",
			request_len);
		return STATUS_INTERNAL_ERROR;
	}

	response_len = ec_i2c_count_response(SpbRequest, TransferCount);
	if (response_len < 0) {
		/* Unexpected; no errors should come when NULL response */
		CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Error preparing response %d\n", response_len);
		return STATUS_INTERNAL_ERROR;
	}

	alloc_size = max(request_len, response_len);
	msg = ExAllocatePoolWithTag(NonPagedPool ,sizeof(struct cros_ec_command) + alloc_size, CROSECI2C_POOL_TAG);
	if (!msg) {
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	result = ec_i2c_construct_message(msg->data, SpbTarget, SpbRequest, TransferCount, pDevice->busNumber, &bytes_transferred);
	if (!NT_SUCCESS(result)) {
		CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Error constructing EC i2c message 0x%x\n", result);
		goto exit;
	}

	msg->version = 0;
	msg->command = EC_CMD_I2C_PASSTHRU;
	msg->outsize = request_len;
	msg->insize = response_len;
	 
	result = (*pDevice->CrosEcCmdXferStatus)(pDevice->CrosEcBusContext, msg);
	if (!NT_SUCCESS(result)) {
		CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Error transferring EC I2C Message 0x%x\n", result);
		goto exit;
	}

	int TransferredCount = TransferCount;
	result = ec_i2c_parse_response(msg->data, SpbRequest, &TransferredCount, &bytes_transferred);
	if (!NT_SUCCESS(result)) {
		goto exit;
	}

	WdfRequestSetInformation(SpbRequest, bytes_transferred);

exit:
	ExFreePoolWithTag(msg, CROSECI2C_POOL_TAG);
	return result;
}

NTSTATUS OnTargetConnect(
	_In_  WDFDEVICE  SpbController,
	_In_  SPBTARGET  SpbTarget
) {
	UNREFERENCED_PARAMETER(SpbController);

	PPBC_TARGET pTarget = GetTargetContext(SpbTarget);

	//
	// Get target connection parameters.
	//

	SPB_CONNECTION_PARAMETERS params;
	SPB_CONNECTION_PARAMETERS_INIT(&params);

	SpbTargetGetConnectionParameters(SpbTarget, &params);
	PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER  connection = (PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER)params.ConnectionParameters;

	if (connection->PropertiesLength < sizeof(PNP_SERIAL_BUS_DESCRIPTOR)) {
		CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Invalid connection properties (length = %lu, "
			"expected = %Iu)\n",
			connection->PropertiesLength,
			sizeof(PNP_SERIAL_BUS_DESCRIPTOR));
		return STATUS_INVALID_PARAMETER;
	}

	PPNP_SERIAL_BUS_DESCRIPTOR descriptor = (PPNP_SERIAL_BUS_DESCRIPTOR)connection->ConnectionProperties;
	if (descriptor->SerialBusType != I2C_SERIAL_BUS_TYPE) {
		CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Bus type %c not supported, only I2C\n",
			descriptor->SerialBusType);
		return STATUS_INVALID_PARAMETER;
	}

	PPNP_I2C_SERIAL_BUS_DESCRIPTOR i2cDescriptor = (PPNP_I2C_SERIAL_BUS_DESCRIPTOR)connection->ConnectionProperties;
	pTarget->Settings.Address = (ULONG)i2cDescriptor->SlaveAddress;

	USHORT I2CFlags = i2cDescriptor->SerialBusDescriptor.TypeSpecificFlags;

	pTarget->Settings.AddressMode = ((I2CFlags & I2C_SERIAL_BUS_SPECIFIC_FLAG_10BIT_ADDRESS) == 0) ? AddressMode7Bit : AddressMode10Bit;
	pTarget->Settings.ConnectionSpeed = i2cDescriptor->ConnectionSpeed;

	return STATUS_SUCCESS;
}

VOID OnSpbIoRead(
	_In_ WDFDEVICE SpbController,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ size_t Length
)
{
	PCROSECI2C_CONTEXT pDevice = GetDeviceContext(SpbController);

	NTSTATUS status = ec_i2c_xfer(pDevice, SpbTarget, SpbRequest, 1);
	SpbRequestComplete(SpbRequest, status);
}

VOID OnSpbIoWrite(
	_In_ WDFDEVICE SpbController,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ size_t Length
)
{
	PCROSECI2C_CONTEXT pDevice = GetDeviceContext(SpbController);

	NTSTATUS status = ec_i2c_xfer(pDevice, SpbTarget, SpbRequest, 1);
	SpbRequestComplete(SpbRequest, status);
}

VOID OnSpbIoSequence(
	_In_ WDFDEVICE SpbController,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ ULONG TransferCount
)
{

	PCROSECI2C_CONTEXT pDevice = GetDeviceContext(SpbController);

	NTSTATUS status = ec_i2c_xfer(pDevice, SpbTarget, SpbRequest, TransferCount);
	SpbRequestComplete(SpbRequest, status);
}

NTSTATUS
CrosEcI2CEvtDeviceAdd(
IN WDFDRIVER       Driver,
IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	PCROSECI2C_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	CrosEcI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"CrosEcI2CEvtDeviceAdd called\n");

	status = SpbDeviceInitConfig(DeviceInit);
	if (!NT_SUCCESS(status)) {
		CrosEcI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"SpbDeviceInitConfig failed with status code 0x%x\n", status);
		return status;
	}

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CROSECI2C_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		CrosEcI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	//
	// Bind a SPB controller object to the device.
	//

	SPB_CONTROLLER_CONFIG spbConfig;
	SPB_CONTROLLER_CONFIG_INIT(&spbConfig);

	spbConfig.PowerManaged = WdfTrue;
	spbConfig.EvtSpbTargetConnect = OnTargetConnect;
	spbConfig.EvtSpbIoRead = OnSpbIoRead;
	spbConfig.EvtSpbIoWrite = OnSpbIoWrite;
	spbConfig.EvtSpbIoSequence = OnSpbIoSequence;

	status = SpbDeviceInitialize(devContext->FxDevice, &spbConfig);
	if (!NT_SUCCESS(status))
	{
		CrosEcI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"SpbDeviceInitialize failed with status code 0x%x\n", status);

		return status;
	}

	WDF_OBJECT_ATTRIBUTES targetAttributes;
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&targetAttributes, PBC_TARGET);

	SpbControllerSetTargetAttributes(devContext->FxDevice, &targetAttributes);

	return status;
}