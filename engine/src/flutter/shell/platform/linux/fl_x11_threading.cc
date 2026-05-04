// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gdk/gdk.h>

#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>

static void fl_linux_init_x11_threads(void) __attribute__((constructor));

static void fl_linux_init_x11_threads(void) {
  XInitThreads();
}
#endif
