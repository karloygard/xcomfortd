/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _MQTT_GATEWAY_H_
#define _MQTT_GATEWAY_H_

#include "usb.h"

int64_t getmseconds();

class MQTTGateway
    : public USB
{
public:

    MQTTGateway(bool verbose);

    virtual bool Init(int epoll_fd,
		      const char* server,
		      int port,
		      const char* username,
		      const char* password);
    virtual void Stop();

    virtual int Prepoll(int epoll_fd);
    virtual void Poll(const epoll_event& event);

protected:

    // Verbose logging

    bool verbose;

    // Mosquitto instance

    mosquitto* mosq;

private:

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
    virtual void MQTTMessage(const struct mosquitto_message* message) = 0;

    bool RegisterSocket();

    // Time to reconnect, only set when we have been disconnected

    int64_t reconnect_time;
};

#endif
