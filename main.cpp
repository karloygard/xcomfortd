/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
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
#include <sys/epoll.h>

#include "main.h"

int do_exit = 0;

static void
sighandler(int signum)
{
    do_exit = 1;
}

long getmseconds()
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
    return long(tp.tv_sec * 1000) + (tp.tv_nsec / 1000000);
}

MQTTGateway::MQTTGateway(bool debug)
    : mosq(NULL),
      change_buffer(NULL),
      next_message_id(0),
      messages_in_transit(0),
      debug(debug)
{
}

void
MQTTGateway::MessageReceived(mci_rx_event event,
			     int datapoint,
			     mci_rx_datatype data_type,
			     int value,
			     int signal,
			     mci_battery_status battery)
{
    const char* msg_name = xc_rxevent_name(event);
    const char* batt_name = xc_battery_status_name(battery);
    
    printf("received MCI_PT_RX(%s): datapoint: %d value_type: %d value: %d (signal: %d) (battery: %s)\n",
	   msg_name,
	   datapoint,
	   data_type,
	   value,
	   signal,
	   batt_name);

    switch (event)
    {
    case MSG_STATUS:
        {
            // Received message that datapoint value changed

	    char topic[128];
	    char state[128];
	    
	    snprintf(topic, 128, "%d/get/dimmer", datapoint);
	    snprintf(state, 128, "%d", value);
	    
	    if (mosquitto_publish(mosq, NULL, topic, strlen(state), (const uint8_t*) state, 1, true))
		fprintf(stderr, "failed to publish message\n");
	    
	    snprintf(topic, 128, "%d/get/switch", datapoint);
	    
	    if (mosquitto_publish(mosq, NULL, topic, value ? 4 : 5, value ? "true" : "false", 1, true))
		fprintf(stderr, "failed to publish message\n");
	}
	break;

    default:
	break;
    }
}

void
MQTTGateway::AckReceived(int success, int message_id)
{
    if (--messages_in_transit < 0)
	/* Messages can be acked after we have given up waiting for
           them. */

	messages_in_transit = 0;

    if (message_id >= 0)
    {
	for (datapoint_change* dp = change_buffer; dp; dp = dp->next)
	    if (dp->active_message_id == message_id)
	    {
		// We got an ack for this message; clear to send updates, if any

		if (debug)
		    fprintf(stderr, "%d acked after %d ms\n", message_id, int(getmseconds() - (dp->timeout - 5500)));

		dp->active_message_id = -1;
		dp->timeout = 0;

		return;
	    }

	fprintf(stderr, "received spurious ack; message timeout is possibly too low\n");
    }
}

void
MQTTGateway::SetDPValue(int datapoint, int value, bool boolean)
{
    datapoint_change* dp = change_buffer;

    for (; dp; dp = dp->next)
	if (dp->datapoint == datapoint)
	    break;

    if (dp)
    {
	// This is already a known datapoint; update values

	dp->new_value = value;
	dp->boolean = boolean;
    }
    else
    {
	dp = new datapoint_change;

	if (!dp)
	    return;

	dp->next = change_buffer;
	dp->datapoint = datapoint;
	dp->new_value = value;
	dp->sent_value = -1;
	dp->boolean = boolean;

	dp->active_message_id = -1;
	dp->timeout = 0;

	change_buffer = dp;
    }
}

void
MQTTGateway::TrySendMore()
{
    if (messages_in_transit >= 1)
	/* Number of messages we can run in parallel.
	   
           The stick appears to run into issues when handling multiple
	   requests in parallel; it starts silently dropping messages
	   or throwing unknown errors.  If you are adventurous, you
	   can try bumping this for higher throughput when changing
	   multiple datapoints; I saw issues with 4+ parallel
	   requests. */
	
	return;

    if (CanSend())
    {
	long current_time = getmseconds();
	datapoint_change* dp = change_buffer;
	datapoint_change* prev = NULL;

	while (dp)
	{
	    if (dp->timeout <= current_time)
	    {
		// Time to inspect

		if (dp->active_message_id != -1 || dp->new_value != -1)
		{
		    // Unacked or unsent

		    char buffer[9];
		    bool retry = dp->active_message_id != -1;
		    int value;

		    if (retry)
		    {
			// Was value updated in the meanwhile?
			
			if (dp->new_value != -1)
			    value = dp->new_value;
			else
			    value = dp->sent_value;

			if (debug)
			    fprintf(stderr, "message %d was lost; retrying setting %d to %d (new id %d)\n", dp->active_message_id, dp->datapoint, value, next_message_id);
		    }
		    else
		    {
			value = dp->new_value;

			if (debug)
			    fprintf(stderr, "setting %d to %d (id %d)\n", dp->datapoint, value, next_message_id);
		    }

		    dp->active_message_id = next_message_id;

		    dp->sent_value = value;
		    dp->new_value = -1;

		    // This is how long we'll wait until we consider the message to be lost
		    dp->timeout = current_time + 5500;

		    if (dp->boolean)
			xc_make_switch_msg(buffer, dp->datapoint, value != 0, next_message_id);
		    else
			xc_make_setpercent_msg(buffer, dp->datapoint, value, next_message_id);

		    if (++next_message_id == 256)
			next_message_id = 0;

		    Send(buffer, 9);

		    if (!retry)
			messages_in_transit++;

		    return;
		}
		else
		{
		    // Expired and not updated; delete entry

		    datapoint_change* tmp = dp->next;

		    delete dp;

		    if (prev)
			dp = prev->next = tmp;
		    else
			dp = change_buffer = tmp;

		    continue;
		}
	    }
	    
	    prev = dp;
	    dp = dp->next;
        }
    }
}

void
MQTTGateway::mqtt_message(mosquitto* mosq, void* obj, const struct mosquitto_message* message)
{
    MQTTGateway* this_object = (MQTTGateway*) obj;

    this_object->MQTTMessage(message);
}

void
MQTTGateway::MQTTMessage(const struct mosquitto_message* message)
{
    bool boolean_msg = false;
    int value = 0;
    
    mosquitto_topic_matches_sub("+/set/switch", message->topic, &boolean_msg);
    
    int datapoint = strtol(message->topic, NULL, 10);
    
    if (errno == EINVAL ||
	errno == ERANGE)
	return;
    
    if (boolean_msg)
    {
        if (strcmp((char*) message->payload, "true") == 0)
            value = true;
        else
            value = false;

	SetDPValue(datapoint, value, true);
    }
    else
    {
        value = strtol((char*) message->payload, NULL, 10);
	    
        if (errno == EINVAL ||
	    errno == ERANGE)
	    return;

	SetDPValue(datapoint, value, false);
    }
}

int
MQTTGateway::Init(int epoll_fd, const char* server)
{
    epoll_event mosquitto_event;
    char clientid[24];
    int err = 0;
    
    mosquitto_lib_init();
    
    memset(clientid, 0, 24);
    snprintf(clientid, 23, "xcomfort_%d", getpid());
    mosq = mosquitto_new(clientid, 0, this);
    
    if (!mosq)
	return false;

    mosquitto_message_callback_set(mosq, mqtt_message);

    err = mosquitto_connect(mosq, server, 1883, 60);

    if (err)
    {
	printf("failed to connect to MQTT server: %d\n", err);
	return false;
    }
    else
    {
	if (mosquitto_subscribe(mosq, NULL, "+/set/switch", 1))
	    return false;
	
	if (mosquitto_subscribe(mosq, NULL, "+/set/dimmer", 1))
	    return false;
    }
    
    mosquitto_event.events = EPOLLIN;
    mosquitto_event.data.ptr = this;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mosquitto_socket(mosq), &mosquitto_event) < 0)
    {
        fprintf(stderr, "epoll_ctl failed %s\n", strerror(errno));
	return false;
    }

    return USB::Init(epoll_fd);
}

void
MQTTGateway::Stop()
{
    USB::Stop();

    if (mosq)
	mosquitto_destroy(mosq);
    
    mosquitto_lib_cleanup();
}

long
MQTTGateway::Prepoll(int epoll_fd)
{
    long timeout = LONG_MAX;
    epoll_event mosquitto_event;

    mosquitto_event.data.ptr = this;

    if (change_buffer)
    {
	TrySendMore();

	// Find lowest timeout

	for (datapoint_change* dp = change_buffer; dp; dp = dp->next)
	    if (dp->new_value != -1 && timeout > dp->timeout)
		timeout = dp->timeout;

	if (timeout != LONG_MAX)
	    timeout -= getmseconds();
    }

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

    // Should be called "fairly frequently"

    mosquitto_loop_misc(mosq);

    if (timeout > 500)
	// 500ms minimum timeout, for the above call

	timeout = 500;

    return timeout;
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

int
main(int argc, char* argv[])
{
    struct sigaction sigact;
    int err;
    int epoll_fd = -1;
    MQTTGateway gateway(false);

    if (argc != 2)
    {
	fprintf(stderr, "Usage %s: [MQTT server]\n", argv[0]);
	exit(0);
    }

    epoll_fd = epoll_create(10);
    
    if (!gateway.Init(epoll_fd, argv[1]))
	goto out;
    
    sigact.sa_handler = sighandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    
    while (!do_exit)
    {
	int events;
	epoll_event event;
	int timeout = gateway.Prepoll(epoll_fd);

	events = epoll_wait(epoll_fd, &event, 1, timeout);

	if (events < 0)
	    break;
	
	if (events)
	    gateway.Poll(event);
    }
    
out:
    gateway.Stop();
    
    if (do_exit == 1)
	err = 0;
    else
	err = 1;
    
    return err >= 0 ? err : -err;
}

