// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_OPENGL_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_OPENGL_MANAGER_H_

#include <glib-object.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef struct _GdkDisplay GdkDisplay;
typedef uintptr_t FlOpenGLDrawable;

G_DECLARE_FINAL_TYPE(FlOpenGLManager,
                     fl_opengl_manager,
                     FL,
                     OPENGL_MANAGER,
                     GObject)

/**
 * fl_opengl_manager_new:
 *
 * Creates an object that allows Flutter to render by OpenGL ES.
 *
 * Returns: a new #FlOpenGLManager.
 */
FlOpenGLManager* fl_opengl_manager_new();

/**
 * fl_opengl_manager_make_current:
 * @manager: an #FlOpenGLManager.
 *
 * Makes the rendering context current.
 *
 * Returns: %TRUE if the context made current.
 */
gboolean fl_opengl_manager_make_current(FlOpenGLManager* manager);

/**
 * fl_opengl_manager_make_current_with_drawable:
 * @manager: an #FlOpenGLManager.
 * @drawable: native drawable to make current.
 *
 * Makes the rendering context current with a native drawable.
 *
 * Returns: %TRUE if the context made current.
 */
gboolean fl_opengl_manager_make_current_with_drawable(
    FlOpenGLManager* manager,
    FlOpenGLDrawable drawable);

/**
 * fl_opengl_manager_make_resource_current:
 * @manager: an #FlOpenGLManager.
 *
 * Makes the resource rendering context current.
 *
 * Returns: %TRUE if the context made current.
 */
gboolean fl_opengl_manager_make_resource_current(FlOpenGLManager* manager);

/**
 * fl_opengl_manager_clear_current:
 * @manager: an #FlOpenGLManager.
 *
 * Clears the current rendering context.
 *
 * Returns: %TRUE if the context cleared.
 */
gboolean fl_opengl_manager_clear_current(FlOpenGLManager* manager);

/**
 * fl_opengl_manager_try_enable_glx:
 * @manager: an #FlOpenGLManager.
 * @display: a #GdkDisplay.
 *
 * Initializes GLX contexts for X11 direct presentation.
 *
 * Returns: %TRUE if GLX was initialized.
 */
gboolean fl_opengl_manager_try_enable_glx(FlOpenGLManager* manager,
                                          GdkDisplay* display);

/**
 * fl_opengl_manager_disable_glx:
 * @manager: an #FlOpenGLManager.
 *
 * Disables GLX contexts and returns to the EGL contexts.
 */
void fl_opengl_manager_disable_glx(FlOpenGLManager* manager);

/**
 * fl_opengl_manager_get_glx_visual_id:
 * @manager: an #FlOpenGLManager.
 *
 * Gets the X11 visual ID used by the OpenGL context.
 *
 * Returns: X11 visual ID, or 0 if unavailable.
 */
gulong fl_opengl_manager_get_glx_visual_id(FlOpenGLManager* manager);

/**
 * fl_opengl_manager_swap_buffers:
 * @manager: an #FlOpenGLManager.
 * @drawable: native drawable to swap.
 *
 * Swaps native drawable buffers.
 *
 * Returns: %TRUE if buffers were swapped.
 */
gboolean fl_opengl_manager_swap_buffers(FlOpenGLManager* manager,
                                        FlOpenGLDrawable drawable);

G_END_DECLS

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_OPENGL_MANAGER_H_
