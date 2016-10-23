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
    MQTT_TOPIC_SWITCH, MQTT_TOPIC_DIMMER, MQTT_TOPIC_STATUS, MQTT_DEBUG
};

std::map<std::string, mqtt_topics> mqtt_topic_type = { { "switch",
        MQTT_TOPIC_SWITCH }, { "dimmer", MQTT_TOPIC_DIMMER }, { "status",
        MQTT_TOPIC_STATUS }, { "debug", MQTT_DEBUG } };

struct datapoint_change
{
    datapoint_change* next;

    int datapoint;

    int new_value;
    int sent_value;

    int boolean;

    // non zero when we're waiting for an ack
    long timeout;

    // The id we're waiting for an ack for
    int active_message_id;
};

class MQTTGateway
    : public USB
{
public:

    MQTTGateway(bool debug);

    virtual int Init(int epoll_fd, const char* server);
    virtual void Stop();

    long Prepoll(int epoll_fd);
    virtual void Poll(const epoll_event& event);

    void SetDPValue(int datapoint, int value, bool boolean);

private:

    void TrySendMore();

    static void mqtt_connect(mosquitto* mosq,
    			 void* obj,
				 int rc);

    static void mqtt_message(mosquitto* mosq,
			     void* obj,
			     const struct mosquitto_message* message);

    void MQTTMessage(const struct mosquitto_message* message);

    virtual void MessageReceived(mci_rx_event event,
				 int datapoint,
				 mci_rx_datatype data_type,
				 int value,
				 int signal,
				 mci_battery_status battery);

    virtual void AckReceived(int success, int message_id);

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

    // Debug logging

    bool debug;
};

#endif
