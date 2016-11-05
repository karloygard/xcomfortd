/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _XC_TO_MQTT_GATEWAY_H_
#define _XC_TO_MQTT_GATEWAY_H_

#include "mqtt.h"
#include <map>
#include <string>

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

class XCtoMQTT
    : public MQTTGateway
{
public:

    XCtoMQTT(bool verbose, bool use_syslog);

    long Prepoll(int epoll_fd);

    void SendDPValue(int datapoint, int value, mci_tx_event event);

protected:

    virtual void Error(const char* fmt, ...);
    virtual void Info(const char* fmt, ...);

private:

    void TrySendMore();

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

    /* Linked list that keeps track of requested datapoint changes.
       This buffers requests, in order to prevent overloading the
       stick. */

    datapoint_change* change_buffer;

    // Next message id (0 to 255 looping)

    int next_message_id;

    // Messages in transit

    int messages_in_transit;

    // Log to syslog

    bool use_syslog;
};

#endif
