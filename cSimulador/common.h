#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>

#include "sllp.h"

#define CURVE_INFO_SIZE         18
#define CURVE_CSUM_SIZE         16

#define HEADER_SIZE             SLLP_HEADER_SIZE
#define CURVE_BLOCK             SLLP_CURVE_BLOCK_SIZE
#define MAX_PAYLOAD             SLLP_MAX_PAYLOAD
#define MAX_MESSAGE             SLLP_MAX_MESSAGE
#define MAX_PAYLOAD_ENCODED     255

#define WRITABLE_MASK           0x80
#define SIZE_MASK               0x7F

#define WRITABLE                0x80
#define READ_ONLY               0x00

enum command_code
{
    CMD_QUERY_STATUS = 0x00,
    CMD_STATUS,
    CMD_QUERY_VARS_LIST,
    CMD_VARS_LIST,
    CMD_QUERY_GROUPS_LIST,
    CMD_GROUPS_LIST,
    CMD_QUERY_GROUP,
    CMD_GROUP,
    CMD_QUERY_CURVES_LIST,
    CMD_CURVES_LIST,

    CMD_READ_VAR = 0x10,
    CMD_VAR_READING,
    CMD_READ_GROUP,
    CMD_GROUP_READING,

    CMD_WRITE_VAR = 0x20,
    CMD_WRITE_GROUP = 0x22,
    CMD_BIN_OP_VAR = 0x24,
    CMD_BIN_OP_GROUP = 0x26,

    CMD_CREATE_GROUP = 0x30,
    CMD_REMOVE_ALL_GROUPS = 0x32,

    CMD_CURVE_TRANSMIT = 0x40,
    CMD_CURVE_BLOCK,
    CMD_CURVE_RECALC_CSUM,

    CMD_OK = 0xE0,
    CMD_ERR_MALFORMED_MESSAGE,
    CMD_ERR_OP_NOT_SUPPORTED,
    CMD_ERR_INVALID_ID,
    CMD_ERR_INVALID_VALUE,
    CMD_ERR_INVALID_PAYLOAD_SIZE,
    CMD_ERR_READ_ONLY,
    CMD_ERR_INSUFFICIENT_MEMORY,

    CMD_MAX
};

enum group_id
{
    GROUP_ALL_ID,
    GROUP_READ_ID,
    GROUP_WRITE_ID,

    GROUP_STANDARD_COUNT,
};

#endif  /* COMMON_H */

