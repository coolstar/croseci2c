#ifndef __CROS_EC_REGS_H__
#define __CROS_EC_REGS_H__

#define BIT(nr) (1UL << (nr))

typedef enum ADDRESS_MODE
{
    AddressMode7Bit,
    AddressMode10Bit
}
ADDRESS_MODE, * PADDRESS_MODE;

typedef struct PBC_TARGET_SETTINGS
{
    // TODO: Update this structure to include other
    //       target settings needed to configure the
    //       controller (i.e. connection speed, phase/
    //       polarity for SPI).

    ADDRESS_MODE                  AddressMode;
    USHORT                        Address;
    ULONG                         ConnectionSpeed;
}
PBC_TARGET_SETTINGS, * PPBC_TARGET_SETTINGS;

/////////////////////////////////////////////////
//
// Context definitions.
//
/////////////////////////////////////////////////

typedef struct PBC_TARGET   PBC_TARGET, * PPBC_TARGET;

struct PBC_TARGET
{
    // TODO: Update this structure with variables that 
    //       need to be stored in the target context.

    // Handle to the SPB target.
    SPBTARGET                      SpbTarget;

    // Target specific settings.
    PBC_TARGET_SETTINGS            Settings;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PBC_TARGET, GetTargetContext);

#define I2C_SERIAL_BUS_TYPE 0x01
#define I2C_SERIAL_BUS_SPECIFIC_FLAG_10BIT_ADDRESS 0x0001

/* I2C passthru command */

#define EC_CMD_I2C_PASSTHRU 0x009E

/* Read data; if not present, message is a write */
#define EC_I2C_FLAG_READ	BIT(15)

/* Mask for address */
#define EC_I2C_ADDR_MASK	0x3ff

#define EC_I2C_STATUS_NAK	BIT(0) /* Transfer was not acknowledged */
#define EC_I2C_STATUS_TIMEOUT	BIT(1) /* Timeout during transfer */

/* Any error */
#define EC_I2C_STATUS_ERROR	(EC_I2C_STATUS_NAK | EC_I2C_STATUS_TIMEOUT)

/**
 * struct cros_ec_command - Information about a ChromeOS EC command.
 * @version: Command version number (often 0).
 * @command: Command to send (EC_CMD_...).
 * @outsize: Outgoing length in bytes.
 * @insize: Max number of bytes to accept from the EC.
 * @result: EC's response to the command (separate from communication failure).
 * @data: Where to put the incoming data from EC and outgoing data to EC.
 */
struct cros_ec_command {
    UINT32 version;
    UINT32 command;
    UINT32 outsize;
    UINT32 insize;
    UINT32 result;
    UINT8 data[];
};

#include <pshpack2.h>

struct ec_params_i2c_passthru_msg {
    UINT16 addr_flags;	/* I2C slave address (7 or 10 bits) and flags */
    UINT16 len;		/* Number of bytes to read or write */
};

struct ec_params_i2c_passthru {
    UINT8 port;		/* I2C port number */
    UINT8 num_msgs;	/* Number of messages */
    struct ec_params_i2c_passthru_msg msg[];
    /* Data to write for all messages is concatenated here */
};

#include <poppack.h>
#include <pshpack1.h>

struct ec_response_i2c_passthru {
    UINT8 i2c_status;	/* Status flags (EC_I2C_STATUS_...) */
    UINT8 num_msgs;	/* Number of messages processed */
    UINT8 data[];		/* Data read by messages concatenated here */
};

#include <poppack.h>

NTSTATUS
MdlChainGetByte(
    PMDL pMdlChain,
    size_t Length,
    size_t Index,
    UCHAR* pByte);

NTSTATUS
MdlChainSetByte(
    PMDL pMdlChain,
    size_t Length,
    size_t Index,
    UCHAR Byte
);

#endif /* __CROS_EC_REGS_H__ */