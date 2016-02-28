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

const char* xc_battery_status_name(enum mci_battery_status state)
{
    switch (state)
    {
    default:
    case BATTERY_NA:         return "unknown";
    case BATTERY_EMPTY:      return "empty";
    case BATTERY_WEAK:       return "weak";
    case BATTERY_AVERAGE:    return "average";
    case BATTERY_ALMOSTFULL: return "almost full";
    case BATTERY_FULL:       return "full";
    case POWERLINE:          return "powerline";
    }
}

void xc_parse_packet(const char* buffer, size_t size, xc_recv_fn recv, xc_ack_fn ack, void* user_data)
{
    struct xc_ci_message* msg = (struct xc_ci_message*) buffer;

    if (size < 2 ||
        size < msg->message_size)
	return;

    switch (msg->action)
    {
    case MCI_PT_RX:
	recv(user_data,
	     (enum mci_rx_event) msg->packet_rx.rx_event,
	     msg->packet_rx.datapoint,
	     (enum mci_rx_datatype) msg->packet_rx.rx_data_type,
	     msg->packet_rx.value,
	     msg->packet_rx.signal,
	     (enum mci_battery_status) msg->packet_rx.battery);

	break;
    
    case MCI_PT_ACK:
    {
        int i;
	int message_id = (unsigned char) buffer[4];

	printf("received MCI_PT_ACK(%d) [", msg->message_size);
	
        // The ACK parsing is mostly based on guesses

        switch (buffer[2])
        {
        case 0x1c:
            printf("success ");
            break;

        default:
            printf("fail ");

            switch (buffer[3])
            {
            case 6:
                printf("no response ");
                break;

            case 5:
		// Possibly overflowing buffers
		printf("unknown ");
		message_id = -1;
		break;

            case 1:
                printf("unknown command ");
	        message_id = (unsigned char) buffer[5];
                break;

            case 0:
                printf("unknown dp ");
	        message_id = (unsigned char) buffer[5];
                break;
            }
            break;
        }

	for (i = 2; i < msg->message_size; ++i)
	    printf("%02hhx ", buffer[i]);
	
	printf("]\n");
	
	ack(user_data, buffer[2] == 0x1c, message_id);

	break;
    }

    case MCI_PT_FW:
	printf("software version: %d.%02d\n", buffer[11], buffer[12]);
	break;

    default:
	printf("unprocessed: received %02x: %d\n", msg->action, msg->message_size);
	break;
    }
}

void xc_make_setpercent_msg(char* buffer, int datapoint, int value, int message_id)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x9;
    message->action = MCI_PT_TX;
    message->packet_tx.datapoint = datapoint;
    message->packet_tx.tx_event = DIM_STOP_OR_SET;
    message->packet_tx.value = (value << 8) + 0x40;
    message->packet_tx.message_id = message_id;
}

void xc_make_switch_msg(char* buffer, int datapoint, int on, int message_id)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x9;
    message->action = MCI_PT_TX;
    message->packet_tx.datapoint = datapoint;
    message->packet_tx.tx_event = SET_BOOLEAN;
    message->packet_tx.value = on;
    message->packet_tx.message_id = message_id;
}

void xc_make_getswversion(char* buffer)
{
    struct xc_ci_message* message = (struct xc_ci_message*) buffer;

    message->message_size = 0x9;
    message->action = MCI_PT_FW;

    buffer[2] = 0x10;
    buffer[3] = 0x08;
    buffer[4] = 0x0;
    buffer[5] = 0x0;
    buffer[6] = 0x00;
    buffer[7] = 0x0a;
    buffer[8] = 0x0;
}

