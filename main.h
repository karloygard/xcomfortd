/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _MQTT_GATEWAY_H_
#define _MQTT_GATEWAY_H_

#include "usb.h"
#include <map>
#include <string>

enum mqtt_topics
{
    MQTT_TOPIC_SWITCH,
    MQTT_TOPIC_DIMMER,
    MQTT_TOPIC_SHUTTER,
    MQTT_TOPIC_REQUEST_STATUS,
    MQTT_DEBUG
};

std::map<std::string, mqtt_topics> mqtt_topic_type = {
    { "switch", MQTT_TOPIC_SWITCH },
    { "dimmer", MQTT_TOPIC_DIMMER },
    { "shutter", MQTT_TOPIC_SHUTTER },
    { "requeststatus", MQTT_TOPIC_REQUEST_STATUS },
    { "debug", MQTT_DEBUG }
};

std::map<std::string, mci_sb_command> shutter_cmd_type = {
    { "down", SHUTTER_CMD_DOWN },
    { "up", SHUTTER_CMD_UP },
    { "stop", SHUTTER_CMD_STOP }
};

struct datapoint_change
{
    // Linked list
    datapoint_change* next;

    // Datapoint this relates to
    int datapoint;

    int new_value;
    int sent_value;

    // number of status requests sent
    int sent_status_requests;

    mci_tx_event event;

    // non zero when we're waiting for an ack
    long timeout;

    // The id we're waiting for an ack for
    int active_message_id;
};

class MQTTGateway
    : public USB
{
public:

    MQTTGateway(bool verbose, bool use_syslog);

    virtual bool Init(int epoll_fd,
		      const char* server,
		      int port,
		      const char* username,
		      const char* password);
    virtual void Stop();

    long Prepoll(int epoll_fd);
    virtual void Poll(const epoll_event& event);

    void SendDPValue(int datapoint, int value, mci_tx_event event);

protected:

    virtual void Error(const char* fmt, ...);
    virtual void Info(const char* fmt, ...);

private:

    void TrySendMore();

    static void mqtt_connected(mosquitto* mosq,
			       void* obj,
			       int rc);

    static void mqtt_disconnected(mosquitto* mosq,
				  void* obj,
				  int rc);

    static void mqtt_message(mosquitto* mosq,
			     void* obj,
			     const struct mosquitto_message* message);

    void MQTTConnected(int rc);
    void MQTTDisconnected(int rc);
    void MQTTMessage(const struct mosquitto_message* message);

    virtual void Relno(int rf_major,
		       int rf_minor,
		       int usb_major,
		       int usb_minor);

    virtual void MessageReceived(mci_rx_event event,
				 int datapoint,
				 mci_rx_datatype data_type,
				 int value,
				 int signal,
				 mci_battery_status battery);

    virtual void AckReceived(int success, int message_id);

    bool RegisterSocket();

    // Mosquitto instance

    mosquitto* mosq;

    /* Linked list that keeps track of requested datapoint changes.
       This buffers requests, in order to prevent overloading the
       stick. */

    datapoint_change* change_buffer;

    // Next message id (0 to 255 looping)

    int next_message_id;

    // Messages in transit

    int messages_in_transit;

    // Verbose logging

    bool verbose;

    // Log to syslog

    bool use_syslog;

    // Time to reconnect, only set when we have been disconnected

    long reconnect_time;
};

#endif
