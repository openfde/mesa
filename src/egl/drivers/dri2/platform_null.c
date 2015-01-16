/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xf86drm.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "egl_dri2.h"
#include "egl_dri2_fallbacks.h"
#include "loader.h"

static struct dri2_egl_display_vtbl dri2_null_display_vtbl = {
   .create_pixmap_surface = dri2_fallback_create_pixmap_surface,
   .create_image = dri2_create_image_khr,
   .swap_interval = dri2_fallback_swap_interval,
   .swap_buffers_with_damage = dri2_fallback_swap_buffers_with_damage,
   .swap_buffers_region = dri2_fallback_swap_buffers_region,
   .post_sub_buffer = dri2_fallback_post_sub_buffer,
   .copy_buffers = dri2_fallback_copy_buffers,
   .query_buffer_age = dri2_fallback_query_buffer_age,
   .create_wayland_buffer_from_image = dri2_fallback_create_wayland_buffer_from_image,
   .get_sync_values = dri2_fallback_get_sync_values,
};

static void
null_flush_front_buffer(__DRIdrawable *driDrawable, void *loaderPrivate)
{
}

static __DRIbuffer *
null_get_buffers_with_format(__DRIdrawable * driDrawable,
                             int *width, int *height,
                             unsigned int *attachments, int count,
                             int *out_count, void *loaderPrivate)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;
   struct dri2_egl_display *dri2_dpy =
      dri2_egl_display(dri2_surf->base.Resource.Display);

   dri2_surf->buffer_count = 1;
   if (width)
      *width = dri2_surf->base.Width;
   if (height)
      *height = dri2_surf->base.Height;
   *out_count = dri2_surf->buffer_count;;
   return dri2_surf->buffers;
}

static const char* node_path_fmt_card = "/dev/dri/card%d";
static const char* node_path_fmt_render = "/dev/dri/renderD%d";

EGLBoolean
dri2_initialize_null(_EGLDriver *drv, _EGLDisplay *disp)
{
   struct dri2_egl_display *dri2_dpy;
   const char* err;
   int i, render_node;
   int driver_loaded = 0;

   loader_set_logger(_eglLog);

   dri2_dpy = calloc(1, sizeof *dri2_dpy);
   if (!dri2_dpy)
      return _eglError(EGL_BAD_ALLOC, "eglInitialize");

   disp->DriverData = (void *) dri2_dpy;

   for (render_node = 1; render_node >= 0; --render_node) {
      const char* node_path_fmt =
            render_node ? node_path_fmt_render : node_path_fmt_card;
      const int base = render_node ? 128 : 0;
      for (i = 0; i < 16; ++i) {
         char *card_path;
         if (asprintf(&card_path, node_path_fmt, base + i) < 0)
            continue;

         dri2_dpy->fd = open(card_path, O_RDWR);
         free(card_path);
         if (dri2_dpy->fd < 0)
            continue;

         dri2_dpy->driver_name = loader_get_driver_for_fd(dri2_dpy->fd, 0);
         if (dri2_dpy->driver_name) {
            if (dri2_load_driver(disp)) {
               driver_loaded = 1;
               break;
            }
            free(dri2_dpy->driver_name);
         }
         close(dri2_dpy->fd);
      }
      if (driver_loaded)
         break;
   }

   if (!driver_loaded) {
      err = "DRI2: failed to load driver";
      goto cleanup_display;
   }

   dri2_dpy->dri2_loader_extension.base.name = __DRI_DRI2_LOADER;
   dri2_dpy->dri2_loader_extension.base.version = 3;
   dri2_dpy->dri2_loader_extension.getBuffers = NULL;
   dri2_dpy->dri2_loader_extension.flushFrontBuffer =
      null_flush_front_buffer;
   dri2_dpy->dri2_loader_extension.getBuffersWithFormat =
      null_get_buffers_with_format;

   dri2_dpy->extensions[0] = &dri2_dpy->dri2_loader_extension.base;
   dri2_dpy->extensions[1] = &image_lookup_extension.base;
   dri2_dpy->extensions[2] = &use_invalidate.base;
   dri2_dpy->extensions[3] = NULL;

   if (!dri2_create_screen(disp)) {
      err = "DRI2: failed to create screen";
      goto cleanup_driver;
   }

   for (i = 0; dri2_dpy->driver_configs[i]; i++) {
      EGLint attr_list[1];
      attr_list[0] = EGL_NONE;
      dri2_add_config(disp, dri2_dpy->driver_configs[i],
                      i + 1, EGL_WINDOW_BIT, attr_list, NULL);
   }

   disp->Extensions.KHR_image_base = EGL_TRUE;

   /* we're supporting EGL 1.4 */
   disp->VersionMajor = 1;
   disp->VersionMinor = 4;

   /* Fill vtbl last to prevent accidentally calling virtual function during
    * initialization.
    */
   dri2_dpy->vtbl = &dri2_null_display_vtbl;

   return EGL_TRUE;

cleanup_driver:
   dlclose(dri2_dpy->driver);
   free(dri2_dpy->driver_name);
   close(dri2_dpy->fd);
cleanup_display:
   free(dri2_dpy);

   return _eglError(EGL_NOT_INITIALIZED, err);
}
