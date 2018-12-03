/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _XC_TO_MQTT_GATEWAY_H_
#define _XC_TO_MQTT_GATEWAY_H_

#include "mqtt.h"

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

    // Event to be sent
    mci_tx_event event;

    // Non zero when we're waiting for an ack
    int64_t timeout;

    // The id we're waiting for an ack for
    int active_message_id;
};

class XCtoMQTT
    : public MQTTGateway
{
public:

    XCtoMQTT(bool verbose, bool use_syslog);

    int Prepoll(int epoll_fd);

    void SendDPValue(int datapoint, int value, mci_tx_event event);

protected:

    virtual void Error(const char* fmt, ...);
    virtual void Info(const char* fmt, ...);

private:

    void TrySendMore();

    void MQTTMessage(const struct mosquitto_message* message);

    virtual void Relno(int status,
		       unsigned int rf_major,
		       unsigned int rf_minor,
		       unsigned int usb_major,
		       unsigned int usb_minor);

    virtual void MessageReceived(mci_rx_event event,
				 int datapoint,
				 mci_rx_datatype data_type,
				 int value,
				 int signal,
				 mgw_rx_battery battery,
				 int seq_no);

    virtual void AckReceived(int success, int seq_no, int extra);

    /* Linked list that keeps track of requested datapoint changes.
       This buffers requests, in order to prevent overloading the
       stick. */

    datapoint_change* change_buffer;

    // Next sequence no (0 to 15 looping)

    int next_message_id;

    // Messages in transit

    int messages_in_transit;

    // Log to syslog

    bool use_syslog;
};

#endif
