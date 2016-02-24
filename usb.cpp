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
#define INTR_LENGTH		19
#define INTR_SEND_LENGTH	32

extern int do_exit;
extern int epoll_fd;

bool message_in_transit = false;
bool waiting_for_ack = false;

static libusb_context* context;
static struct libusb_device_handle* devh = NULL;

static unsigned char recvbuf[INTR_LENGTH];
static struct libusb_transfer* recv_transfer = NULL;

static unsigned char sendbuf[INTR_SEND_LENGTH];
static struct libusb_transfer* send_transfer = NULL;

static void LIBUSB_CALL cb_recv(struct libusb_transfer* transfer)
{
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
	fprintf(stderr, "irq transfer status %d, terminating\n", transfer->status);

	do_exit = 2;
	libusb_free_transfer(transfer);
	recv_transfer = NULL;

	return;
    }

    xc_parse_packet((char*) transfer->buffer, transfer->length, (xc_callback_fn) transfer->user_data);

    // Resubmit transfer
    
    if (libusb_submit_transfer(recv_transfer) < 0)
	do_exit = 2;
}

static void LIBUSB_CALL cb_send(struct libusb_transfer* transfer)
{
    message_in_transit = false;

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
	fprintf(stderr, "irq transfer status %d?\n", transfer->status);
	
	do_exit = 2;
	libusb_free_transfer(transfer);
	send_transfer = NULL;
	
	return;
    }
}

static void usb_fd_added_cb(int fd, short fd_events, void * source)
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

static void usb_fd_removed_cb(int fd, void* source)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0)
        fprintf(stderr, "epoll_ctl failed %s\n", strerror(errno));
}

int usb_init_fds(libusb_context* context)
{
    const struct libusb_pollfd** usb_fds = libusb_get_pollfds(context);
    
    if (!usb_fds)
	return -1;
    
    for (int numfds = 0; usb_fds[numfds] != NULL; ++numfds)
	usb_fd_added_cb(usb_fds[numfds]->fd, usb_fds[numfds]->events, NULL);
    
    free(usb_fds);
    
    libusb_set_pollfd_notifiers(context, usb_fd_added_cb, usb_fd_removed_cb, NULL);

    return 0;
}

bool usb_start(xc_callback_fn callback)
{
    int err;
    
    err = libusb_init(&context);
    if (err < 0)
    {
	fprintf(stderr, "failed to initialise libusb\n");
	exit(1);
    }
    
    devh = libusb_open_device_with_vid_pid(context, 0x188a, 0x1101);
    if (!devh)
    {
	fprintf(stderr, "Could not find/open xComfort USB device\n");
	return false;
    }
    
    if (libusb_kernel_driver_active(devh, 0) == 1)
    {
	err = libusb_detach_kernel_driver(devh, 0);
	if (err < 0)
	{
	    fprintf(stderr, "usb_detach_kernel_driver %d\n", err);
	    return false;
	}
    }
    
    err = libusb_set_configuration(devh, 1); 
    if (err < 0)
    { 
	fprintf(stderr, "libusb_set_configuration error %d\n", err); 
	return false;
    } 
    
    err = libusb_claim_interface(devh, 0);
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
    
    libusb_fill_interrupt_transfer(recv_transfer, devh, EP_IN, recvbuf,
				   sizeof(recvbuf), cb_recv, (void*) callback, 0);
    
    send_transfer = libusb_alloc_transfer(0);
    if (!send_transfer)
    {
	fprintf(stderr, "failed to allocate transfer %d\n", err);
	return false;
    }
    
    libusb_fill_interrupt_transfer(send_transfer, devh, EP_OUT, sendbuf,
				   sizeof(sendbuf), cb_send, NULL, 0);

    err = libusb_submit_transfer(recv_transfer);
    if (err < 0)
	return false;
    
    err = libusb_submit_transfer(send_transfer);
    if (err < 0)
	return false;
    
    usb_init_fds(context);

    return true;
}

void usb_handle_events_timeout(struct timeval* tv)
{
    libusb_handle_events_timeout(context, tv);
}


int usb_send(const char* buffer, size_t length)
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

    return 0;
}

void usb_stop()
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

    if (devh)
    {
	libusb_release_interface(devh, 0);
	libusb_close(devh);
    }

    libusb_exit(NULL);
}    
