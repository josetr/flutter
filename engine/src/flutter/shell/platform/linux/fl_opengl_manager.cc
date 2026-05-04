// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <epoxy/egl.h>
#include <epoxy/glx.h>
#include <gdk/gdkwayland.h>
#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <gdk/gdkx.h>
#endif

#include "flutter/shell/platform/linux/fl_opengl_manager.h"

struct _FlOpenGLManager {
  GObject parent_instance;

  // Display being rendered to.
  EGLDisplay display;

  // Context used by Flutter to render.
  EGLContext render_context;

  // Context used by Flutter to share resources.
  EGLContext resource_context;

  // GLX
  gboolean use_glx;
  Display* glx_display;

  // Tiny GLX drawable used when making contexts current without presenting.
  Window glx_context_window;
  Colormap glx_colormap;
  GLXContext glx_render_context;
  GLXContext glx_resource_context;
  gulong glx_visual_id;
};

G_DEFINE_TYPE(FlOpenGLManager, fl_opengl_manager, G_TYPE_OBJECT)

#ifdef GDK_WINDOWING_X11
class XDisplayLock {
 public:
  explicit XDisplayLock(Display* display) : display_(display) {
    if (display_ != nullptr) {
      XLockDisplay(display_);
    }
  }

  ~XDisplayLock() {
    if (display_ != nullptr) {
      XUnlockDisplay(display_);
    }
  }

 private:
  Display* display_;
};
#endif

static void clear_glx_state(FlOpenGLManager* self) {
  if (self->glx_render_context != nullptr) {
    glXDestroyContext(self->glx_display, self->glx_render_context);
    self->glx_render_context = nullptr;
  }
  if (self->glx_resource_context != nullptr) {
    glXDestroyContext(self->glx_display, self->glx_resource_context);
    self->glx_resource_context = nullptr;
  }
  if (self->glx_context_window != 0) {
    XDestroyWindow(self->glx_display, self->glx_context_window);
    self->glx_context_window = 0;
  }
  if (self->glx_colormap != 0) {
    XFreeColormap(self->glx_display, self->glx_colormap);
    self->glx_colormap = 0;
  }
  self->use_glx = FALSE;
  self->glx_display = nullptr;
  self->glx_visual_id = 0;
}

#ifdef GDK_WINDOWING_X11
static gboolean init_glx(FlOpenGLManager* self, GdkDisplay* display) {
  self->glx_display = gdk_x11_display_get_xdisplay(display);
  XDisplayLock display_lock(self->glx_display);

  int visual_attribs[] = {GLX_RGBA,
                          GLX_DOUBLEBUFFER,
                          GLX_RED_SIZE,
                          8,
                          GLX_GREEN_SIZE,
                          8,
                          GLX_BLUE_SIZE,
                          8,
                          GLX_ALPHA_SIZE,
                          8,
                          GLX_DEPTH_SIZE,
                          8,
                          GLX_STENCIL_SIZE,
                          8,
                          None};
  XVisualInfo* visual = glXChooseVisual(
      self->glx_display, DefaultScreen(self->glx_display), visual_attribs);
  if (visual == nullptr) {
    g_warning("Failed to choose GLX visual");
    self->glx_display = nullptr;
    return FALSE;
  }

  self->glx_visual_id = visual->visualid;

  XSetWindowAttributes attributes = {};
  attributes.colormap = XCreateColormap(
      self->glx_display, RootWindow(self->glx_display, visual->screen),
      visual->visual, AllocNone);
  attributes.border_pixel = 0;
  attributes.event_mask = 0;
  self->glx_context_window = XCreateWindow(
      self->glx_display, RootWindow(self->glx_display, visual->screen), 0, 0, 1,
      1, 0, visual->depth, InputOutput, visual->visual,
      CWBorderPixel | CWColormap | CWEventMask, &attributes);
  self->glx_colormap = attributes.colormap;
  self->glx_render_context =
      glXCreateContext(self->glx_display, visual, nullptr, True);
  self->glx_resource_context = glXCreateContext(self->glx_display, visual,
                                                self->glx_render_context, True);
  gboolean has_direct_contexts =
      self->glx_render_context != nullptr &&
      self->glx_resource_context != nullptr &&
      glXIsDirect(self->glx_display, self->glx_render_context) &&
      glXIsDirect(self->glx_display, self->glx_resource_context);
  XFree(visual);

  if (self->glx_context_window == 0 || self->glx_render_context == nullptr ||
      self->glx_resource_context == nullptr || !has_direct_contexts) {
    g_warning("Failed to create direct GLX context");
    clear_glx_state(self);
    return FALSE;
  }

  self->use_glx = TRUE;
  XSync(self->glx_display, False);
  return TRUE;
}
#endif

static void fl_opengl_manager_dispose(GObject* object) {
  FlOpenGLManager* self = FL_OPENGL_MANAGER(object);

  if (self->use_glx) {
    XDisplayLock display_lock(self->glx_display);
    glXMakeCurrent(self->glx_display, None, nullptr);
  }
  clear_glx_state(self);
  eglDestroyContext(self->display, self->render_context);
  eglDestroyContext(self->display, self->resource_context);
  eglTerminate(self->display);

  G_OBJECT_CLASS(fl_opengl_manager_parent_class)->dispose(object);
}

static void fl_opengl_manager_class_init(FlOpenGLManagerClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_opengl_manager_dispose;
}

static void fl_opengl_manager_init(FlOpenGLManager* self) {
  GdkDisplay* display = gdk_display_get_default();
  if (GDK_IS_WAYLAND_DISPLAY(display)) {
    self->display = eglGetPlatformDisplayEXT(
        EGL_PLATFORM_WAYLAND_EXT, gdk_wayland_display_get_wl_display(display),
        NULL);
#ifdef GDK_WINDOWING_X11
  } else if (GDK_IS_X11_DISPLAY(display)) {
    self->display = eglGetPlatformDisplayEXT(
        EGL_PLATFORM_X11_EXT, gdk_x11_display_get_xdisplay(display), NULL);
#endif
  } else {
    g_critical("Unsupported GDK backend, unable to get EGL display");
  }

  eglInitialize(self->display, nullptr, nullptr);

  const EGLint config_attributes[] = {EGL_RED_SIZE,   8, EGL_GREEN_SIZE,   8,
                                      EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE,   8,
                                      EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8,
                                      EGL_NONE};
  EGLConfig config = nullptr;
  EGLint num_config = 0;
  eglChooseConfig(self->display, config_attributes, &config, 1, &num_config);

  const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  self->render_context = eglCreateContext(self->display, config, EGL_NO_CONTEXT,
                                          context_attributes);
  self->resource_context = eglCreateContext(
      self->display, config, self->render_context, context_attributes);
}

FlOpenGLManager* fl_opengl_manager_new() {
  FlOpenGLManager* self =
      FL_OPENGL_MANAGER(g_object_new(fl_opengl_manager_get_type(), nullptr));
  return self;
}

gboolean fl_opengl_manager_make_current(FlOpenGLManager* self) {
  if (self->use_glx) {
    XDisplayLock display_lock(self->glx_display);
    return glXMakeCurrent(self->glx_display, self->glx_context_window,
                          self->glx_render_context);
  }
  return eglMakeCurrent(self->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        self->render_context) == EGL_TRUE;
}

gboolean fl_opengl_manager_make_current_with_drawable(
    FlOpenGLManager* self,
    FlOpenGLDrawable drawable) {
  if (self->use_glx) {
    XDisplayLock display_lock(self->glx_display);
    return glXMakeCurrent(self->glx_display, static_cast<Window>(drawable),
                          self->glx_render_context);
  }
  return FALSE;
}

gboolean fl_opengl_manager_make_resource_current(FlOpenGLManager* self) {
  if (self->use_glx) {
    XDisplayLock display_lock(self->glx_display);
    return glXMakeCurrent(self->glx_display, self->glx_context_window,
                          self->glx_resource_context);
  }
  return eglMakeCurrent(self->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        self->resource_context) == EGL_TRUE;
}

gboolean fl_opengl_manager_clear_current(FlOpenGLManager* self) {
  if (self->use_glx) {
    XDisplayLock display_lock(self->glx_display);
    return glXMakeCurrent(self->glx_display, None, nullptr);
  }
  return eglMakeCurrent(self->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT) == EGL_TRUE;
}

gboolean fl_opengl_manager_try_enable_glx(FlOpenGLManager* self,
                                          GdkDisplay* display) {
#ifdef GDK_WINDOWING_X11
  if (self->use_glx) {
    return TRUE;
  }
  if (!GDK_IS_X11_DISPLAY(display)) {
    return FALSE;
  }
  return init_glx(self, display);
#else
  return FALSE;
#endif
}

void fl_opengl_manager_disable_glx(FlOpenGLManager* self) {
  if (!self->use_glx && self->glx_display == nullptr) {
    return;
  }
  Display* display = self->glx_display;
  XDisplayLock display_lock(display);
  if (self->use_glx) {
    glXMakeCurrent(display, None, nullptr);
  }
  clear_glx_state(self);
}

gulong fl_opengl_manager_get_glx_visual_id(FlOpenGLManager* self) {
  return self->glx_visual_id;
}

gboolean fl_opengl_manager_swap_buffers(FlOpenGLManager* self,
                                        FlOpenGLDrawable drawable) {
  if (self->use_glx) {
    XDisplayLock display_lock(self->glx_display);
    glXSwapBuffers(self->glx_display, static_cast<Window>(drawable));
    return TRUE;
  }
  return FALSE;
}
