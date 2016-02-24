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

#include "ckoz0014.h"
#include "usb.h"

#define mqtt_host "192.168.1.1"
#define mqtt_port 1883

int epoll_fd = 0;
struct mosquitto* mosq = NULL;

int do_exit = 0;

void
callback(mci_rx_event event,
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
	    
	    snprintf(topic, 128, "%d/get", datapoint);
	    snprintf(state, 128, "%d", value);
	    
	    if (mosquitto_publish(mosq, NULL, topic, strlen(state), (const uint8_t*) state, 1, true))
		fprintf(stderr, "failed to publish message\n");
	    
	    snprintf(topic, 128, "%d/get_boolean", datapoint);
	    
	    if (mosquitto_publish(mosq, NULL, topic, value ? 4 : 5, value ? "true" : "false", 1, true))
		fprintf(stderr, "failed to publish message\n");
	}
	break;

    default:
	break;
    }
}

void
set_boolean(int datapoint, int value)
{
    char buffer[9];

    printf("setting %d to %d\n", datapoint, value);

    xc_make_switch_msg(buffer, datapoint, value != 0);
    usb_send(buffer, 9);
}

void
set_value(int datapoint, int value)
{
    char buffer[9];

    printf("setting %d to %d\n", datapoint, value);

    xc_make_setpercent_msg(buffer, datapoint, value);
    usb_send(buffer, 9);
}

static void
sighandler(int signum)
{
    do_exit = 1;
}

void
message_callback(mosquitto* mosq, void* obj, const struct mosquitto_message* message)
{
    bool boolean_msg = false;
    int value = 0;
    
    mosquitto_topic_matches_sub("+/set_boolean", message->topic, &boolean_msg);
    
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

	set_boolean(datapoint, value);
    }
    else
    {
        value = strtol((char*) message->payload, NULL, 10);
	    
        if (errno == EINVAL ||
	    errno == ERANGE)
	    return;

	set_value(datapoint, value);
    }
}


int
main(void)
{
    epoll_event mosquitto_event;
    char clientid[24];
    int err = 0;
    
    mosquitto_lib_init();
    
    memset(clientid, 0, 24);
    snprintf(clientid, 23, "xcomfort_%d", getpid());
    mosq = mosquitto_new(clientid, 0, NULL);
    
    if (mosq)
    {
	mosquitto_message_callback_set(mosq, message_callback);
	
	err = mosquitto_connect(mosq, mqtt_host, mqtt_port, 60);
	
	if (err)
        {
	    printf("failed to connect to MQTT server: %d\n", err);
            goto out;
	}
	else
	{
	    err = mosquitto_subscribe(mosq, NULL, "+/set", 1);
	    err = mosquitto_subscribe(mosq, NULL, "+/set_boolean", 1);
	}
    }
    
    epoll_fd = epoll_create(10);
    
    mosquitto_event.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mosquitto_socket(mosq), &mosquitto_event) < 0)
        fprintf(stderr, "epoll_ctl failed %s\n", strerror(errno));
    
    struct sigaction sigact;
    
    if (!usb_start(callback))
	goto out;
    
    sigact.sa_handler = sighandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    
    while (!do_exit)
    {
	epoll_event events;
	struct timeval tv = { 0, 0 };
	
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
	
	if (epoll_wait(epoll_fd, &events, 1, 500) < 0)
	    break;
	
	if (events.data.ptr)
	{
	    // This is for mosquitto
	    
	    if (events.events & POLLIN)
		mosquitto_loop_read(mosq, 1);
	    if (events.events & POLLOUT)
		mosquitto_loop_write(mosq, 1);
	}
	
	mosquitto_loop_misc(mosq);
	usb_handle_events_timeout(&tv);
    }
    
out:
    usb_stop();
    
    if (do_exit == 1)
	err = 0;
    else
	err = 1;
    
    if (mosq)
	mosquitto_destroy(mosq);
    
    mosquitto_lib_cleanup();
    
    return err >= 0 ? err : -err;
}

