/* -*- Mode: C++; c-file-style: "k&r" -*- */

/*
 *  Reverse engineered from a variety of sources, for the purpose of
 *  interoperability.
 *
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mosquitto.h>

#include <libusb-1.0/libusb.h>
#include <poll.h>

#define mqtt_host "192.168.1.1"
#define mqtt_port 1883

#define EP_IN			(4 | LIBUSB_ENDPOINT_IN)
#define EP_OUT			(5 | LIBUSB_ENDPOINT_OUT)
#define CTRL_IN			(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT		(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)
#define USB_RQ			0x04
#define INTR_LENGTH		62

#include "lib_crc.h"

struct mosquitto* mosq;

unsigned short
calculate_crc(char* buf, size_t len)
{
     unsigned short crc = 0;
     size_t i = 0;
     
     for (i = 0; i < len; i++)
	  crc = update_crc_kermit(crc, buf[i]);
     
     return crc;
}

enum message_action
{
     A = 0x02,          // 2 bytes
     REPORT = 0x03,
     B = 0x04,          // Init or something? 0x04 0x0b
     INIT = 0x0b,       // No parameters
     C = 0x11,
     DISCOVER = 0x17,
     STARTSTOP = 0x1a,  // First thing to send, just one byte payload 1 = start, 0 = stop
     SENDMSG = 0x1b,
     E = 0x22,
     F = 0x30,
     G = 0x31,
     H = 0x33,
     I = 0x34,
     J = 0x36,           // No parameters
     Q = 0x39,           
     P = 0x40,           // two bytes (2, 0 or 2, 1)
     SERIALNO = 0x42,    // No parameters
     SOMESTRING = 0x44,  // No parameters
     M = 0x47,           // 9 bytes long, one long + extra byte
     N = 0x48,           // 9 bytes long, one long + extra byte
     O = 0x49,
     MCI_PT_TX = 0xB1,
     MCI_PT_RX = 0xC1
};

enum message_action_type
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
     MSG_NONE            = 0x1000
};

struct Argh
{
     const char* name;
     message_action_type type;
};

Argh message_lookup[] = 
{
     { "MSG_ACK",             MSG_ACK},
     { "MSG_STAY_ONLINE",     MSG_STAY_ONLINE},
     { "MSG_ALLIVE",          MSG_ALLIVE},
     { "MSG_GET_OFFLINE",     MSG_GET_OFFLINE},
     { "MSG_GET_EEPROM",      MSG_GET_EEPROM},
     { "MSG_SET_EEPROM",      MSG_SET_EEPROM},
     { "MSG_GET_CRC",         MSG_GET_CRC},
     { "MSG_TIME",            MSG_TIME},
     { "MSG_DATE",            MSG_DATE},
     { "MSG_PAKET",           MSG_PAKET},
     { "MSG_KILL",            MSG_KILL},
     { "MSG_FACTORY",         MSG_FACTORY},
     { "MSG_ON",              MSG_ON},
     { "MSG_OFF",             MSG_OFF},
     { "MSG_SWITCH_ON",       MSG_SWITCH_ON},
     { "MSG_SWITCH_OFF",      MSG_SWITCH_OFF},
     { "MSG_UP_PRESSED",      MSG_UP_PRESSED},
     { "MSG_UP_RELEASED",     MSG_UP_RELEASED},
     { "MSG_DOWN_PRESSED",    MSG_DOWN_PRESSED},
     { "MSG_DOWN_RELEASED",   MSG_DOWN_RELEASED},
     { "MSG_PWM",             MSG_PWM},
     { "MSG_FORCED",          MSG_FORCED},
     { "MSG_SINGLE_ON",       MSG_SINGLE_ON},
     { "MSG_TOGGLE",          MSG_TOGGLE},
     { "MSG_VALUE",           MSG_VALUE},
     { "MSG_ZU_KALT",         MSG_ZU_KALT},
     { "MSG_ZU_WARM",         MSG_ZU_WARM},
     { "MSG_STATUS",          MSG_STATUS},
     { "MSG_STATUS_APPL",     MSG_STATUS_APPL},
     { "MSG_STATUS_REQ_APPL", MSG_STATUS_REQ_APPL},
     { NULL, MSG_NONE}
};

enum battery_state
{
     BATTERY_EMPTY       = 0x0,
     BATTERY_WEAK        = 0x1,
     BATTERY_AVERAGE     = 0x2,
     BATTERY_ALMOSTFULL  = 0x3,
     BATTERY_FULL        = 0x4,
     POWERLINE           = 0x3f,
};

enum data_type
{
     NO_TELEGRAM_DATA         = 0x00,
     PERCENT                  = 0x01,
     DATA_TYPE_UCHAR          = 0x02,
     SSHORT_1COMMA            = 0x03,
     ANSI_FLOAT               = 0x04,
     MEMORY                   = 0x06,
     MEMORY_REQ               = 0x07,
     ALLIVE_FILTER            = 0x0c,
     USHORT_NO_COMMA          = 0x0d,
     RC_DATA                  = 0x17,
     MEMORY32                 = 0x1a,
     MEMORY32_REQ             = 0x1b,
     DATA_TYPE_TIME           = 0x1e,
     DATA_TYPE_DATE           = 0x1f,
     DATA_TYPE_PACKET         = 0x20,
     DATA_TYPE_UNSIGNED_LONG  = 0x25,
     DATA_TYPE_ULONG_1COMMA   = 0x26,
     DATA_TYPE_ULONG_2COMMA   = 0x27,
     DATA_TYPE_ULONG_3COMMA   = 0x28,
     USHORT_1COMMA            = 0x29,
     USHORT_2COMMA            = 0x2a,
     USHORT_3COMMA            = 0x2b,
     DIMPLEX_DATA             = 0x2d,
     DATA_TYPE_SLONG_NOCOMMA  = 0x2e,
     DATA_TYPE_SLONG_1COMMA   = 0x2f,
     DATA_TYPE_SLONG_2COMMA   = 0x30,
     DATA_TYPE_SLONG_3COMMA   = 0x31,
     DATA_TYPE_SSHORT_NOCOMMA = 0x32,
     DATA_TYPE_SSHORT_2COMMA  = 0x33,
     DATA_TYPE_SSHORT_3COMMA  = 0x34,
     DATE                     = 0x37,
     ARRAY                    = 0xf0
};

const char*
lookup_message_type(message_action_type type)
{
     for (int i = 0; message_lookup[i].name; ++i)
	  if (message_lookup[i].type == type)
	       return message_lookup[i].name;
     
     return NULL;
}

const char*
Blah(int type)
{
     // May be off by one!

     switch (type)
     {
     case 0:
	  return "Taster";
     case 1:
	  return "Taster 2-fach";
     case 2:
	  return "Taster 4-fach";
     case 4:
     case 50:
	  return "Raumcontroller";
     case 15:
	  return "Schaltaktor";
     case 16:
     case 76:
	  return "Dimmaktor";
     case 17:
     case 26:
	  return "Jalousieaktor";
     case 18:
	  return "Bin 230";
     case 19:
	  return "Bin Batt";
     case 20:
	  return "Fernbedienung";
     case 21:
	  return "Home Manager";
     case 22:
	  return "Temperatursensor";
     case 23:
	  return "Analogeingang";
     case 24:
	  return "Analogaktor";
     case 25:
     case 66:
	  return "Room-Manager";
     case 27:
	  return "Komm.-Schnittstelle";
     case 28:
	  return "Bewegungsmelder";
     case 47:
	  return "Fernbedienung 2K";
     case 48:
	  return "Fernbedienung 12K";
     case 49:
	  return "FB Display";
	  return "Fernbedienung LCD";
     case 51:
	  return "Router";
     case 52:
	  return "Impulseingang";
     case 53:
	  return "Energiesensor";
	  return "EMS";
     case 54:
	  return "Heizungsaktor";
     case 55:
	  return "FB Alarmtaster";
     case 56:
	  return "BOSCOS Interface";
     case 57:
	  return "Testgerät";
     case 58:
	  return "Aktor-Sensor-Kombination";
     case 59:
	  return "Generic device";
     case 60:
	  return "RF_CC1110";
     case 61:
	  return "MEP-Device";
     case 62:
	  return "Serial Interface";
	  return "Smart Home Controller";
     case 64:
	  return "Heizkörperthermostat";
     case 65:
	  return "Multi-Heizaktor 6-fach";
     case 67:
	  return "Rosetta-Sensor";
     case 68:
	  return "Rosetta-Router";
     case 69:
	  return "Ethernet Communication Interface";
     case 70:
	  return "MCH_12x-Testgerät";
	  return "Multi-Heizaktor 12-fach";
     case 71:
	  return "Kommunikationsstick";
	  return "USB-Gateway";
     case 73:
	  return "Schaltaktor neu";
     case 75:
	  return "Fenster/Tür Sensor";
     default:
	  return NULL;
     }
}

#pragma pack(push,1)

struct message
{
     unsigned char      begin; // Always 5A
     unsigned char      size;
     unsigned char      action;
     union
     {
	  char             data[59];
	  struct {
	       unsigned char  type;
	       short int      magic;
	       unsigned char  sequence; // bitfield aaaa:ssss (a = 4 means something)
	       char           something2; // bitfield aaa:b:0:cc:d
	       char           batterystate; // bitfield 00:aaa:bbb
	       char           channel;
	       int            source;
	       short int      somethingelse;
	       int            destination;
	       char           signalstrength[2];
	       short int      time2;
	       short int      strength;
	       unsigned char  end; // Always A5
	  }                   report;
	  struct {
	       unsigned char  type;
	       unsigned char  magic; // 00
	       unsigned char  length; // 15
	       unsigned char  sequence; // 10-1f
	       unsigned char  bitfield; // aaaaaaa:b, b = wants ack
	       unsigned char  magic3; // 07
	       short int      magic1; // 00 80
	       int            source;
	       int            destination;
	       short int      magic2; // 01 00 or 0c 00
	       unsigned char  level;
	       short int      crc;
	       unsigned char  end; // Always A5
	  }                   sendmsg;
	  struct {
	       int            serialno;
	  }                   serialno;
	  struct {
	       unsigned char  stop;
	       unsigned char  end; // Always A5
	  }                   startstop;
	  struct {
	       int            blah;
	       unsigned char  index;
	       unsigned char  blah3; // A5 if short packet
	       unsigned char  blah4;
	       unsigned char  blah5; // Always A5
	  }                   m;
	  struct {
	       unsigned char  datapoint;
	       unsigned char  opcode;
	       unsigned char  value;
	       int            unused;
	  }                   datapoint_tx;
	  struct {
	       unsigned char  datapoint;
	       unsigned char  infoshort;
	       unsigned char  datatype;
	       int            data;
	       unsigned char  unknown;
	       unsigned char  signal;
	       unsigned char  battery;
	  }                   datapoint_rx;
	  struct {
	       short int      something;
               unsigned char  end;
	  }                   i;
	  struct {
	       unsigned char  end; // Always A5
	  }                   somestring;
     };
};

#pragma pack(pop)

static int next_state(void);

enum
{
     STATE_AWAIT_INIT = 1,
     STATE_READY,
     STATE_22,
     STATE_H,
     STATE_SERIALNO,
     STATE_47,
     STATE_44,
     STATE_SECOND_47,
     STATE_STARTSTOP,
     STATE_STARTSTOP2
};

struct xcomfort_device
{
     xcomfort_device*    next;
     int                 serial_number;

     bool                waiting_for_ack;

     int                 last_known_value;
     int                 desired_value;
};

xcomfort_device* device_list = NULL;
bool message_in_transit = false;
bool waiting_for_ack = false;

static int state = 0;
static struct libusb_device_handle* devh = NULL;

static unsigned char recvbuf[INTR_LENGTH];
static struct libusb_transfer* recv_transfer = NULL;

static unsigned char sendbuf[INTR_LENGTH];
static struct libusb_transfer* send_transfer = NULL;

static int do_exit = 0;

void
construct_sendmsg(struct message* question, int target, message_action_type type, unsigned char level)
{
     static int sequence = 0x10;
     
     question->begin = 0x5a;
     question->size = 0x19;
     question->action = SENDMSG;
     question->sendmsg.type = type;
     question->sendmsg.magic = 0x0;
     question->sendmsg.length = 0x15;
     question->sendmsg.sequence = sequence++;
     question->sendmsg.bitfield = 0x82; // wants ack
     question->sendmsg.magic3 = 0x07;
     question->sendmsg.magic1 = 0x8000;
     question->sendmsg.source = 0x0;
     question->sendmsg.destination = target;
     question->sendmsg.magic2 = 0x1;
     question->sendmsg.level = level;
     question->sendmsg.crc = calculate_crc((char*) &(question->sendmsg), sizeof(question->sendmsg) - 3);
     question->sendmsg.end = 0xa5;
     
     if (sequence == 0x20)
	  sequence = 0x10;

     for (int i = 0; i < 0x16; ++i)
       printf("%02hhx", question->data[i]);
     printf("\n");
}

void
send_next_message()
{
     xcomfort_device* i;

     for (i = device_list; i; i = i->next)
	  if (i->desired_value != i->last_known_value &&
	      !i->waiting_for_ack)
	  {
	       message_action_type type = MSG_OFF;

	       struct message* question = (struct message*) sendbuf;
	       int r;

	       if (i->desired_value == 255)
		    type = MSG_ON;
	       else
		    if (i->desired_value > 0)
			 type = MSG_FORCED;
	       
	       i->waiting_for_ack = true;
	       construct_sendmsg(question, i->serial_number, type, i->desired_value);
	       
	       r = libusb_submit_transfer(send_transfer);
	       if (r < 0)
		    fprintf(stderr, "failed to submit transfer\n");

	       assert(!message_in_transit);
	       message_in_transit = true;
	       waiting_for_ack = true;

	       return;
	  }
}

bool
ack_received(int serial_number)
{
     xcomfort_device* i;

     waiting_for_ack = false;

     for (i = device_list; i; i = i->next)
	  if (i->serial_number == serial_number)
	       if (i->waiting_for_ack)
	       {
		    printf("received expected ack for %d to change to %d\n", serial_number, i->desired_value);

		    i->waiting_for_ack = false;
		    i->last_known_value = i->desired_value;
		    
		    send_next_message();

		    return true;
	       }
	       else
		    break;

     return false;
}

void
set_value(int serial_number, int value, bool reported = false)
{
     // See if it's already known and inactive

     xcomfort_device* i;

     for (i = device_list; i; i = i->next)
	  if (i->serial_number == serial_number)
	  {
	       if (i->desired_value == value)
	       {
		    // This value is already desired, nothing to do

		    printf("device %d already setting to %d\n", serial_number, value);

		    return;
	       }
	       else
		    if (i->last_known_value == value)
			 if (!i->waiting_for_ack)
			 {
			      // Value is already active, nothing to do

			      printf("device %d already set to %d\n", serial_number, value);
			      
			      return;
			 }

	       if (reported)
	       {
		    i->last_known_value = value;
		    
		    printf("reported change %d to %d\n", serial_number, value);
	       }
	       else
		    printf("have to change %d to %d\n", serial_number, value);

	       i->desired_value = value;
	       i->waiting_for_ack = false;
	       
	       break;
	  }

     if (!i)
     {
	  // Previously unknown device

	  printf("creating %d with value %d\n", serial_number, value);

	  i  = (xcomfort_device*) malloc(sizeof(xcomfort_device));
	  
	  if (!i)
	       return;
	  
	  i->next = device_list;
	  i->serial_number = serial_number;
	  
	  if (reported)
	       i->last_known_value = value;
	  else
	       i->last_known_value = 0;

	  i->desired_value = value;
	  i->waiting_for_ack = false;
	  
	  device_list = i;
     }

     if (!reported &&
	 !message_in_transit &&
	 !waiting_for_ack)
	  send_next_message();
}

static int
next_state(void)
{
     int r = 0;
     
     switch (state)
     {
     case STATE_AWAIT_INIT:
	  state = STATE_AWAIT_INIT;
	  break;
     
     default:
	  printf("unrecognised state %d\n", state);
     }

     if (r < 0)
     {
	  fprintf(stderr, "error detected changing state\n");
	  return r;
     }
     
     return 0;
}

static void LIBUSB_CALL
cb_recv(struct libusb_transfer* transfer)
{
     struct message* answer = (struct message*) transfer->buffer;

     if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
     {
	  fprintf(stderr, "irq transfer status %d?\n", transfer->status);
	  do_exit = 2;
	  libusb_free_transfer(transfer);
	  recv_transfer = NULL;
	  return;
     }
     
     switch (answer->action)
     {
         case REPORT:
	 {
	      static message_action_type last_type = MSG_ACK;
	      
	      const char* name = lookup_message_type((message_action_type) answer->report.type);
	      
	      printf("received REPORT(%s, %d): %d 0x%08x %d (battery: %d) (strength: %d) (channel: %d) %04x %02x %02x\t[",
		     name,
		     answer->size,
		     answer->report.sequence >> 4,
		     answer->report.source,
		     answer->report.batterystate >> 3,
		     answer->report.batterystate & 0x7,
		     *((short int*) (answer->data + answer->size - 6)),
		     answer->report.channel,
		     answer->report.magic,
		     answer->report.sequence,
		     answer->report.something2);
	      
	      for (int i = 0; i < answer->size - 21; ++i)
		   printf("%02hhx ", answer->report.signalstrength[i]);
	      
	      printf("]\n");
	      
	      if (answer->report.type == MSG_ACK)
	      {
		   const char* state = NULL;
		   int value = 0;

		   ack_received(answer->report.source);
		   
		   switch (last_type)
		   {
		   case MSG_ON:
			state = "true";
			value = 255;
			break;
			
		   case MSG_OFF:
			state = "false";
			value = 0;
			break;
			
		   default:
			break;
		   }
		   
		   if (state)
		   {
			char topic[128];
			
			set_value(answer->report.source, value, true);
     
			sprintf(topic, "%08x", answer->report.source);
			mosquitto_publish(mosq, NULL, topic, strlen(state), (const uint8_t*) state, 1, true);
		   }
	      }
	      
	      last_type = (message_action_type) answer->report.type;
	      
	      break;
	 }
	 
     case INIT:
         {
	      struct message* question = (struct message*) sendbuf;
	      int r;
	      
	      // Should be 5f 2a 5f
	      
	      printf("received 0x0b INIT(%d): %02x, %02x, %02x, %02x, %02x, %02x\n",
		     answer->size, answer->data[0], answer->data[1], answer->data[2],
		     answer->data[3], answer->data[4], answer->data[5]);
	      
	      question->begin = 0x5a;
	      question->size = 0x05;
	      question->action = STARTSTOP;
	      question->startstop.stop = 0;
	      question->startstop.end = 0xa5;
	      
	      r = libusb_submit_transfer(send_transfer);
	      if (r < 0)
		   fprintf(stderr, "failed to submit transfer\n");
	      
	      state = STATE_STARTSTOP;
	 }
	 break;
	  
     case E:
         {
	      struct message* question = (struct message*) sendbuf;
	      int r;
	      
	      printf("received 0x22 E(%d)\n", answer->size);
	      
	      question->begin = 0x5a;
	      question->size = 0x05;
	      question->action = H;
	      question->startstop.stop = 1;
	      question->startstop.end = 0xa5;
	      
	      r = libusb_submit_transfer(send_transfer);
	      if (r < 0)
		   fprintf(stderr, "failed to submit transfer\n");
	      
	      state = STATE_H;
	 }
	 break;
     
     case SERIALNO:
         {
	      struct message* question = (struct message*) sendbuf;
	      int r;
	      
	      printf("received 0x42 SERIALNO: %d\n", answer->serialno.serialno);
	      
	      question->begin = 0x5a;
	      question->size = 0x04;
	      question->action = E;
	      question->somestring.end = 0xa5;
	      
	      r = libusb_submit_transfer(send_transfer);
	      if (r < 0)
		   fprintf(stderr, "failed to submit transfer\n");
	      
	      state = STATE_22;
	 }
	 break;

  case 0x2:
       printf("received 0x02(%d): %08x\n", answer->size, answer->serialno.serialno);
       {
	    struct message* question = (struct message*) sendbuf;
	    int r;
	    
	    printf("broadcasting MSG_ALLIVE\n");
	    
	    construct_sendmsg(question, 0, MSG_ALLIVE, 0x3);
	    
	    r = libusb_submit_transfer(send_transfer);
	    if (r < 0)
		 fprintf(stderr, "failed to submit transfer\n");
	    
	    state = STATE_READY;
       }
       break;
       
     case SOMESTRING:
         {
	      char name[128];
	      struct message* question = (struct message*) sendbuf;
	      int r;
	      
	      strcpy(name, answer->data);
	      printf("received 0x44 SOMESTRING(%d): %s\n", answer->size, name);
	      
	      question->begin = 0x5a;
	      question->size = 0x04;
	      question->action = SERIALNO;
	      question->somestring.end = 0xa5;
	      
	      r = libusb_submit_transfer(send_transfer);
	      if (r < 0)
		   fprintf(stderr, "failed to submit transfer\n");
	      
	      state = STATE_SERIALNO;
	 }
	 break;
	 
     case 0x47:
	  printf("received 0x47(%d): %08x %02x %02x %02x %02x\n", answer->size, answer->m.blah, answer->m.index, answer->m.blah3, answer->m.blah4, answer->m.blah5);
	  
	  if (state != STATE_SECOND_47)
	  {
	       struct message* question = (struct message*) sendbuf;
	       int r;
	       
	       question->begin = 0x5a;
	       question->size = 0x09;
	       question->action = M;
	       question->m.blah = 0x50;
	       question->m.index = 2;
	       question->m.blah3 = 0xa5;
	       
	       r = libusb_submit_transfer(send_transfer);
	       if (r < 0)
		    fprintf(stderr, "failed to submit transfer\n");
	       
	       state = STATE_SECOND_47;
	  }
	  else
	  {
	       struct message* question = (struct message*) sendbuf;
	       int r;
	       
	       question->begin = 0x5a;
	       question->size = 0x04;
	       question->action = SOMESTRING;
	       question->somestring.end = 0xa5;
	       
	       r = libusb_submit_transfer(send_transfer);
	       if (r < 0)
		    fprintf(stderr, "failed to submit transfer\n");
	       
	       state = STATE_44;
	  }
	  break;
	  
     default:
	  printf("unprocessed: received %02x: %d\n", answer->action, answer->size);
	  break;
  }
     
     if (libusb_submit_transfer(recv_transfer) < 0)
	  do_exit = 2;
}

static void LIBUSB_CALL
cb_send(struct libusb_transfer* transfer)
{
     message_in_transit = false;

     if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
     {
	  fprintf(stderr, "irq transfer status %d?\n", transfer->status);
	  do_exit = 2;
	  libusb_free_transfer(transfer);
	  send_transfer = NULL;
	  return;
     }
     
     switch (state)
     {
     case STATE_STARTSTOP2:
         {
	      //struct message* question = (struct message*) sendbuf;
	      int r;
	      
	      printf("----- sending i\n");
	      
	      /*question->begin = 0x5a;
		question->size = 0x06;
		question->action = I;
		question->i.something = 0xffff;
		question->i.end = 0xa5;
		
		r = libusb_submit_transfer(send_transfer);
		if (r < 0)
		fprintf(stderr, "failed to submit transfer\n");*/
	      
	      state = STATE_READY;
	 }
	 break;
	 
     case STATE_H:
         {
	      struct message* question = (struct message*) sendbuf;
	      int r;
	      
	      printf("----- sending startstop 2\n");
	      
	      question->begin = 0x5a;
	      question->size = 0x05;
	      question->action = STARTSTOP;
	      question->startstop.stop = 1;
	      question->startstop.end = 0xa5;
	      
	      r = libusb_submit_transfer(send_transfer);
	      if (r < 0)
		   fprintf(stderr, "failed to submit transfer\n");
	      
	      state = STATE_STARTSTOP2;
	 }
	 break;
	 
	 
     case STATE_STARTSTOP:
          {
	       struct message* question = (struct message*) sendbuf;
	       int r;
	      
	       printf("----- sending first 0x47\n");

	       question->begin = 0x5a;
	       question->size = 0x09;
	       question->action = M;
	       question->m.blah = 0x8;
	       question->m.index = 6;
	       question->m.blah3 = 0xa5;
	       
	       r = libusb_submit_transfer(send_transfer);
	       if (r < 0)
		    fprintf(stderr, "failed to submit transfer\n");
	       
	       state = STATE_47;
;
	  }
	  break;
     }
}

static int
init_communication(void)
{
     int r;

     r = libusb_submit_transfer(recv_transfer);
     if (r < 0)
	  return r;
     
     r = libusb_submit_transfer(send_transfer);
     if (r < 0)
	  return r;
     
     /* start state machine */
     state = STATE_AWAIT_INIT;
     return next_state();
}

static int
alloc_transfers(void)
{
     recv_transfer = libusb_alloc_transfer(0);
     if (!recv_transfer)
	  return -ENOMEM;
     
     libusb_fill_interrupt_transfer(recv_transfer, devh, EP_IN, recvbuf,
				    sizeof(recvbuf), cb_recv, NULL, 0);
     
     struct message* question = (struct message*) sendbuf;
     
     question->begin = 0x5a;
     question->size = 0x04;
     question->action = INIT;
     question->m.blah = 0xa5;

     printf("init\n");
     
     send_transfer = libusb_alloc_transfer(0);
     if (!send_transfer)
	  return -ENOMEM;
     
     libusb_fill_interrupt_transfer(send_transfer, devh, EP_OUT, sendbuf,
				    sizeof(sendbuf), cb_send, NULL, 0);
     
     return 0;
}

static void
sighandler(int signum)
{
     do_exit = 1;
}

void
connect_callback(mosquitto* mosq, void* obj, int result)
{
     printf("connected to MQTT server, rc=%d\n", result);
}

void
message_callback(mosquitto* mosq, void* obj, const struct mosquitto_message* message)
{
     bool match = 0;
     int value = 0;

     if (state != STATE_READY)
	  return;
     
     int target = strtol(message->topic, NULL, 16);
     
     if (errno == EINVAL ||
	 errno == ERANGE)
	  return;
     
     if (strcmp((char*) message->payload, "true") == 0)
	  value = 255;
     else
	  if (strcmp((char*) message->payload, "false") == 0)
	       value = 0;
	  else
	       return;
     
     printf("got message '%s' for topic '%s'\n", (char*) message->payload, message->topic);

     set_value(target, value);
     
     /*mosquitto_topic_matches_sub("/devices/wb-adc/controls/+", message->topic, &match);
       if (match) {
       printf("got message for ADC topic\n");
       }*/
}

int fds = 0;
pollfd fdset[10];

void usb_fd_added_cb(int fd, short events, void * source)
{
     pollfd* mod = fdset;

     for (int i = 0; i < fds; ++i)
     {
	  if (mod->fd == fd) 
	       break;
	  
	  mod++;
     }
     
     if (mod->fd == 0)
	  fds++;
     
     mod->fd = fd;
     mod->events = events;
     mod->revents = 0;
}

void usb_fd_removed_cb(int fd, void* source)
{
     pollfd* mod = fdset;
     
     for (int i = 0; i < fds; ++i)
     {
	  if (mod->fd == fd) 
	       break;
	  
	  mod++;
     }
     
     printf("removing %d\n", fd);
     mod->events = 0;
     mod->revents = 0;
}

int usb_init_fds(libusb_context *context)
{
     const struct libusb_pollfd ** usb_fds = libusb_get_pollfds(context);
     
     if (!usb_fds)
	  return -1;
     
     for (int numfds = 0; usb_fds[numfds] != NULL; ++numfds)
     {
	  usb_fd_added_cb(usb_fds[numfds]->fd, usb_fds[numfds]->events, NULL);
     }
     
     free(usb_fds);
     
     libusb_set_pollfd_notifiers(context, usb_fd_added_cb, usb_fd_removed_cb, NULL);
     return 0;
}

int main(void)
{
     char clientid[24];
     int rc = 0;
     
     libusb_context *context;
     
     mosquitto_lib_init();
     
     memset(clientid, 0, 24);
     snprintf(clientid, 23, "xcomfort_%d", getpid());
     mosq = mosquitto_new(clientid, 0, NULL);
     
     if (mosq)
     {
	  mosquitto_connect_callback_set(mosq, connect_callback);
	  mosquitto_message_callback_set(mosq, message_callback);
	  
	  rc = mosquitto_connect(mosq, mqtt_host, mqtt_port, 60);
	  
	  if (rc)
	       printf("error connecting to MQTT server: %d\n", rc);
	  else
	  {
	       int err = mosquitto_subscribe(mosq, NULL, "#", 1);
	  }
     }

     fds++;

     fdset[0].fd = mosquitto_socket(mosq);
     fdset[0].events = POLLIN;
     fdset[0].revents = 0;
     
     struct sigaction sigact;
     int r = 1;
     
     r = libusb_init(&context);
     if (r < 0)
     {
	  fprintf(stderr, "failed to initialise libusb\n");
	  exit(1);
     }
     
     devh = libusb_open_device_with_vid_pid(NULL, 0x188a, 0x1102);
     if (!devh)
     {
	  fprintf(stderr, "Could not find/open device\n");
	  goto out;
     }
     
     if (libusb_kernel_driver_active(devh, 0) == 1)
     {
	  r = libusb_detach_kernel_driver(devh, 0);
	  if (r < 0)
	  {
	       fprintf(stderr, "usb_detach_kernel_driver %d\n", r);
	       goto out;
	  }

	  printf("detached kernel from device\n");
     }
     
     r = libusb_set_configuration(devh, 1); 
     if (r < 0)
     { 
	  fprintf(stderr, "libusb_set_configuration error %d\n", r); 
	  goto out; 
     } 

     r = libusb_claim_interface(devh, 0);
     if (r < 0)
     {
	  fprintf(stderr, "usb_claim_interface error %d\n", r);
	  goto out;
     }
     printf("claimed interface\n");
     
     /* async from here onwards */
     
     r = alloc_transfers();
     if (r < 0)
	  goto out_deinit;
     
     r = init_communication();
     if (r < 0)
	  goto out_deinit;
     
     sigact.sa_handler = sighandler;
     sigemptyset(&sigact.sa_mask);
     sigact.sa_flags = 0;
     sigaction(SIGINT, &sigact, NULL);
     sigaction(SIGTERM, &sigact, NULL);
     sigaction(SIGQUIT, &sigact, NULL);
     
     usb_init_fds(context);
     
     while (!do_exit)
     {
	  struct timeval tv = { 0, 0 };
	  
	  if (mosquitto_want_write(mosq))
	       fdset[0].events = POLLIN | POLLOUT;
	  else
	       fdset[0].events = POLLIN;
	  
	  if (poll(fdset, fds, 500) < 0)
	       break;
	  
	  if (fdset[0].revents & POLLIN)
	       mosquitto_loop_read(mosq, 1);
	  if (fdset[0].revents & POLLOUT)
	       mosquitto_loop_write(mosq, 1);
	  
	  fdset[0].revents = 0;
	  mosquitto_loop_misc(mosq);
	  
	  libusb_handle_events_timeout(context, &tv);
     }
     
     printf("shutting down...\n");
     
     if (recv_transfer)
     {
	  r = libusb_cancel_transfer(recv_transfer);
	  if (r < 0)
	       goto out_deinit;
     }
     
     while (recv_transfer)
	  if (libusb_handle_events(NULL) < 0)
	       break;
     
     if (send_transfer)
     {
	  r = libusb_cancel_transfer(send_transfer);
	  if (r < 0)
	       goto out_deinit;
     }
     
     while (send_transfer)
	  if (libusb_handle_events(NULL) < 0)
	       break;
     
     if (do_exit == 1)
	  r = 0;
     else
	  r = 1;
     
out_deinit:

     if (mosq)
	  mosquitto_destroy(mosq);
     
     mosquitto_lib_cleanup();
     
     libusb_free_transfer(recv_transfer);
     libusb_free_transfer(send_transfer);

     libusb_release_interface(devh, 0);
out:
     libusb_close(devh);
     libusb_exit(NULL);

     return r >= 0 ? r : -r;
}

