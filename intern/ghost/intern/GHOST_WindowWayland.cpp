/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup GHOST
 */

#include "GHOST_WindowWayland.h"
#include "GHOST_SystemWayland.h"
#include "GHOST_WindowManager.h"

#include "GHOST_Event.h"

#include "GHOST_ContextEGL.h"
#include "GHOST_ContextNone.h"

#include <wayland-egl.h>

#include <libdecoration/libdecoration.h>

struct window_t {
  GHOST_WindowWayland *w;
  wl_surface *surface;
  struct libdecor_frame *frame;
  wl_egl_window *egl_window;
  bool is_maximised;
  bool is_fullscreen;
  bool is_active;
  int32_t width, height;
};

/* -------------------------------------------------------------------- */
/** \name Wayland Interface Callbacks
 *
 * These callbacks are registered for Wayland interfaces and called when
 * an event is received from the compositor.
 * \{ */

//static void toplevel_configure(
//    void *data, xdg_toplevel * /*xdg_toplevel*/, int32_t width, int32_t height, wl_array *states)
//{
//  window_t *win = static_cast<window_t *>(data);
//  win->pending_width = width;
//  win->pending_height = height;

//  win->is_maximised = false;
//  win->is_fullscreen = false;
//  win->is_active = false;

//  /* Note that the macro 'wl_array_for_each' would typically be used to simplify this logic,
//   * however it's not compatible with C++, so perform casts instead.
//   * If this needs to be done more often we could define our own C++ compatible macro. */
//  for (enum xdg_toplevel_state *state = static_cast<xdg_toplevel_state *>(states->data);
//       reinterpret_cast<uint8_t *>(state) < (static_cast<uint8_t *>(states->data) + states->size);
//       state++) {
//    switch (*state) {
//      case XDG_TOPLEVEL_STATE_MAXIMIZED:
//        win->is_maximised = true;
//        break;
//      case XDG_TOPLEVEL_STATE_FULLSCREEN:
//        win->is_fullscreen = true;
//        break;
//      case XDG_TOPLEVEL_STATE_ACTIVATED:
//        win->is_active = true;
//        break;
//      default:
//        break;
//    }
//  }
//}

//static void toplevel_close(void *data, xdg_toplevel * /*xdg_toplevel*/)
//{
//  static_cast<window_t *>(data)->w->close();
//}

//static const xdg_toplevel_listener toplevel_listener = {
//    toplevel_configure,
//    toplevel_close,
//};

//static void surface_configure(void *data, xdg_surface *xdg_surface, uint32_t serial)
//{
//  window_t *win = static_cast<window_t *>(data);

//  int w, h;
//  wl_egl_window_get_attached_size(win->egl_window, &w, &h);
//  if (win->pending_width != 0 && win->pending_height != 0 && win->pending_width != w &&
//      win->pending_height != h) {
//    win->width = win->pending_width;
//    win->height = win->pending_height;
//    wl_egl_window_resize(win->egl_window, win->pending_width, win->pending_height, 0, 0);
//    win->pending_width = 0;
//    win->pending_height = 0;
//    win->w->notify_size();
//  }

//  if (win->is_active) {
//    win->w->activate();
//  }
//  else {
//    win->w->deactivate();
//  }

//  xdg_surface_ack_configure(xdg_surface, serial);
//}

//static const xdg_surface_listener surface_listener = {
//    surface_configure,
//};

static void
frame_configure(struct libdecor_frame *frame,
     struct libdecor_configuration *configuration,
     void *data)
{
  window_t *win = static_cast<window_t *>(data);

  int width, height;
  enum libdecor_window_state window_state;
  struct libdecor_state *state;

  if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height)) {
    width = win->width;
    height = win->height;
  }

  GHOST_PRINT("frame conf: " << width << ", " << height << std::endl);

  win->width = width;
  win->height = height;

  wl_egl_window_resize(win->egl_window, win->width, win->height, 0, 0);
  win->w->notify_size();

  if (!libdecor_configuration_get_window_state(configuration, &window_state))
    window_state = LIBDECOR_WINDOW_STATE_NONE;

  win->is_maximised = window_state & LIBDECOR_WINDOW_STATE_MAXIMIZED;
  win->is_fullscreen = window_state & LIBDECOR_WINDOW_STATE_FULLSCREEN;
  win->is_active = window_state & LIBDECOR_WINDOW_STATE_ACTIVE;

  GHOST_PRINT("frame state M/F/A: " << win->is_maximised << "/" << win->is_fullscreen << "/" << win->is_active << std::endl);

  win->is_active ? win->w->activate() : win->w->deactivate();

  state = libdecor_state_new(width, height);
  libdecor_frame_commit(frame, state, configuration);
  libdecor_state_free(state);

//  wl_surface_attach(window->wl_surface, buffer->wl_buffer, 0, 0);
//  wl_surface_damage(win->surface, 0, 0, width, height);
  wl_surface_commit(win->surface);
}

static void
frame_close(struct libdecor_frame */*frame*/, void *data)
{
  static_cast<window_t *>(data)->w->close();
}

static void
frame_commit(void *data)
{
  wl_surface_commit(static_cast<window_t *>(data)->surface);
}

static struct libdecor_frame_interface libdecor_frame_iface = {
  frame_configure,
  frame_close,
  frame_commit,
};

static void
handle_error(struct libdecor */*context*/, enum libdecor_error error, const char *message)
{
  GHOST_PRINT("decoration error ("<<  error <<"): " << message << std::endl);
  exit(EXIT_FAILURE);
}

static struct libdecor_interface libdecor_iface = {
  .error = handle_error,
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Ghost Implementation
 *
 * Wayland specific implementation of the GHOST_Window interface.
 * \{ */

GHOST_TSuccess GHOST_WindowWayland::hasCursorShape(GHOST_TStandardCursor cursorShape)
{
  return m_system->hasCursorShape(cursorShape);
}

GHOST_WindowWayland::GHOST_WindowWayland(GHOST_SystemWayland *system,
                                         const char *title,
                                         GHOST_TInt32 /*left*/,
                                         GHOST_TInt32 /*top*/,
                                         GHOST_TUns32 width,
                                         GHOST_TUns32 height,
                                         GHOST_TWindowState state,
                                         const GHOST_IWindow *parentWindow,
                                         GHOST_TDrawingContextType type,
                                         const bool stereoVisual,
                                         const bool exclusive)
    : GHOST_Window(width, height, state, stereoVisual, exclusive),
      m_system(system),
      w(new window_t)
{
  w->w = this;

  w->width = int32_t(width);
  w->height = int32_t(height);

  /* Window surfaces. */
  w->surface = wl_compositor_create_surface(m_system->compositor());
  w->egl_window = wl_egl_window_create(w->surface, int(width), int(height));

  wl_surface_set_user_data(w->surface, this);

  /* create window decorations */
  struct libdecor *context = libdecor_new(m_system->display(), &libdecor_iface);

  w->frame = libdecor_decorate(context, w->surface, &libdecor_frame_iface, w);
  libdecor_frame_map(w->frame);

  if (parentWindow) {
    libdecor_frame_set_parent_frame(w->frame, dynamic_cast<const GHOST_WindowWayland *>(parentWindow)->w->frame);
  }

  /* Call top-level callbacks. */
  wl_surface_commit(w->surface);
  wl_display_roundtrip(m_system->display());
  wl_display_roundtrip(m_system->display());

  setState(state);

  setTitle(title);

  /* EGL context. */
  if (setDrawingContextType(type) == GHOST_kFailure) {
    GHOST_PRINT("Failed to create EGL context" << std::endl);
  }
}

GHOST_TSuccess GHOST_WindowWayland::close()
{
  return m_system->pushEvent(
      new GHOST_Event(m_system->getMilliSeconds(), GHOST_kEventWindowClose, this));
}

GHOST_TSuccess GHOST_WindowWayland::activate()
{
  if (m_system->getWindowManager()->setActiveWindow(this) == GHOST_kFailure) {
    return GHOST_kFailure;
  }
  return m_system->pushEvent(
      new GHOST_Event(m_system->getMilliSeconds(), GHOST_kEventWindowActivate, this));
}

GHOST_TSuccess GHOST_WindowWayland::deactivate()
{
  m_system->getWindowManager()->setWindowInactive(this);
  return m_system->pushEvent(
      new GHOST_Event(m_system->getMilliSeconds(), GHOST_kEventWindowDeactivate, this));
}

GHOST_TSuccess GHOST_WindowWayland::notify_size()
{
  return m_system->pushEvent(
      new GHOST_Event(m_system->getMilliSeconds(), GHOST_kEventWindowSize, this));
}

GHOST_TSuccess GHOST_WindowWayland::setWindowCursorGrab(GHOST_TGrabCursorMode mode)
{
  return m_system->setCursorGrab(mode, w->surface);
}

GHOST_TSuccess GHOST_WindowWayland::setWindowCursorShape(GHOST_TStandardCursor shape)
{
  const GHOST_TSuccess ok = m_system->setCursorShape(shape);
  m_cursorShape = (ok == GHOST_kSuccess) ? shape : GHOST_kStandardCursorDefault;
  return ok;
}

GHOST_TSuccess GHOST_WindowWayland::setWindowCustomCursorShape(GHOST_TUns8 *bitmap,
                                                               GHOST_TUns8 *mask,
                                                               int sizex,
                                                               int sizey,
                                                               int hotX,
                                                               int hotY,
                                                               bool canInvertColor)
{
  return m_system->setCustomCursorShape(bitmap, mask, sizex, sizey, hotX, hotY, canInvertColor);
}

void GHOST_WindowWayland::setTitle(const char *title)
{
  libdecor_frame_set_app_id(w->frame, title);
  libdecor_frame_set_title(w->frame, title);
  this->title = title;
}

std::string GHOST_WindowWayland::getTitle() const
{
  return this->title.empty() ? "untitled" : this->title;
}

void GHOST_WindowWayland::getWindowBounds(GHOST_Rect &bounds) const
{
  getClientBounds(bounds);
}

void GHOST_WindowWayland::getClientBounds(GHOST_Rect &bounds) const
{
  bounds.set(0, 0, w->width, w->height);
}

GHOST_TSuccess GHOST_WindowWayland::setClientWidth(GHOST_TUns32 width)
{
  return setClientSize(width, GHOST_TUns32(w->height));
}

GHOST_TSuccess GHOST_WindowWayland::setClientHeight(GHOST_TUns32 height)
{
  return setClientSize(GHOST_TUns32(w->width), height);
}

GHOST_TSuccess GHOST_WindowWayland::setClientSize(GHOST_TUns32 width, GHOST_TUns32 height)
{
  wl_egl_window_resize(w->egl_window, int(width), int(height), 0, 0);
  return GHOST_kSuccess;
}

void GHOST_WindowWayland::screenToClient(GHOST_TInt32 inX,
                                         GHOST_TInt32 inY,
                                         GHOST_TInt32 &outX,
                                         GHOST_TInt32 &outY) const
{
  outX = inX;
  outY = inY;
}

void GHOST_WindowWayland::clientToScreen(GHOST_TInt32 inX,
                                         GHOST_TInt32 inY,
                                         GHOST_TInt32 &outX,
                                         GHOST_TInt32 &outY) const
{
  outX = inX;
  outY = inY;
}

GHOST_WindowWayland::~GHOST_WindowWayland()
{
  releaseNativeHandles();

  libdecor_frame_unref(w->frame);

  wl_egl_window_destroy(w->egl_window);
//  xdg_toplevel_destroy(w->xdg_toplevel);
//  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);

  delete w;
}

GHOST_TSuccess GHOST_WindowWayland::setWindowCursorVisibility(bool visible)
{
  return m_system->setCursorVisibility(visible);
}

GHOST_TSuccess GHOST_WindowWayland::setState(GHOST_TWindowState state)
{
  switch (state) {
    case GHOST_kWindowStateNormal:
      /* Unset states. */
      switch (getState()) {
        case GHOST_kWindowStateMaximized:
          libdecor_frame_unset_maximized(w->frame);
          break;
        case GHOST_kWindowStateFullScreen:
          libdecor_frame_unset_fullscreen(w->frame);
          break;
        default:
          break;
      }
      break;
    case GHOST_kWindowStateMaximized:
      libdecor_frame_set_maximized(w->frame);
      break;
    case GHOST_kWindowStateMinimized:
      libdecor_frame_set_minimized(w->frame);
      break;
    case GHOST_kWindowStateFullScreen:
      libdecor_frame_set_fullscreen(w->frame, nullptr);
      break;
    case GHOST_kWindowStateEmbedded:
      return GHOST_kFailure;
  }
  return GHOST_kSuccess;
}

GHOST_TWindowState GHOST_WindowWayland::getState() const
{
  if (w->is_fullscreen) {
    return GHOST_kWindowStateFullScreen;
  }
  else if (w->is_maximised) {
    return GHOST_kWindowStateMaximized;
  }
  else {
    return GHOST_kWindowStateNormal;
  }
}

GHOST_TSuccess GHOST_WindowWayland::invalidate()
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowWayland::setOrder(GHOST_TWindowOrder /*order*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowWayland::beginFullScreen() const
{
  libdecor_frame_set_fullscreen(w->frame, nullptr);
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowWayland::endFullScreen() const
{
  libdecor_frame_unset_fullscreen(w->frame);
  return GHOST_kSuccess;
}

/**
 * \param type  The type of rendering context create.
 * \return Indication of success.
 */
GHOST_Context *GHOST_WindowWayland::newDrawingContext(GHOST_TDrawingContextType type)
{
  GHOST_Context *context;
  switch (type) {
    case GHOST_kDrawingContextTypeNone:
      context = new GHOST_ContextNone(m_wantStereoVisual);
      break;
    case GHOST_kDrawingContextTypeOpenGL:
      context = new GHOST_ContextEGL(m_wantStereoVisual,
                                     EGLNativeWindowType(w->egl_window),
                                     EGLNativeDisplayType(m_system->display()),
                                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                     3,
                                     3,
                                     GHOST_OPENGL_EGL_CONTEXT_FLAGS,
                                     GHOST_OPENGL_EGL_RESET_NOTIFICATION_STRATEGY,
                                     EGL_OPENGL_API);
      break;
  }

  return (context->initializeDrawingContext() == GHOST_kSuccess) ? context : nullptr;
}

/** \} */
