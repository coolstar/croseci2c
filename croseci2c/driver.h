#if !defined(_CROSECI2C_H_)
#define _CROSECI2C_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>
#include <acpiioct.h>
#include <SPBCx.h>

#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include <pshpack1.h>

typedef struct _PNP_I2C_SERIAL_BUS_DESCRIPTOR {
    PNP_SERIAL_BUS_DESCRIPTOR SerialBusDescriptor;
    ULONG ConnectionSpeed;
    USHORT SlaveAddress;
    // follwed by optional Vendor Data
    // followed by PNP_IO_DESCRIPTOR_RESOURCE_NAME
} PNP_I2C_SERIAL_BUS_DESCRIPTOR, * PPNP_I2C_SERIAL_BUS_DESCRIPTOR;

#include <poppack.h>

#include "croseci2c.h"

//
// String definitions
//

#define DRIVERNAME                 "croseci2c.sys: "

#define CROSECI2C_POOL_TAG            (ULONG) 'CRI2'
#define CROSECI2C_HARDWARE_IDS        L"CoolStar\\GOOG0012\0\0"
#define CROSECI2C_HARDWARE_IDS_LENGTH sizeof(CROSECI2C_HARDWARE_IDS)

#define NTDEVICE_NAME_STRING       L"\\Device\\GOOG0012"
#define SYMBOLIC_NAME_STRING       L"\\DosDevices\\GOOG0012"

#define true 1
#define false 0

typedef struct _CROSEC_COMMAND {
    UINT32 Version;
    UINT32 Command;
    UINT32 OutSize;
    UINT32 InSize;
    UINT32 Result;
    UINT8 Data[];
} CROSEC_COMMAND, *PCROSEC_COMMAND;

typedef
NTSTATUS
(*PCROSEC_CMD_XFER_STATUS)(
    IN      PVOID Context,
    OUT     PCROSEC_COMMAND Msg
    );

typedef
BOOLEAN
(*PCROSEC_CHECK_FEATURES)(
    IN PVOID Context,
    IN INT Feature
    );

DEFINE_GUID(GUID_CROSEC_INTERFACE_STANDARD,
    0xd7062676, 0xe3a4, 0x11ec, 0xa6, 0xc4, 0x24, 0x4b, 0xfe, 0x99, 0x46, 0xd0);

/*DEFINE_GUID(GUID_DEVICE_PROPERTIES,
    0xdaffd814, 0x6eba, 0x4d8c, 0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01);*/ //Windows defender false positive

//
// Interface for getting and setting power level etc.,
//
typedef struct _CROSEC_INTERFACE_STANDARD {
    INTERFACE                        InterfaceHeader;
    PCROSEC_CMD_XFER_STATUS          CmdXferStatus;
    PCROSEC_CHECK_FEATURES           CheckFeatures;
} CROSEC_INTERFACE_STANDARD, * PCROSEC_INTERFACE_STANDARD;

typedef struct _CROSECI2C_CONTEXT
{
	//
	// Handle back to the WDFDEVICE
	//

	WDFDEVICE FxDevice;

    PVOID CrosEcBusContext;

    PCROSEC_CMD_XFER_STATUS CrosEcCmdXferStatus;

    WDFIOTARGET busIoTarget;

    UINT8 busNumber;

} CROSECI2C_CONTEXT, *PCROSECI2C_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CROSECI2C_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD CrosEcI2CDriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD CrosEcI2CEvtDeviceAdd;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CrosEcI2CEvtInternalDeviceControl;

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define CrosEcI2CPrint(dbglevel, dbgcatagory, fmt, ...) {          \
    if (CrosEcI2CDebugLevel >= dbglevel &&                         \
        (CrosEcI2CDebugCatagories && dbgcatagory))                 \
		    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
		    }                                                           \
}
#else
#define CrosEcI2CPrint(dbglevel, fmt, ...) {                       \
}
#endif
#endif