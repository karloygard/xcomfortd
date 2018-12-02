/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <mosquitto.h>

#include <sys/epoll.h>
#include <sys/stat.h>
#include <syslog.h>
#include <getopt.h>
#include <stdarg.h>
#include <map>

#include "main.h"

int do_exit = 0;

enum mqtt_topics
{
    MQTT_TOPIC_SWITCH,
    MQTT_TOPIC_DIMMER,
    MQTT_TOPIC_SHUTTER,
    MQTT_TOPIC_REQUEST_STATUS,
    MQTT_DEBUG
};

std::map<std::string, mqtt_topics> mqtt_topic_type = {
    { "switch", MQTT_TOPIC_SWITCH },
    { "dimmer", MQTT_TOPIC_DIMMER },
    { "shutter", MQTT_TOPIC_SHUTTER },
    { "requeststatus", MQTT_TOPIC_REQUEST_STATUS },
    { "debug", MQTT_DEBUG }
};

std::map<std::string, mci_sb_command> shutter_cmd_type = {
    { "down", MGW_TED_CLOSE },
    { "up", MGW_TED_OPEN },
    { "stop", MGW_TED_JSTOP }
};

static void
sighandler(int signum)
{
    do_exit = 1;
}

XCtoMQTT::XCtoMQTT(bool verbose, bool use_syslog)
    : MQTTGateway(verbose),
      change_buffer(NULL),
      next_message_id(0),
      messages_in_transit(0),
      use_syslog(use_syslog)
{
}

void
XCtoMQTT::Relno(int status,
		unsigned int rf_major,
		unsigned int rf_minor,
		unsigned int usb_major,
		unsigned int usb_minor)
{
    if (verbose)
    {
        if (status == 0x10)
	    Info("CKOZ-00/14 revision numbers: HW-Rev %d, RF-Rev %d, FW-Rev %d\n",
	         rf_major,
	         rf_minor,
	         (usb_major << 8) + usb_minor);
        else
	    Info("CKOZ-00/14 version numbers: RFV%d.%02d, USBV%d.%02d\n",
	         rf_major,
	         rf_minor,
	         usb_major,
	         usb_minor);
    }
}

void
XCtoMQTT::MessageReceived(mci_rx_event event,
			  int datapoint,
			  mci_rx_datatype data_type,
			  int value,
			  int rssi,
			  mgw_rx_battery battery)
{
    if (verbose)
	Info("received MGW_PT_RX(%s): datapoint: %d value_type: %d value: %d (signal: %s) (battery: %s)\n",
             xc_rxevent_name(event),
	     datapoint,
	     data_type,
	     value,
             xc_rssi_status_name(rssi),
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

	    snprintf(topic, 128, "xcomfort/%d/get/shutter", datapoint);

	    if (mosquitto_publish(mosq, NULL, topic, strlen(xc_shutter_status_name(value)), xc_shutter_status_name(value), 1, true))
		Error("failed to publish message\n");

	    for (datapoint_change* dp = change_buffer; dp; dp = dp->next)
		if (dp->datapoint == datapoint)
		{
		    if (dp->event == MGW_TE_REQUEST)
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
XCtoMQTT::AckReceived(int success, int message_id)
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

		    if (dp->event != MGW_TE_REQUEST)
		    {
			dp->event = MGW_TE_REQUEST;
			dp->sent_status_requests = 0;
		    }

		    // Give time to get status

		    dp->timeout = getmseconds() + 1000;
		}

		return;
	    }

	if (verbose)
	    Info("received spurious ack %d; message timeout is possibly too low\n", message_id);
    }
}

void
XCtoMQTT::SendDPValue(int datapoint, int value, mci_tx_event event)
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

	if (event != MGW_TE_REQUEST)
	{
	    // No need to do this for MGW_TE_REQUEST, status will be
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
XCtoMQTT::TrySendMore()
{
    if (messages_in_transit >= 4)
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
	int64_t current_time = getmseconds();
	datapoint_change* dp = change_buffer;
	datapoint_change* prev = NULL;

	while (dp)
	{
	    if (dp->timeout <= current_time)
	    {
		// Time to inspect this datapoint

		if (dp->active_message_id != -1 ||
		    dp->new_value != -1 ||
		    (dp->event == MGW_TE_REQUEST &&
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
                        {
			    if (dp->event == MGW_TE_REQUEST)
			        Info("message %d was lost; retrying status request from DP %d (new id %d)\n",
				     dp->active_message_id, dp->datapoint, next_message_id);
                            else
			        Info("message %d was lost; retrying setting DP %d to %d (new id %d)\n",
				     dp->active_message_id, dp->datapoint, value, next_message_id);
		        }
		    }
		    else
		    {
			value = dp->new_value;

			if (dp->event == MGW_TE_REQUEST)
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
		    case MGW_TE_SWITCH:
			xc_make_switch_msg(buffer, dp->datapoint, value != 0, next_message_id);
			break;

		    case MGW_TE_DIM:
			xc_make_dim_msg(buffer, dp->datapoint, value, next_message_id);
			break;

		    case MGW_TE_JALO:
			xc_make_jalo_msg(buffer, dp->datapoint, (mci_sb_command) value, next_message_id);
			break;

		    case MGW_TE_REQUEST:
			xc_make_request_msg(buffer, dp->datapoint, next_message_id);
			break;

		    default:
			Error("Unsupported event\n");
			return;
		    }

		    if (++next_message_id == 16)
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
XCtoMQTT::MQTTMessage(const struct mosquitto_message* message)
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

        SendDPValue(datapoint, value, MGW_TE_SWITCH);
        break;

    case MQTT_TOPIC_DIMMER:
        value = strtol((char*) message->payload, NULL, 10);

        if (errno == EINVAL || errno == ERANGE)
            return;

        SendDPValue(datapoint, value, MGW_TE_DIM);
        break;

    case MQTT_TOPIC_SHUTTER:
	SendDPValue(datapoint, shutter_cmd_type[(char*) message->payload], MGW_TE_JALO);
        break;

    case MQTT_TOPIC_REQUEST_STATUS:
        SendDPValue(datapoint, -1, MGW_TE_REQUEST);
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

    default:
	Error("Unknown topic\n");
        break;
    }

    mosquitto_sub_topic_tokens_free(&topics, topic_count);
}

int
XCtoMQTT::Prepoll(int epoll_fd)
{
    int next_change = INT_MAX;
    int timeout = MQTTGateway::Prepoll(epoll_fd);
    int64_t current_time = getmseconds();

    if (change_buffer)
    {
	TrySendMore();

	// Find lowest timeout

	for (datapoint_change* dp = change_buffer; dp; dp = dp->next)
	    if (dp->new_value != -1 && next_change > dp->timeout - current_time)
		next_change = dp->timeout - current_time;
    }

    if (timeout < next_change)
	return timeout;

    return next_change;
}

void
XCtoMQTT::Info(const char* fmt, ...)
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
XCtoMQTT::Error(const char* fmt, ...)
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
    char* password = NULL;
    char* username = NULL;
    int port = 1883;

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
	    username = strdup(optarg);
	    break;

	case 'P':
	    password = strdup(optarg);
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

    XCtoMQTT gateway(verbose, daemon);

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

    if (password)
	free(password);

    if (username)
	free(username);

    if (do_exit == 1)
	return 0;

    return 1;
}
