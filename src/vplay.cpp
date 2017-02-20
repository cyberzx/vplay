#include  "vulkan_api.h"
#include  "vulkantools.h"
#include  "v3d.h"

#include  <stdexcept>
#include  <memory>
#include  <X11/Xutil.h>
#include  <xcb/xcb.h>

#include  <stdlib.h>

// window system
Display* display;
Window xlib_window;
Atom xlib_wm_delete_window;

xcb_connection_t *connection;
xcb_screen_t *screen;
xcb_window_t window;
xcb_intern_atom_reply_t *atom_wm_delete_window;

VkSurfaceKHR  xcb_surface;

int win_width = 800;
int win_height = 600;
static bool quit = false;
bool  need_resize = false;

void create_window()
{
  int scr;
  connection = xcb_connect(nullptr, &scr);
  if (xcb_connection_has_error(connection) > 0)
    throw std::runtime_error("cannot find a compatable vulkan installable client driver (ICD)");

  const xcb_setup_t  *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t   iter = xcb_setup_roots_iterator(setup);

  while (scr-- > 0)
    xcb_screen_next(&iter);
  screen = iter.data;

  // create window
  window = xcb_generate_id(connection);
  uint32_t value_mask, value_list[32];

  value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  value_list[0] = screen->black_pixel;
  value_list[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE |
                  XCB_EVENT_MASK_STRUCTURE_NOTIFY;

  xcb_create_window(connection, XCB_COPY_FROM_PARENT, window,
                    screen->root, 0, 0, win_width, win_height, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                    value_mask, value_list);

  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
  xcb_intern_atom_reply_t *reply =
      xcb_intern_atom_reply(connection, cookie, 0);

  xcb_intern_atom_cookie_t cookie2 =
      xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
  atom_wm_delete_window =
      xcb_intern_atom_reply(connection, cookie2, 0);

  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
                      (*reply).atom, 4, 32, 1,
                      &(*atom_wm_delete_window).atom);
  free(reply);
  xcb_map_window(connection, window);

  xcb_surface = v3d::get_vk().createXcbSurfaceKHR(vk::XcbSurfaceCreateInfoKHR()
                                                    .setWindow(window)
                                                    .setConnection(connection));
}

static void handle_window_event(const xcb_generic_event_t* event)
{
  uint8_t event_code = event->response_type & 0x7f;
  switch (event_code) {
  case XCB_CLIENT_MESSAGE:
      if ((*(xcb_client_message_event_t *)event).data.data32[0] ==
          (*atom_wm_delete_window).atom) {
          quit = true;
      }
      break;
  case XCB_KEY_RELEASE: {
      const xcb_key_release_event_t *key =
          (const xcb_key_release_event_t *)event;

      switch (key->detail) {
      case 0x9: // Escape
          quit = true;
          break;
      }
  } break;
  case XCB_CONFIGURE_NOTIFY: {
      printf("configure\n");
      const xcb_configure_notify_event_t *cfg =
          (const xcb_configure_notify_event_t *)event;
        if ((win_width != cfg->width) || (win_height != cfg->height)) {
            win_width = cfg->width;
            win_height = cfg->height;
            need_resize = true;
        }
      }
   break;
  default:
      break;
  }
}

static void do_resize()
{
  need_resize = false;
  v3d::on_window_resize(xcb_surface);
}

static void mainloop()
{
  xcb_flush(connection);

  while (!quit)
  {
    xcb_generic_event_t*  event;

    event = xcb_poll_for_event(connection);
    while (event)
    {
      handle_window_event(event);
      free(event);
      event = xcb_poll_for_event(connection);
    }

    if (need_resize)
      do_resize();

    v3d::render();
  }
}

int main()
{
  try {
    v3d::init("vplay", "fa20");
    create_window();
    v3d::on_window_create(xcb_surface);

    mainloop();
  }
  catch (std::exception const& e)
  {
    printf("%s\n", e.what());
  }
  v3d::get_device().waitIdle();
  v3d::free_resources();

  if (xcb_surface)
    v3d::get_vk().destroySurfaceKHR(xcb_surface);
  if (window)
    xcb_destroy_window(connection, window);
  if (connection)
    xcb_disconnect(connection);
  free(atom_wm_delete_window);

  v3d::shutdown();
  return 0;
}
