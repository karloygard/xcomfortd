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
#include <unistd.h>
#include <mosquitto.h>

#include <libusb-1.0/libusb.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <syslog.h>
#include <getopt.h>
#include <stdarg.h>

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

MQTTGateway::MQTTGateway(bool verbose, bool use_syslog)
    : mosq(NULL),
      change_buffer(NULL),
      next_message_id(0),
      messages_in_transit(0),
      verbose(verbose),
      use_syslog(use_syslog)
{
}

void
MQTTGateway::Relno(int rf_major,
		   int rf_minor,
		   int usb_major,
		   int usb_minor)
{
    if (verbose)
	Info("CKOZ-00/14 version numbers: RFV%d.%02d, USBV%d.%02d\n",
	     rf_major,
	     rf_minor,
	     usb_major,
	     usb_minor);
}

void
MQTTGateway::MessageReceived(mci_rx_event event,
			     int datapoint,
			     mci_rx_datatype data_type,
			     int value,
			     int signal,
			     mci_battery_status battery)
{
    if (verbose)
	Info("received MCI_PT_RX(%s): datapoint: %d value_type: %d value: %d (signal: %d) (battery: %s)\n",
             xc_rxevent_name(event),
	     datapoint,
	     data_type,
	     value,
	     signal,
             xc_battery_status_name(battery));

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
		Error("failed to publish message\n");
	    
	    snprintf(topic, 128, "xcomfort/%d/get/switch", datapoint);
	    
	    if (mosquitto_publish(mosq, NULL, topic, value ? 4 : 5, value ? "true" : "false", 1, true))
		Error("failed to publish message\n");

	    for (datapoint_change* dp = change_buffer; dp; dp = dp->next)
		if (dp->datapoint == datapoint)
		{
		    if (dp->event == REQUEST_STATUS)
			// We're done

			dp->sent_status_requests = 3;

		    break;
		}
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
		// We got an ack for this message; clear to send next
		// messages, if any

		if (verbose)
		    Info("Message id %d acked after %d ms\n", message_id, int(getmseconds() - (dp->timeout - 5500)));

		dp->active_message_id = -1;

		if (dp->new_value != -1)
		    // Send next value asap

		    dp->timeout = 0;
		else
		{
		    // No new value to send, set up status request to
		    // be sent if status is not received within
		    // reasonable time

		    if (dp->event != REQUEST_STATUS)
		    {
			dp->event = REQUEST_STATUS;
			dp->sent_status_requests = 0;
		    }

		    // Give time to get status

		    dp->timeout = getmseconds() + 1000;
		}

		return;
	    }

	if (verbose)
	    Info("received spurious ack; message timeout is possibly too low\n");
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
	// This datapoint has pending or active messages, update
	// values in place and let the system handle it when it's
	// ready

	if (event != REQUEST_STATUS)
	{
	    // No need to do this for REQUEST_STATUS, status will be
	    // reported implicitly or requested explicity if missing
	    // anyways

	    dp->new_value = value;
	    dp->event = event;
	}
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
	dp->timeout = 0;

	dp->active_message_id = -1;

	change_buffer = dp;
    }

    dp->sent_status_requests = 0;
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
		// Time to inspect this datapoint

		if (dp->active_message_id != -1 ||
		    dp->new_value != -1 ||
		    (dp->event == REQUEST_STATUS &&
		     dp->sent_status_requests < 3))
		{
		    // Unacked or unsent; needs attention

		    char buffer[9];
		    bool waiting_for_ack = dp->active_message_id != -1;
		    int value;

		    if (waiting_for_ack)
		    {
			// Was value updated in the meanwhile?
			
			if (dp->new_value != -1)
			    value = dp->new_value;
			else
			    value = dp->sent_value;

			if (verbose)
			    Info("message %d was lost; retrying (new id %d)\n",
				 dp->active_message_id, next_message_id);
		    }
		    else
		    {
			value = dp->new_value;

			if (dp->event == REQUEST_STATUS)
			{
			    if (verbose)
				Info("requesting status from DP %d (message id %d, try %d)\n",
				     dp->datapoint, next_message_id, dp->sent_status_requests);

			    dp->sent_status_requests++;
			}
			else
			    if (verbose)
				Info("setting DP %d to %d (message id %d)\n",
				     dp->datapoint, value, next_message_id);
		    }

		    dp->active_message_id = next_message_id;
		    dp->new_value = -1;
		    dp->sent_value = value;

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
			Error("Unsupported event\n");
		    }

		    if (++next_message_id == 256)
			next_message_id = 0;

		    Send(buffer, 9);

		    if (!waiting_for_ack)
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
	Info("MQTT Disconnected, %s\n", mosquitto_connack_string(rc));

    if (rc)
    {
        sleep(1);

	if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, mosquitto_socket(mosq), NULL) < 0)
	    Error("epoll_ctl del failed %s\n", strerror(errno));

	rc = mosquitto_reconnect(mosq);

	if (rc)
	    Info("MQTT, Reconnecting failed (%d), %s\n", rc, mosquitto_connack_string(rc));
	else
	    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mosquitto_socket(mosq), &mosquitto_event) < 0)
		Error("epoll_ctl add failed %s\n", strerror(errno));
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
    int value = 0;
    char** topics;
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
        SetDPValue(datapoint, -1, REQUEST_STATUS);
        break;

    case MQTT_DEBUG:
        if (datapoint == 0)
	{
            if (strcmp((char*) message->payload, "true") == 0)
                verbose = true;
            else
                verbose = false;
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
    mosquitto_disconnect_callback_set(mosq, mqtt_disconnected);
    mosquitto_connect_callback_set(mosq, mqtt_connected);

    if (username && password)
    	mosquitto_username_pw_set(mosq, username, password);

    err = mosquitto_connect(mosq, server, port, 60);

    if (err)
    {
	Error("failed to connect to MQTT server: %d\n", err);
    	return false;
    }
    
    mosquitto_event.events = EPOLLIN;
    mosquitto_event.data.ptr = this;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mosquitto_socket(mosq), &mosquitto_event) < 0)
    {
	Error("epoll_ctl failed %s\n", strerror(errno));
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

void
MQTTGateway::Info(const char* fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);

    if (use_syslog)
	vsyslog(LOG_INFO, fmt, argptr);
    else
	vprintf(fmt, argptr);

    va_end(argptr);
}

void
MQTTGateway::Error(const char* fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);

    if (use_syslog)
	vsyslog(LOG_ERR, fmt, argptr);
    else
	vfprintf(stderr, fmt, argptr);

    va_end(argptr);
}

int
main(int argc, char* argv[])
{
    bool daemon = false;
    bool verbose = false;
    int epoll_fd = -1;
    char hostname[32] = "localhost";
    struct sigaction sigact;
    char password[32];
    int port = 1883;
    char username[32];

    int argindex = 0;

    static struct option long_options[] =
    {
	{"verbose",  no_argument,       0, 'v'},
	{"daemon",   no_argument,       0, 'd'},
	{"help",     no_argument,       0, 0},
	{"port",     required_argument, 0, 'p'},
	{"host",     required_argument, 0, 'h'},
	{"username", required_argument, 0, 'u'},
	{"password", required_argument, 0, 'P'},
	{0, 0, 0, 0}
    };

    for (;;)
    {
	int c = getopt_long(argc, argv, "vdh:p:u:P:",
			    long_options, &argindex);

	if (c == -1)
	    break;

	switch (c)
	{
	case 'v':
	    verbose = true;
	    break;

	case 'd':
	    daemon = true;
	    break;

	case 'p':
	    port = atoi(optarg);
	    break;

	case 'h':
	    snprintf(hostname, sizeof(hostname), "%s", optarg);
	    break;

	case 'u':
	    snprintf(username, sizeof(username), "%s", optarg);
	    break;

	case 'P':
	    snprintf(password, sizeof(password), "%s", optarg);
	    break;

	default:
	    printf("Usage: %s [OPTION]\n", argv[0]);
	    printf("xComfort to MQTT gateway.\n\n");
	    printf("Options:\n");
	    printf("  -v, --verbose\n");
	    printf("  -d, --daemon\n");
	    printf("  -h, --host (default: localhost)\n");
	    printf("  -p, --port (default: 1883)\n");
	    printf("  -u, --username\n");
	    printf("  -P, --password\n");
	    printf("\n");
	    exit(EXIT_SUCCESS);
	}
    }

    // Daemonize for startup script

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

    MQTTGateway gateway(verbose, daemon);

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
	return 0;

    return 1;
}
