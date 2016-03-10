/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <libusb-1.0/libusb.h>
#include <poll.h>
#include <sys/epoll.h>

#include "usb.h"

#define EP_IN			(1 | LIBUSB_ENDPOINT_IN)
#define EP_OUT			(2 | LIBUSB_ENDPOINT_OUT)

extern int do_exit;

void
USB::received(struct libusb_transfer* transfer)
{
    USB* this_object = (USB*) transfer->user_data;

    this_object->Received(transfer);
}

void
USB::Received(struct libusb_transfer* transfer)
{
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
	fprintf(stderr, "irq transfer status %d, terminating\n", transfer->status);

	do_exit = 2;
	libusb_free_transfer(transfer);
	recv_transfer = NULL;
    }
    else
    {
	xc_parse_packet((char*) transfer->buffer, transfer->length, message_received, ack_received, this);

	// Resubmit transfer
    
	if (libusb_submit_transfer(recv_transfer) < 0)
	    do_exit = 2;
    }
}

void
USB::message_received(void* user_data,
		      mci_rx_event event,
		      int datapoint,
		      mci_rx_datatype data_type,
		      int value,
		      int signal,
		      mci_battery_status battery)
{
    USB* this_object = (USB*) user_data;

    this_object->MessageReceived(event,
				 datapoint,
				 data_type,
				 value,
				 signal,
				 battery);
}

void
USB::ack_received(void* user_data,
		   int success,
		   int message_id)
{
    USB* this_object = (USB*) user_data;

    this_object->AckReceived(success, message_id);
}

void
USB::sent(struct libusb_transfer* transfer)
{
    USB* this_object = (USB*) transfer->user_data;

    this_object->Sent(transfer);
}

void
USB::Sent(struct libusb_transfer* transfer)
{
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
	fprintf(stderr, "irq transfer status %d?\n", transfer->status);
	
	do_exit = 2;
	libusb_free_transfer(transfer);
	send_transfer = NULL;
    }
    else
	message_in_transit = false;
}

void
USB::fd_added(int fd, short fd_events, void * source)
{
    USB* this_object = (USB*) source;

    this_object->FDAdded(fd, fd_events);
}

void
USB::FDAdded(int fd, short fd_events)
{
    epoll_event events;

    bzero(&events, sizeof(events));
    if (fd_events & POLLIN)
	events.events |= EPOLLIN;
    if (fd_events & POLLOUT)
	events.events |= EPOLLOUT;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &events) < 0)
        fprintf(stderr, "epoll_ctl failed %s\n", strerror(errno));
}

void
USB::fd_removed(int fd, void* source)
{
    USB* this_object = (USB*) source;

    this_object->FDRemoved(fd);
}

void
USB::FDRemoved(int fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0)
        fprintf(stderr, "epoll_ctl failed %s\n", strerror(errno));
}

bool
USB::init_fds()
{
    const struct libusb_pollfd** usb_fds = libusb_get_pollfds(context);
    
    if (!usb_fds)
	return false;
    
    for (int numfds = 0; usb_fds[numfds] != NULL; ++numfds)
	FDAdded(usb_fds[numfds]->fd, usb_fds[numfds]->events);
    
    free(usb_fds);
    
    libusb_set_pollfd_notifiers(context, fd_added, fd_removed, this);

    return true;
}

USB::USB()
    : message_in_transit(false),
      epoll_fd(-1),
      context(NULL),
      handle(NULL),
      recv_transfer(NULL),
      send_transfer(NULL)
{
}

int
USB::Init(int fd)
{
    int err;
    
    epoll_fd = fd;

    err = libusb_init(&context);
    if (err < 0)
    {
	fprintf(stderr, "failed to initialise libusb\n");
	exit(1);
    }
    
    handle = libusb_open_device_with_vid_pid(context, 0x188a, 0x1101);
    if (!handle)
    {
	fprintf(stderr, "Could not find/open xComfort USB device\n");
	return false;
    }
    
    if (libusb_kernel_driver_active(handle, 0) == 1)
    {
	err = libusb_detach_kernel_driver(handle, 0);
	if (err < 0)
	{
	    fprintf(stderr, "usb_detach_kernel_driver %d\n", err);
	    return false;
	}
    }
    
    err = libusb_set_configuration(handle, 1); 
    if (err < 0)
    { 
	fprintf(stderr, "libusb_set_configuration error %d\n", err); 
	return false;
    } 
    
    err = libusb_claim_interface(handle, 0);
    if (err < 0)
    {
	fprintf(stderr, "usb_claim_interface error %d\n", err);
	return false;
    }
    
    recv_transfer = libusb_alloc_transfer(0);
    if (!recv_transfer)
    {
	fprintf(stderr, "failed to allocate transfer %d\n", err);
	return false;
    }
    
    libusb_fill_interrupt_transfer(recv_transfer, handle, EP_IN, recvbuf,
				   sizeof(recvbuf), received, (void*) this, 0);
    
    send_transfer = libusb_alloc_transfer(0);
    if (!send_transfer)
    {
	fprintf(stderr, "failed to allocate transfer %d\n", err);
	return false;
    }
    
    libusb_fill_interrupt_transfer(send_transfer, handle, EP_OUT, sendbuf,
				   sizeof(sendbuf), sent, this, 0);

    err = libusb_submit_transfer(recv_transfer);
    if (err < 0)
	return false;

    xc_make_mgmt_msg((char*) sendbuf, CK_RELNO, 0);
    
    err = libusb_submit_transfer(send_transfer);
    if (err < 0)
	return false;
    
    if (!init_fds())
	return false;

    return true;
}

void
USB::Poll(const epoll_event& event)
{
    struct timeval tv = { 0, 0 };

    libusb_handle_events_timeout(context, &tv);
}


int
USB::Send(const char* buffer, size_t length)
{
    int err;

    bzero(sendbuf, INTR_SEND_LENGTH);
    memcpy(sendbuf, buffer, length);

    err = libusb_submit_transfer(send_transfer);
    if (err < 0)
    {
	fprintf(stderr, "failed to submit transfer\n");
	return -1;
    }

    message_in_transit = true;

    return 0;
}

void
USB::Stop()
{
    if (context)
    {
	if (recv_transfer)
	    if (!libusb_cancel_transfer(recv_transfer))
		while (recv_transfer)
		    if (libusb_handle_events(NULL) < 0)
			break;

	if (send_transfer)
	    if (!libusb_cancel_transfer(send_transfer))
		while (send_transfer)
		    if (libusb_handle_events(NULL) < 0)
			break;

	if (recv_transfer)
	    libusb_free_transfer(recv_transfer);

	if (send_transfer)
	    libusb_free_transfer(send_transfer);

	if (handle)
	{
	    libusb_release_interface(handle, 0);
	    libusb_close(handle);
	}

	libusb_exit(context);
    }
}    
