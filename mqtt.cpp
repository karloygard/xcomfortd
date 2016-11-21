/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <mosquitto.h>
#include <poll.h>
#include <sys/epoll.h>

#include "mqtt.h"

int64_t getmseconds()
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
    return (int64_t(tp.tv_sec) * 1000) + (tp.tv_nsec / 1000000);
}

MQTTGateway::MQTTGateway(bool verbose)
    : verbose(verbose),
      mosq(NULL),
      reconnect_time(INT64_MAX)
{
}

void
MQTTGateway::mqtt_connected(mosquitto* mosq, void* obj, int rc)
{
    MQTTGateway* this_object = (MQTTGateway*) obj;

    this_object->MQTTConnected(rc);
}

void
MQTTGateway::MQTTConnected(int rc)
{
    if (verbose)
	Info("MQTT Connected, %s\n", mosquitto_connack_string(rc));

    mosquitto_subscribe(mosq, NULL, "xcomfort/+/set/+", 0);
}

void
MQTTGateway::mqtt_disconnected(mosquitto* mosq, void* obj, int rc)
{
    MQTTGateway* this_object = (MQTTGateway*) obj;

    this_object->MQTTDisconnected(rc);
}

void
MQTTGateway::MQTTDisconnected(int rc)
{
    if (verbose)
	Info("MQTT Disconnected, %s\n", mosquitto_strerror(rc));

    // Attempt to reconnect in 15 seconds

    reconnect_time = getmseconds() + 15000;
}

void
MQTTGateway::mqtt_message(mosquitto* mosq, void* obj, const struct mosquitto_message* message)
{
    MQTTGateway* this_object = (MQTTGateway*) obj;

    this_object->MQTTMessage(message);
}

bool
MQTTGateway::Init(int epoll_fd, const char* server, int port, const char* username, const char* password)
{
    char clientid[24];
    int err = 0;

    mosquitto_lib_init();

    memset(clientid, 0, 24);
    snprintf(clientid, 23, "xcomfort");
    mosq = mosquitto_new(clientid, 0, this);

    if (!mosq)
	return false;

    mosquitto_message_callback_set(mosq, mqtt_message);
    mosquitto_disconnect_callback_set(mosq, mqtt_disconnected);
    mosquitto_connect_callback_set(mosq, mqtt_connected);

    if (username && password)
	mosquitto_username_pw_set(mosq, username, password);

    err = mosquitto_connect(mosq, server, port, 30);

    if (err)
    {
	Error("failed to connect to MQTT server: %s\n", mosquitto_strerror(err));
	return false;
    }

    if (!USB::Init(epoll_fd))
	return false;

    return RegisterSocket();
}

bool
MQTTGateway::RegisterSocket()
{
    epoll_event mosquitto_event;

    mosquitto_event.events = EPOLLIN;
    mosquitto_event.data.ptr = this;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mosquitto_socket(mosq), &mosquitto_event) < 0)
    {
	Error("epoll_ctl failed %s\n", strerror(errno));
        return false;
    }

    return true;
}

void
MQTTGateway::Stop()
{
    USB::Stop();

    if (mosq)
    {
	mosquitto_disconnect(mosq);
	mosquitto_destroy(mosq);

        mosq = NULL;
    }

    mosquitto_lib_cleanup();
}

int
MQTTGateway::Prepoll(int epoll_fd)
{
    epoll_event mosquitto_event;

    mosquitto_event.data.ptr = this;

    if (reconnect_time < getmseconds())
    {
	int rc = mosquitto_reconnect(mosq);

	if (rc)
	{
	    Info("MQTT, Reconnecting failed, %s\n", mosquitto_strerror(rc));

	    // Attempt to reconnect in 15 seconds

	    reconnect_time = getmseconds() + 15000;
	}
	else
	{
	    RegisterSocket();
	    reconnect_time = INT64_MAX;
	}
    }

    if (mosquitto_socket(mosq) != -1)
    {
	// Mosquitto isn't making this easy

	if (mosquitto_want_write(mosq))
	{
	    mosquitto_event.events = EPOLLIN|EPOLLOUT;
	    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, mosquitto_socket(mosq), &mosquitto_event);
	}
	else
	{
	    mosquitto_event.events = EPOLLIN;
	    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, mosquitto_socket(mosq), &mosquitto_event);
	}
    }

    // Should be called "fairly frequently"

    mosquitto_loop_misc(mosq);

    // 500ms minimum timeout, for the above call

    return 500;
}

void
MQTTGateway::Poll(const epoll_event& event)
{
    if (event.data.ptr == this)
    {
	// This is for mosquitto

	if (event.events & POLLIN)
	    mosquitto_loop_read(mosq, 1);
	if (event.events & POLLOUT)
	    mosquitto_loop_write(mosq, 1);
    }
    else
	USB::Poll(event);
}

