/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Reverse engineered from a variety of sources, for the purpose of
 *  interoperability.
 *
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <syslog.h>

#include "ckoz0014.h"

const char* xc_rxevent_name(enum mci_rx_event event)
{
    switch (event)
    {
    case MSG_ACK:             return "MSG_ACK";
    case MSG_STAY_ONLINE:     return "MSG_STAY_ONLINE";
    case MSG_ALLIVE:          return "MSG_ALLIVE";
    case MSG_GET_OFFLINE:     return "MSG_GET_OFFLINE";
    case MSG_GET_EEPROM:      return "MSG_GET_EEPROM";
    case MSG_SET_EEPROM:      return "MSG_SET_EEPROM";
    case MSG_GET_CRC:	      return "MSG_GET_CRC";
    case MSG_TIME:            return "MSG_TIME";            
    case MSG_DATE:            return "MSG_DATE";            
    case MSG_PAKET:           return "MSG_PAKET";           
    case MSG_KILL:            return "MSG_KILL";            
    case MSG_FACTORY:	      return "MSG_FACTORY";         
    case MSG_ON:              return "MSG_ON";              
    case MSG_OFF:             return "MSG_OFF";             
    case MSG_SWITCH_ON:       return "MSG_SWITCH_ON";       
    case MSG_SWITCH_OFF:      return "MSG_SWITCH_OFF";      
    case MSG_UP_PRESSED:      return "MSG_UP_PRESSED";      
    case MSG_UP_RELEASED:     return "MSG_UP_RELEASED";     
    case MSG_DOWN_PRESSED:    return "MSG_DOWN_PRESSED";    
    case MSG_DOWN_RELEASED:   return "MSG_DOWN_RELEASED";   
    case MSG_PWM:             return "MSG_PWM";             
    case MSG_FORCED:          return "MSG_FORCED";          
    case MSG_SINGLE_ON:       return "MSG_SINGLE_ON";       
    case MSG_TOGGLE:          return "MSG_TOGGLE";          
    case MSG_VALUE:           return "MSG_VALUE";           
    case MSG_ZU_KALT:         return "MSG_ZU_KALT";         
    case MSG_ZU_WARM:         return "MSG_ZU_WARM";         
    case MSG_STATUS:          return "MSG_STATUS";          
    case MSG_STATUS_APPL:     return "MSG_STATUS_APPL";     
    case MSG_STATUS_REQ_APPL: return "MSG_STATUS_REQ_APPL"; 
    default:                  return "-- unknown --";
    }
}

const char* xc_rssi_status_name(int rssi)
{
    if (rssi <= 67)
        return "good";
    else if (rssi <= 75)
        return "normal";
    else if (rssi <= 90)
        return "weak";
    else if (rssi <= 120)
        return "very weak";
    else
        return "unknown";
}

const char* xc_battery_status_name(enum mgw_rx_battery state)
{
    switch (state)
    {
    default:
    case MGW_RB_NA:  return "not available";
    case MGW_RB_0:   return "empty";
    case MGW_RB_25:  return "very weak";
    case MGW_RB_50:  return "weak";
    case MGW_RB_75:  return "good";
    case MGW_RB_100: return "new";
    case MGW_RB_PWR: return "powerline";
    }
}

const char* xc_shutter_status_name(int state)
{
    switch (state)
    {
    default:
    case SHUTTER_STOPPED:    return "stopped";
    case SHUTTER_UP:         return "up";
    case SHUTTER_DOWN:       return "down";
    }
}

void xc_parse_packet(const unsigned char* buffer, size_t size, xc_parse_data* data)
{
    struct xc_ci_message* msg = (struct xc_ci_message*) buffer;

    if (size < 2 ||
        size < msg->message_size)
	return;

    switch (msg->type)
    {
    case MGW_PT_RX:
	data->recv(data->user_data,
		   (enum mci_rx_event) msg->packet_rx.rx_event,
		   msg->packet_rx.datapoint,
		   (enum mci_rx_datatype) msg->packet_rx.rx_data_type,
		   msg->packet_rx.value,
		   msg->packet_rx.rssi,
		   (enum mgw_rx_battery) msg->packet_rx.battery);

	break;
    
    case MGW_PT_STATUS:
    {
        int i;
	int message_id = -1;

        // The ACK parsing isn't completely understood

        switch (msg->pt_status.type)
        {
        case MGW_STT_SERIAL:
            printf("serial number: %08x\n", ntohl(msg->pt_status.data));
            return;

        case MGW_STT_RELEASE:
	    data->relno(data->user_data, msg->pt_status.status, buffer[4], buffer[5], buffer[6], buffer[7]);
            return;

        case MGW_CT_COUNTER_RX:
            printf("counter rx: %08x\n", msg->pt_status.data);
            return;

        case MGW_CT_COUNTER_TX:
            printf("counter tx: %08x\n", msg->pt_status.data);
            return;

        case MGW_STT_TIMEACCOUNT:
            printf("time account: %d%%\n", buffer[4]);
            return;

        case MGW_STT_SEND_RFSEQNO:
            printf("RF sequence no flag: %d\n", msg->pt_status.status);
            return;

        default:
	    printf("received MGW_PT_STATUS(%d) [", msg->message_size);

	    for (i = 2; i < msg->message_size; ++i)
	        printf("%02hhx ", buffer[i]);

	    printf("]\n");
            return;

        case MGW_STT_OK:
	    message_id = buffer[4];
            break;

        case MGW_STT_ERROR:
            printf("error message: ");

            switch (msg->pt_status.status)
            {
            case MGW_STS_GENERAL:
                printf("general error\n");
	        message_id = (unsigned char) buffer[5];
                break;

            case MGW_STS_UNKNOWN:
                printf("unknown command\n");
	        message_id = (unsigned char) buffer[5];
                break;

            case MGW_STS_DP_OOR:
		printf("datapoint out of range\n");
		break;

            case MGW_STS_BUSY_MRF:
		printf("rf busy (tx message lost)\n");
		break;

            case MGW_STS_BUSY_MRF_RX:
		printf("rf busy (rx in progress)\n");
		break;

            case MGW_STS_TX_MSG_LOST:
		printf("tx message lost; repeat it\n");
		break;

            case MGW_STS_NO_ACK:
                printf("timeout; no ack received\n");
		message_id = (unsigned char) buffer[4];
                break;
            }
            break;
        }

	data->ack(data->user_data, buffer[2] == 0x1c, message_id >> 4);

	break;
    }

    case MGW_PT_FW:
	printf("Firmware version: %d.%02d\n", buffer[11], buffer[12]);
	break;

    default:
	printf("unprocessed: received %02x: %d\n", msg->type, msg->message_size);
	break;
    }
}

void xc_make_jalo_msg(char* buffer, int datapoint, mci_sb_command cmd, int message_id)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x9;
    message->type = MGW_PT_TX;
    message->packet_tx.datapoint = datapoint;
    message->packet_tx.tx_event = MGW_TE_JALO;
    message->packet_tx.value = cmd;
    message->packet_tx.seq_and_pri = message_id << 4;
}

void xc_make_dim_msg(char* buffer, int datapoint, int value, int message_id)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x9;
    message->type = MGW_PT_TX;
    message->packet_tx.datapoint = datapoint;
    message->packet_tx.tx_event = MGW_TE_DIM;
    message->packet_tx.value = (value << 8) + 0x40;
    message->packet_tx.seq_and_pri = message_id << 4;
}

void xc_make_switch_msg(char* buffer, int datapoint, int on, int message_id)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x9;
    message->type = MGW_PT_TX;
    message->packet_tx.datapoint = datapoint;
    message->packet_tx.tx_event = MGW_TE_SWITCH;
    message->packet_tx.value = on;
    message->packet_tx.seq_and_pri = message_id << 4;
}

void xc_make_request_msg (char* buffer, int datapoint, int message_id)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x9;
    message->type = MGW_PT_TX;
    message->packet_tx.datapoint = datapoint;
    message->packet_tx.tx_event = MGW_TE_REQUEST;
    message->packet_tx.seq_and_pri = message_id << 4;
}

void xc_make_config_msg(char* buffer, int type, int mode)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x4;
    message->type = MGW_PT_CONFIG;
    message->pt_config.type = type;
    message->pt_config.mode = mode;
}

