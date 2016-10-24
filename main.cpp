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
#include <sys/stat.h>
#include <syslog.h>

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
    
    if (debug)
    syslog(LOG_INFO, "received MCI_PT_RX(%s): datapoint: %d value_type: %d value: %d (signal: %d) (battery: %s)\n",
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
	    
	    snprintf(topic, 128, "xcomfort/%d/get/dimmer", datapoint);
	    snprintf(state, 128, "%d", value);
	    
	    if (mosquitto_publish(mosq, NULL, topic, strlen(state), (const uint8_t*) state, 1, true))
	    	syslog(LOG_ERR, "failed to publish message\n");
	    
	    snprintf(topic, 128, "xcomfort/%d/get/switch", datapoint);
	    
	    if (mosquitto_publish(mosq, NULL, topic, value ? 4 : 5, value ? "true" : "false", 1, true))
	    	syslog(LOG_ERR, "failed to publish message\n");
        else
            for (datapoint_change* dp = change_buffer; dp; dp = dp->next)
                if (dp->datapoint == datapoint)
                    dp->sent_status_count = 0;

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
			syslog(LOG_ERR, "%d acked after %d ms\n", message_id, int(getmseconds() - (dp->timeout - 5500)));

		dp->active_message_id = -1;
        dp->timeout = getmseconds() + 500; // Give time to get status

		return;
	    }

	if (debug)
		syslog(LOG_INFO, "received spurious ack; message timeout is possibly too low\n");
    }
}

void
MQTTGateway::SetDPValue(int datapoint, int value, mci_tx_event event)
{
    datapoint_change* dp = change_buffer;

    for (; dp; dp = dp->next)
	if (dp->datapoint == datapoint)
	    break;

    if (dp)
    {
	// This is already a known datapoint; update values

	dp->new_value = value;
    dp->event = event;
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
    dp->event = event;

    dp->sent_status_count = 2;

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
				syslog(LOG_INFO, "message %d was lost; retrying setting %d to %d (new id %d)\n", dp->active_message_id, dp->datapoint, value, next_message_id);
		    }
		    else
		    {
			value = dp->new_value;

			if (debug)
				syslog(LOG_INFO, "setting %d to %d (id %d)\n", dp->datapoint, value, next_message_id);
		    }

		    dp->active_message_id = next_message_id;

		    dp->sent_value = value;
		    dp->new_value = -1;

		    // This is how long we'll wait until we consider the message to be lost
		    dp->timeout = current_time + 5500;

            switch (dp->event)
            {
            case SET_BOOLEAN:
                xc_make_switch_msg(buffer, dp->datapoint, value != 0, next_message_id);
                break;
            case DIM_STOP_OR_SET:
                xc_make_setpercent_msg(buffer, dp->datapoint, value, next_message_id);
                break;
            case REQUEST_STATUS:
                xc_make_requeststatus_msg(buffer, dp->datapoint, next_message_id);
                break;
            default:
            	syslog(LOG_ERR, "Unsupported event\n");
            }

		    if (++next_message_id == 256)
			next_message_id = 0;

		    Send(buffer, 9);

		    if (!retry)
			messages_in_transit++;

		    return;
		}
        else if (dp->sent_status_count)
        {

            // Did get a status update or should we force one
            if (debug)
            	syslog(LOG_INFO, "datapoint %d continue send status count %d\n", dp->datapoint, dp->sent_status_count);
            dp->sent_status_count--;
            SetDPValue(dp->datapoint, dp->sent_value, REQUEST_STATUS);
            continue;
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

void MQTTGateway::mqtt_connect(mosquitto* mosq, void* obj, int rc)
{
    syslog(LOG_INFO, "MQTT, Connected (%d), %s\n", rc, mosquitto_connack_string(rc));

    mosquitto_subscribe(mosq, NULL, "xcomfort/+/set/+", 0);
}

void MQTTGateway::mqtt_disconnect(mosquitto* mosq, void* obj, int rc)
{
    syslog(LOG_INFO, "MQTT, Disconnect (%d), %s\n", rc, mosquitto_connack_string(rc));

    if (rc)
    {
        sleep(1);
        MQTTGateway* this_object = (MQTTGateway*) obj;
        this_object->MQTTReconnect();
    }
}

void MQTTGateway::MQTTReconnect()
{
    int rc = 0;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, mosquitto_socket(mosq), NULL) < 0)
        syslog(LOG_ERR, "epoll_ctl del failed %s\n", strerror(errno));

    rc = mosquitto_reconnect(mosq);
    if (rc)
        syslog(LOG_ERR, "MQTT, Reconnecting failed (%d), %s\n", rc, mosquitto_connack_string(rc));
    else
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mosquitto_socket(mosq), &mosquitto_event) < 0)
            syslog(LOG_ERR, "epoll_ctl add failed %s\n", strerror(errno));
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
    int value = 0;
    char **topics;
    int topic_count;

    mosquitto_sub_topic_tokenise(message->topic, &topics, &topic_count);

    int datapoint = strtol(topics[1], NULL, 10);

    if (errno == EINVAL || errno == ERANGE)
        return;

    switch (mqtt_topic_type[topics[3]])
    {
    case MQTT_TOPIC_SWITCH:
        if (strcmp((char*) message->payload, "true") == 0)
            value = true;
        else
            value = false;

        SetDPValue(datapoint, value, SET_BOOLEAN);
        break;
    case MQTT_TOPIC_DIMMER:
        value = strtol((char*) message->payload, NULL, 10);

        if (errno == EINVAL || errno == ERANGE)
            return;

        SetDPValue(datapoint, value, DIM_STOP_OR_SET);
        break;
    case MQTT_TOPIC_STATUS:
        SetDPValue(datapoint, 0, REQUEST_STATUS);
        break;
    case MQTT_DEBUG:
        if (datapoint == 0)
        {
            if (strcmp((char*) message->payload, "true") == 0)
                debug = true;
            else
                debug = false;
        }
        break;
    }

    mosquitto_sub_topic_tokens_free(&topics, topic_count);
}

int
MQTTGateway::Init(int fd, const char* server, int port, const char* username, const char* password)
{
    epoll_fd = fd;

    char clientid[24];
    int err = 0;
    
    mosquitto_lib_init();
    
    memset(clientid, 0, 24);
    snprintf(clientid, 23, "xcomfort");
    mosq = mosquitto_new(clientid, 0, this);
    
    if (!mosq)
	return false;

    mosquitto_message_callback_set(mosq, mqtt_message);

    mosquitto_disconnect_callback_set(mosq, mqtt_disconnect);

    mosquitto_connect_callback_set(mosq, mqtt_connect);

    if (username && password)
    	mosquitto_username_pw_set(mosq, username, password);

    err = mosquitto_connect(mosq, server, port, 60);

    if (err)
    {
    	syslog(LOG_ERR, "failed to connect to MQTT server: %d\n", err);
    	return false;
    }
    
    mosquitto_event.events = EPOLLIN;
    mosquitto_event.data.ptr = this;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mosquitto_socket(mosq), &mosquitto_event) < 0)
    {
    	syslog(LOG_ERR, "epoll_ctl failed %s\n", strerror(errno));
        return false;
    }

    return USB::Init(epoll_fd);
}

void
MQTTGateway::Stop()
{
    USB::Stop();

    if (mosq)
    	mosquitto_disconnect(mosq);

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
	bool daemon = true;
	bool debug = false;
    int err;
    int epoll_fd = -1;
    char hostname[24] = "localhost";
    struct sigaction sigact;
    char password[24];
    int port = 1883;
    char username[24];

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug"))
			debug = true;
		else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--nofork"))
			daemon = false;
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--host"))
		{
			if (i == argc - 1)
			{
				printf("Error: -h argument given but no host specified\n");
				exit(EXIT_FAILURE);
			}
			else
			{
				sprintf(hostname, "%s", argv[i + 1]);
			}
			i++;
		}
		else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port"))
		{
			if (i == argc - 1)
			{
				printf("Error: -p argument given but no port specified\n");
				exit(EXIT_FAILURE);
			}
			else
			{
				port = atoi(argv[i + 1]);
			}
			i++;
		}
		else if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--username"))
		{
			if (i == argc - 1)
			{
				printf("Error: -u argument given but no port specified\n");
				exit(EXIT_FAILURE);
			} else
			{
				sprintf(username, "%s", argv[i + 1]);
			}
			i++;
		}
		else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--password"))
		{
			if (i == argc - 1)
			{
				printf("Error: -a argument given but no port specified\n");
				exit(EXIT_FAILURE);
			}
			else
			{
				sprintf(password, "%s", argv[i + 1]);
			}
			i++;
		}
		else if (!strcmp(argv[i], "--help"))
		{
			printf("\n\txComfort Gateway\n\n");
			printf("Usage: %s [OPTION]\n\n", argv[0]);
			printf("Options:\n");
			printf("  -d, --debug\n");
			printf("  -f, --nofork\n");
			printf("  -h, --host\n");
			printf("  -p, --port\n");
			printf("  -u, --username\n");
			printf("  -a, --password\n");
			printf("\n");
			exit(EXIT_SUCCESS);
		}
	}

	// Deamonize for startup script

	if (daemon)
	{
		pid_t pid, sid;

		pid = fork();
		if (pid < 0)
			exit(EXIT_FAILURE);
		if (pid > 0)
			exit(EXIT_SUCCESS);

		umask(0);

		openlog("xcomfortd", LOG_PID, LOG_DAEMON);

		sid = setsid();
		if (sid < 0)
			exit(EXIT_FAILURE);

		if ((chdir("/tmp/")) < 0)
			exit(EXIT_FAILURE);

		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}
	else
		openlog("xcomfortd", LOG_PERROR, LOG_USER);

    MQTTGateway gateway(debug);

    epoll_fd = epoll_create(10);
    
    if (!gateway.Init(epoll_fd, hostname, port, username, password))
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
