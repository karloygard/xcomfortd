/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _USB_H_
#define _USB_H_

#include "ckoz0014.h"

#define INTR_LENGTH		19
#define INTR_SEND_LENGTH	32

class USB
{
public:

    USB();

    virtual int Init(int epoll_fd);
    virtual void Stop();

    virtual void Poll(const epoll_event& event);

    bool CanSend() const { return !message_in_transit; }
    int Send(const char* buffer, size_t length);

protected:

    virtual void TrySendMore() {}

private:

    static void message_received(void* user_data,
				 mci_rx_event event,
				 int datapoint,
				 mci_rx_datatype data_type,
				 int value,
				 int signal,
				 mci_battery_status battery);

    static void ack_received(void* user_data,
			     int success,
			     int message_id);

    virtual void MessageReceived(mci_rx_event event,
				 int datapoint,
				 mci_rx_datatype data_type,
				 int value,
				 int signal,
				 mci_battery_status battery) {}

    virtual void AckReceived(int success,
			     int message_id) {}

    static void sent(struct libusb_transfer* transfer);
    static void received(struct libusb_transfer* transfer);

    void Sent(struct libusb_transfer* transfer);
    void Received(struct libusb_transfer* transfer);

    static void fd_added(int fd, short fd_events, void* source);
    static void fd_removed(int fd, void* source);

    void FDAdded(int fd, short fd_events);
    void FDRemoved(int fd);

    bool init_fds();

    bool message_in_transit;

    int epoll_fd;

    libusb_context* context;
    libusb_device_handle* handle;

    unsigned char recvbuf[INTR_LENGTH];
    libusb_transfer* recv_transfer;

    unsigned char sendbuf[INTR_SEND_LENGTH];
    libusb_transfer* send_transfer;
};

#endif
