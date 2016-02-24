/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _MQTT_GATEWAY_H_
#define _MQTT_GATEWAY_H_

#include "usb.h"

class MQTTGateway
    : public USB
{
public:

    MQTTGateway();

    virtual int Init(int epoll_fd);
    virtual void Stop();

    void Prepoll(int epoll_fd);
    virtual void Poll(const epoll_event& event);

    void set_boolean(int datapoint, int value);
    void set_value(int datapoint, int value);

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
};

#endif
