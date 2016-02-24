/* -*- Mode: C++; c-file-style: "stroustrup" -*- */

/*
 *  Copyright 2016 Karl Anders Oygard. All rights reserved.
 *  Use of this source code is governed by a BSD-style license that can be
 *  found in the LICENSE file.
 */

#ifndef _USB_H_
#define _USB_H_

#include "ckoz0014.h"

bool usb_start(xc_callback_fn callback);
void usb_handle_events_timeout(struct timeval* tv);
void usb_stop();
int usb_send(const char* buffer, size_t length);

#endif
