/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _MQTT_GATEWAY_H_
#define _MQTT_GATEWAY_H_

#include "usb.h"

struct datapoint_change
{
    datapoint_change* next;
    int datapoint;
    int value;
    int boolean;
    long buffer_until;
};

class MQTTGateway
    : public USB
{
public:

    MQTTGateway();

    virtual int Init(int epoll_fd, const char* server);
    virtual void Stop();

    long Prepoll(int epoll_fd);
    virtual void Poll(const epoll_event& event);

    void set_value(int datapoint, int value, bool boolean);

protected:

    void TrySendMore();

private:

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

    mosquitto* mosq;

    datapoint_change* change_buffer;
};

#endif
