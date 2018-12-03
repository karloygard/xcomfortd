/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Reverse engineered from a variety of sources, for the purpose of
 *  interoperability.
 *
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _CKOZ0014_H_
#define _CKOZ0014_H_

// Known commands that the CKOZ 00/14 understands

enum mci_pt_action
{
    MGW_PT_TX           = 0xB1,
    MGW_PT_CONFIG       = 0xB2,
    MGW_PT_RX           = 0xC1,
    MGW_PT_STATUS       = 0xC3,
    MGW_PT_FW           = 0xD1  // Messages relating to firmware
};

/* Events that can be sent to datapoints.  Not all events are valid
   for all devices. */

enum mci_tx_event
{
    MGW_TE_SWITCH         = 0x0a,
    MGW_TE_REQUEST        = 0x0b, // Requests MSG_STATUS event from the datapoint
    MGW_TE_DIM            = 0x0d,
    MGW_TE_JALO           = 0x0e, /* For dimmer, boolean value indicates direction.
                                     For shutter, see mci_sb_command */
    MGW_TE_INT16_1POINT   = 0x11,
    MGW_TE_FLOAT          = 0x1a,
    MGW_TE_RM_TIME        = 0x2a,
    MGW_TE_RM_DATE        = 0x2b,
    MGW_TE_RC_DATA        = 0x2c,
    MGW_TE_UINT32         = 0x30,
    MGW_TE_UINT32_1POINT  = 0x31,
    MGW_TE_UINT32_2POINT  = 0x32,
    MGW_TE_UINT32_3POINT  = 0x33,
    MGW_TE_UINT16         = 0x40,
    MGW_TE_UINT16_1POINT  = 0x41,
    MGW_TE_UINT16_2POINT  = 0x42,
    MGW_TE_UINT16_3POINT  = 0x43,
    MGW_TE_DIMPLEX_CONFIG = 0x44,
    MGW_TE_DIMPLEX_TEMP   = 0x45,
    MGW_TE_HRV_INR        = 0x46,
    MGW_TE_PUSHBUTTON     = 0x50,
    MGW_TE_BASIC_MODE     = 0x80,
    SET_DIRECT_ON         = 0xa0
};

/* Events that can be received from datapoints.

   These are events that are known by MRF, they may not all be
   applicable to datapoints. */

enum mci_rx_event
{
    MSG_ACK             = 0x01,
    MSG_STAY_ONLINE     = 0x09,
    MSG_ALLIVE          = 0x11,
    MSG_GET_OFFLINE     = 0x18,
    MSG_GET_EEPROM      = 0x30,
    MSG_SET_EEPROM      = 0x31,
    MSG_GET_CRC         = 0x32,
    MSG_TIME            = 0x37,
    MSG_DATE            = 0x38,
    MSG_PAKET           = 0x39,
    MSG_KILL            = 0x43,
    MSG_FACTORY         = 0x44,
    MSG_ON              = 0x50,
    MSG_OFF             = 0x51,
    MSG_SWITCH_ON       = 0x52,
    MSG_SWITCH_OFF      = 0x53,
    MSG_UP_PRESSED      = 0x54,
    MSG_UP_RELEASED     = 0x55,
    MSG_DOWN_PRESSED    = 0x56,
    MSG_DOWN_RELEASED   = 0x57,
    MSG_PWM             = 0x59,
    MSG_FORCED          = 0x5a,
    MSG_SINGLE_ON       = 0x5b,
    MSG_TOGGLE          = 0x61,
    MSG_VALUE           = 0x62,
    MSG_ZU_KALT         = 0x63,
    MSG_ZU_WARM         = 0x64,
    MSG_STATUS          = 0x70,
    MSG_STATUS_APPL     = 0x71,
    MSG_STATUS_REQ_APPL = 0x72,
    MSG_BASIC_MODE      = 0x80
};

/* Commands that can be sent to the stick itself. These are sent with
   the MGW_PT_CONFIG message and is received via the MGW_PT_STATUS
   message. */

enum mgw_cf_type
{
    MGW_CT_CONNEX          = 0x02,
    MGW_CT_RS232_BAUD      = 0x03,
    MGW_CT_SEND_OK_MRF     = 0x04,
    MGW_CT_RS232_FLOW      = 0x05,
    MGW_CT_RS232_CRC       = 0x06,
    MGW_CT_TIMEACCOUNT     = 0x0a,
    MGW_CT_COUNTER_RX      = 0x0b,
    MGW_CT_COUNTER_TX      = 0x0c,
    MGW_CT_SERIAL          = 0x0e,
    MGW_CT_LED             = 0x0f,
    MGW_CT_LED_DIM         = 0x1a,
    MGW_CT_RELEASE         = 0x1b,
    MGW_CT_SEND_CLASS      = 0x1d,
    MGW_CT_SEND_RFSEQNO    = 0x1e,
    MGW_CT_BACK_TO_FACTORY = 0x1f
};

enum mgw_st_type
{
    MGW_STT_CONNEX          = 0x02,
    MGW_STT_RS232_BAUD      = 0x03,
    MGW_STT_RS232_FLOW      = 0x05,
    MGW_STT_RS232_CRC       = 0x06,
    MGW_STT_ERROR           = 0x09,
    MGW_STT_TIMEACCOUNT     = 0x0a,
    MGW_STT_SEND_OK_MRF     = 0x0d,
    MGW_STT_SERIAL          = 0x0e,
    MGW_STT_LED             = 0x0f,
    MGW_STT_LED_DIM         = 0x1a,
    MGW_STT_RELEASE         = 0x1b,
    MGW_STT_OK              = 0x1c,
    MGW_STT_SEND_CLASS      = 0x1d,
    MGW_STT_SEND_RFSEQNO    = 0x1e
};

enum mstt_error
{
    MGW_STS_GENERAL         = 0x00,
    MGW_STS_UNKNOWN         = 0x01,
    MGW_STS_DP_OOR          = 0x02,
    MGW_STS_BUSY_MRF        = 0x03,
    MGW_STS_BUSY_MRF_RX     = 0x04,
    MGW_STS_TX_MSG_LOST     = 0x05,
    MGW_STS_NO_ACK          = 0x06
   
};



// Battery status reported by datapoints

enum mgw_rx_battery
{
    MGW_RB_NA           = 0x0,
    MGW_RB_0            = 0x1,
    MGW_RB_25           = 0x2,
    MGW_RB_50           = 0x3,
    MGW_RB_75           = 0x4,
    MGW_RB_100          = 0x5,
    MGW_RB_PWR          = 0x10
};

// Shutter status reported by datapoints

enum mci_shutter_status
{
    SHUTTER_STOPPED     = 0x00,
    SHUTTER_UP          = 0x01,
    SHUTTER_DOWN        = 0x02
};

// Commands for shutters

enum mci_sb_command
{
    MGW_TED_CLOSE      = 0x00,
    MGW_TED_OPEN       = 0x01,
    MGW_TED_JSTOP      = 0x02,
    MGW_TED_SETP_CLOSE = 0x10,
    MGW_TED_SETP_OPEN  = 0x11
};

/* Data types that can be received from a datapoint

   These are events that are known by MRF, they may not all
   be applicable to datapoints. */

enum mci_rx_datatype
{
    NO_DATA                  = 0x00,
    PERCENT                  = 0x01,
    UINT8                    = 0x02,
    INT16_1POINT             = 0x03,
    FLOAT                    = 0x04,
    UINT16                   = 0x0d,
    UINT32                   = 0x0e,
    UINT32_1POINT            = 0x0f,
    UINT32_2POINT            = 0x10,
    UINT32_3POINT            = 0x11,
    RC_DATA                  = 0x17,
    DATA_TYPE_TIME           = 0x1e,
    DATA_TYPE_DATE           = 0x1f,
    UINT16_1POINT            = 0x21,
    UINT16_2POINT            = 0x22,
    UINT16_3POINT            = 0x23,
    ROSETTA                  = 0x35,
    HRV_OUT                  = 0x37
};

#pragma pack(push,1)

// Message format 

struct xc_ci_message
{
    unsigned char      message_size;
    unsigned char      type;
    union
    {
	struct
	{
	    unsigned char  datapoint;
	    unsigned char  tx_event;
	    int            value;
	    unsigned char  seq_and_pri;
	}                  packet_tx;
	struct
	{
	    unsigned char  type;
	    unsigned char  mode;
	}                  pt_config;
	struct
	{
	    unsigned char  datapoint;
	    unsigned char  rx_event;
	    unsigned char  rx_data_type;
	    int            value;
	    unsigned char  unknown;
	    unsigned char  rssi;            // range 0 - 120
	    unsigned char  battery;         // see BatteryStatus
	    unsigned char  seqno;           // 0-15 monotonously increasing
	}                  packet_rx;
	struct
	{
	    unsigned char  type;
	    unsigned char  status;
            int            data;
	}                  pt_status;
    };
};

#pragma pack(pop)

typedef void (*xc_recv_fn)(void* user_data,
			   enum mci_rx_event,
			   int,
			   enum mci_rx_datatype,
			   int,
			   int,
			   enum mgw_rx_battery,
			   int);

typedef void (*xc_ack_fn)(void* user_data,
			  int success,
			  int seq_no,
			  int extra);

typedef void (*xc_relno_fn)(void* user_data,
			    int status,
			    unsigned int rf_major,
			    unsigned int rf_minor,
			    unsigned int usb_major,
			    unsigned int usb_minor);

struct xc_parse_data {
    xc_ack_fn ack;
    xc_recv_fn recv;
    xc_relno_fn relno;
    void* user_data;
};

void xc_parse_packet(const unsigned char* buffer, size_t size, xc_parse_data* data);

const char* xc_shutter_status_name(int state);

const char* xc_rssi_status_name(int rssi);
const char* xc_battery_status_name(enum mgw_rx_battery state);
const char* xc_rxevent_name(enum mci_rx_event event);

void xc_make_jalo_msg(char* buffer, int datapoint, mci_sb_command cmd, int message_id);
void xc_make_dim_msg(char* buffer, int datapoint, int value, int message_id);
void xc_make_switch_msg(char* buffer, int datapoint, int on, int message_id);
void xc_make_request_msg(char* buffer, int datapoint, int message_id);
void xc_make_config_msg(char* buffer, int type, int mode);

#endif
