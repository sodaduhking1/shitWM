#include <X11/cursorfont.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef DEFAULT_WALLPAPER
#define DEFAULT_WALLPAPER "wallpaper.png"
#endif

#define NUM_WORKSPACES 9

struct Client {
    Window win;
    int workspace;
    int x, y, width, height; 
    struct Client *next;
};

struct Client *head = NULL;
int current_workspace = 0;
Display *display;
Window root;

void launch_kitty(const char *display_name) {
    if (fork() == 0) {
        setenv("DISPLAY", display_name, 1); 
        execl("/usr/bin/kitty", "kitty", NULL);
        exit(1);
    }
}

void launch_rofi(const char *display_name) {
    if (fork() == 0) {
        setenv("DISPLAY", display_name, 1); 
        execl("/usr/bin/rofi", "rofi", "-show", "drun", NULL);
        exit(1);
    }
}

void launch_polybar(const char *display_name) {
    if (fork() == 0) {
        setenv("DISPLAY", display_name, 1); 
        execl("/usr/bin/polybar", "polybar", "example", NULL);
        exit(1);
    }
}

void launch_flameshot(const char *display_name) {
    if (fork() == 0) {
        setenv("DISPLAY", display_name, 1); 
        execl("/usr/bin/flameshot", "flameshot", "gui", NULL);
        exit(1);
    }
}

int is_dock(Window w) {
    Atom prop_type, actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop_data = NULL;
    int result = 0;
    Atom net_wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", True);
    Atom net_wm_window_type_dock = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", True);
    if (net_wm_window_type == None || net_wm_window_type_dock == None) return 0;
    if (XGetWindowProperty(display, w, net_wm_window_type, 0, sizeof(Atom), False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop_data) == Success && prop_data) {
        Atom *atoms = (Atom *)prop_data;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == net_wm_window_type_dock) {
                result = 1;
                break;
            }
        }
        XFree(prop_data);
    }
    return result;
}



void view_workspace(int ws) {
    current_workspace = ws;
    struct Client *curr = head;
    Window active_win = None;

    while (curr != NULL) {
        if (curr->workspace == current_workspace) {
            XMoveResizeWindow(display, curr->win, curr->x, curr->y, curr->width, curr->height);
            XMapWindow(display, curr->win);
            active_win = curr->win;
        } else {
            XMoveWindow(display, curr->win, -4000, -4000);
        }
        curr = curr->next;
    }
    
    if (active_win != None) {
        XSetInputFocus(display, active_win, RevertToParent, CurrentTime);
    } else {
        XSetInputFocus(display, root, RevertToParent, CurrentTime);
    }
    XFlush(display);
}

void add_client(Window w, int x, int y, int width, int height) {
    struct Client *curr = head;
    while (curr != NULL) {
        if (curr->win == w) return;
        curr = curr->next;
    }
    struct Client *c = malloc(sizeof(struct Client));
    c->win = w;
    c->workspace = current_workspace;
    c->x = x; c->y = y; c->width = width; c->height = height;
    c->next = head;
    head = c;
}

void remove_client(Window w) {
    struct Client *curr = head, *prev = NULL;
    while (curr != NULL) {
        if (curr->win == w) {
            if (prev == NULL) head = curr->next;
            else prev->next = curr->next;
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

struct Client* find_client(Window w) {
    struct Client *curr = head;
    while (curr != NULL) {
        if (curr->win == w) return curr;
        curr = curr->next;
    }
    return NULL;
}

void set_image_wallpaper(Display *dpy, Window rt, const char *filename) {
    int width, height, channels;
    unsigned char *img_data = stbi_load(filename, &width, &height, &channels, 4);
    if (!img_data) {
        XSetWindowBackground(dpy, rt, 0x4A5568);
        XClearWindow(dpy, rt);
        return;
    }
    int screen = DefaultScreen(dpy);
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);
    Visual *visual = DefaultVisual(dpy, screen);
    int depth = DefaultDepth(dpy, screen);

    Pixmap pixmap = XCreatePixmap(dpy, rt, sw, sh, depth);
    GC gc = XCreateGC(dpy, pixmap, 0, NULL);
    char *ximage_data = malloc(sw * sh * 4);
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int img_x = (x * width) / sw;
            int img_y = (y * height) / sh;
            if (img_x >= width) img_x = width - 1;
            if (img_y >= height) img_y = height - 1;
            int img_idx = (img_y * width + img_x) * 4;
            int x_idx = (y * sw + x) * 4;
            ximage_data[x_idx + 0] = img_data[img_idx + 2];
            ximage_data[x_idx + 1] = img_data[img_idx + 1];
            ximage_data[x_idx + 2] = img_data[img_idx + 0];
            ximage_data[x_idx + 3] = 0;
        }
    }
    XImage *ximage = XCreateImage(dpy, visual, depth, ZPixmap, 0, ximage_data, sw, sh, 32, 0);
    XPutImage(dpy, pixmap, gc, ximage, 0, 0, 0, 0, sw, sh);
    XSetWindowBackgroundPixmap(dpy, rt, pixmap);
    XClearWindow(dpy, rt);
    XDestroyImage(ximage);
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, pixmap);
    stbi_image_free(img_data);
    XFlush(dpy);
}

Window find_top_level(Window w) {
    Window root_return, parent_return, *children_return;
    unsigned int nchildren_return;
    while (w != root && XQueryTree(display, w, &root_return, &parent_return, &children_return, &nchildren_return)) {
        if (children_return) XFree(children_return);
        if (parent_return == root || parent_return == None) return w;
        w = parent_return;
    }
    return w;
}







int main(void) {
    char *display_name = getenv("DISPLAY");
    if (!display_name) display_name = ":0";

    display = XOpenDisplay(NULL);
    if (!display) return 1;

    root = DefaultRootWindow(display);

    Atom net_supported = XInternAtom(display, "_NET_SUPPORTED", False);
    Atom net_active_win = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    XChangeProperty(display, root, net_supported, XA_ATOM, 32, PropModeReplace, (unsigned char *)&net_active_win, 1);


    int screen = DefaultScreen(display);
    int sw = DisplayWidth(display, screen);
    int sh = DisplayHeight(display, screen);

    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);
    set_image_wallpaper(display, root, DEFAULT_WALLPAPER);

    
    Cursor cursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, root, cursor);
    XFreeCursor(display, cursor);

    KeyCode kitty_keycode = XKeysymToKeycode(display, XK_Return);
    XGrabKey(display, kitty_keycode, Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);

    KeyCode rofi_keycode = XKeysymToKeycode(display, XK_space);
    XGrabKey(display, rofi_keycode, Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);

    KeyCode close_keycode = XKeysymToKeycode(display, XK_Q);
    XGrabKey(display, close_keycode, Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);

    KeyCode exit_keycode = XKeysymToKeycode(display, XK_Escape);
    XGrabKey(display, exit_keycode, Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);

    // Bind Print Screen key to trigger Flameshot
    KeyCode print_keycode = XKeysymToKeycode(display, XK_Print);
    XGrabKey(display, print_keycode, 0, root, True, GrabModeAsync, GrabModeAsync);

    KeySym num_syms[NUM_WORKSPACES] = {XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9};
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        XGrabKey(display, XKeysymToKeycode(display, num_syms[i]), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    }

    XGrabButton(display, Button1, Mod4Mask, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(display, Button3, Mod4Mask, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);

    XEvent ev;
    XWindowAttributes start_attr;
    XButtonEvent start_mouse;
    start_mouse.subwindow = None;

    launch_polybar(display_name);

    while (1) {
        XNextEvent(display, &ev);

        if (ev.type == MapRequest) {
            if (is_dock(ev.xmaprequest.window)) {
                XMapWindow(display, ev.xmaprequest.window);
                XFlush(display);
                continue;
            }

            XWindowAttributes attr;
            XGetWindowAttributes(display, ev.xmaprequest.window, &attr);
            
            long dummy;
            XSizeHints hints;
            XGetWMNormalHints(display, ev.xmaprequest.window, &hints, &dummy);

            int target_x = attr.x;
            int target_y = attr.y;
            int target_w = attr.width;
            int target_h = attr.height;

            if (hints.flags & PSize) {
                target_w = hints.width;
                target_h = hints.height;
            }
            if (hints.flags & PPosition) {
                target_x = hints.x;
                target_y = hints.y;
            }

            if (target_w < 100 || target_h < 100) {
                target_x = 100; target_y = 100;
                target_w = sw - 200; target_h = sh - 200;
            }

            add_client(ev.xmaprequest.window, target_x, target_y, target_w, target_h);
            
            XSelectInput(display, ev.xmaprequest.window, FocusChangeMask | PropertyChangeMask | StructureNotifyMask);
            XMoveResizeWindow(display, ev.xmaprequest.window, target_x, target_y, target_w, target_h);
            XMapWindow(display, ev.xmaprequest.window);
            XSetInputFocus(display, ev.xmaprequest.window, RevertToParent, CurrentTime);
            XChangeProperty(display, root, net_active_win, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&(ev.xmaprequest.window), 1);
            XFlush(display);
        }

        if (ev.type == ConfigureRequest) {
            XConfigureRequestEvent *cre = &ev.xconfigurerequest;
            XWindowChanges wc;
            wc.x = cre->x; wc.y = cre->y;
            wc.width = cre->width; wc.height = cre->height;
            wc.border_width = cre->border_width;
            wc.sibling = cre->above;
            wc.stack_mode = cre->detail;
            XConfigureWindow(display, cre->window, cre->value_mask, &wc);
            
            struct Client *c = find_client(cre->window);
            if (c) {
                if (cre->value_mask & CWX) c->x = cre->x;
                if (cre->value_mask & CWY) c->y = cre->y;
                if (cre->value_mask & CWWidth) c->width = cre->width;
                if (cre->value_mask & CWHeight) c->height = cre->height;
            }
            XFlush(display);
        }

        if (ev.type == UnmapNotify || ev.type == DestroyNotify) {
            remove_client(ev.xany.window);
        }

        if (ev.type == KeyPress) {
            if (ev.xkey.keycode == kitty_keycode && (ev.xkey.state & Mod4Mask)) {
                launch_kitty(display_name);
            }
            if (ev.xkey.keycode == rofi_keycode && (ev.xkey.state & Mod4Mask)) {
                launch_rofi(display_name);
            }
            if (ev.xkey.keycode == close_keycode && (ev.xkey.state & Mod4Mask)) {
                Window focused;
                int revert_to;
                XGetInputFocus(display, &focused, &revert_to);
                if (focused != None && focused != root) {
                    Window top = find_top_level(focused);
                    XKillClient(display, top);
                }
            }
            if (ev.xkey.keycode == exit_keycode && (ev.xkey.state & Mod4Mask)) {
                break; 
            }
            if (ev.xkey.keycode == print_keycode) {
                launch_flameshot(display_name);
            }
            for (int i = 0; i < NUM_WORKSPACES; i++) {
                if (ev.xkey.keycode == XKeysymToKeycode(display, num_syms[i]) && (ev.xkey.state & Mod4Mask)) {
                    view_workspace(i);
                }
            }
        }

        if (ev.type == ButtonPress && ev.xbutton.subwindow != None) {
            XGetWindowAttributes(display, ev.xbutton.subwindow, &start_attr);
            start_mouse = ev.xbutton;
            XRaiseWindow(display, ev.xbutton.subwindow);
            XSetInputFocus(display, ev.xbutton.subwindow, RevertToParent, CurrentTime);
        }

        if (ev.type == MotionNotify && start_mouse.subwindow != None) {
            int x_diff = ev.xbutton.x_root - start_mouse.x_root;
            int y_diff = ev.xbutton.y_root - start_mouse.y_root;
            
            struct Client *c = find_client(start_mouse.subwindow);
            if (start_mouse.button == Button1) {
                int nx = start_attr.x + x_diff;
                int ny = start_attr.y + y_diff;
                XMoveWindow(display, start_mouse.subwindow, nx, ny);
                if (c) { c->x = nx; c->y = ny; }
            } else if (start_mouse.button == Button3) {
                int nw = start_attr.width + x_diff > 10 ? start_attr.width + x_diff : 10;
                int nh = start_attr.height + y_diff > 10 ? start_attr.height + y_diff : 10;
                XMoveResizeWindow(display, start_mouse.subwindow, start_attr.x, start_attr.y, nw, nh);
                if (c) { c->width = nw; c->height = nh; }
            }
        }

        if (ev.type == ButtonRelease) {
            start_mouse.subwindow = None;
        }
    }

    XCloseDisplay(display);
    return 0;
}
