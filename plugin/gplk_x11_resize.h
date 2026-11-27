/** X11 resize wakeups for native Giza, including older blocking cursor loops.
 * All plotting and Giza calls stay on the main thread. A separate X connection
 * observes only windows opened by this module and wakes idle input with a
 * reserved Ctrl+Space event. It never sends restart/refit keys. GPLv3-or-later.
 */
#ifndef TEMPO2_GPLK_X11_RESIZE_H
#define TEMPO2_GPLK_X11_RESIZE_H

#ifdef HAVE_GPLK_X11_RESIZE
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <ctime>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <algorithm>

class GplkX11Resize {
    struct Watched {
        int device,width,height,marginX,marginY;
        Window window;
        unsigned long generation,handled;
        long long lastWake;
        bool waiting;
        Watched():device(0),width(0),height(0),marginX(0),marginY(0),window(0),
                  generation(0),handled(0),lastWake(0),waiting(false) {}
    };
    Display *display_;
    pthread_mutex_t mutex_;
    pthread_t thread_;
    bool running_,stopping_,threadsReady_;
    unsigned long sequence_;
    XErrorHandler previousError_;
    std::vector<Watched> windows_;
    GplkX11Resize(const GplkX11Resize &);
    GplkX11Resize &operator=(const GplkX11Resize &);

    static long long milliseconds() {
        timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        return static_cast<long long>(now.tv_sec)*1000+now.tv_nsec/1000000;
    }
    static int errorHandler(Display *display,XErrorEvent *error) {
        GplkX11Resize &self=instance();
        // A window can disappear between a geometry query and event delivery.
        // Never change error handling for Giza's own display connections.
        if (display==self.display_) return 0;
        return self.previousError_ ? self.previousError_(display,error) : 0;
    }
    void installErrorHandler() {
        XErrorHandler previous=XSetErrorHandler(errorHandler);
        if (previous!=errorHandler) previousError_=previous;
    }
    Window findWindow(Window parent,const char *title,int depth=0) {
        char *name=0;
        if (XFetchName(display_,parent,&name) && name) {
            const bool match=std::strcmp(name,title)==0; XFree(name);
            if (match) return parent;
        }
        if (depth>=8) return 0;
        Window root,parentWindow,*children=0; unsigned int n=0;
        if (!XQueryTree(display_,parent,&root,&parentWindow,&children,&n)) return 0;
        Window found=0;
        for (unsigned int i=0;i<n && !found;++i) found=findWindow(children[i],title,depth+1);
        if (children) XFree(children);
        return found;
    }
    bool geometry(Window window,int &width,int &height) {
        Window root; int x,y; unsigned int w,h,border,depth;
        if (!XGetGeometry(display_,window,&root,&x,&y,&w,&h,&border,&depth)) return false;
        width=w; height=h; return true;
    }
    static void *run(void *arg) { static_cast<GplkX11Resize *>(arg)->observe(); return 0; }
    void observe() {
        while (true) {
            pthread_mutex_lock(&mutex_);
            if (stopping_) { pthread_mutex_unlock(&mutex_); return; }
            while (XPending(display_)) {
                XEvent event; XNextEvent(display_,&event);
                for (std::size_t i=0;i<windows_.size();++i) {
                    Watched &w=windows_[i];
                    if (event.xany.window!=w.window) continue;
                    if (event.type==ConfigureNotify && event.xconfigure.width>0 && event.xconfigure.height>0 &&
                        (w.width!=event.xconfigure.width || w.height!=event.xconfigure.height)) {
                        w.width=event.xconfigure.width; w.height=event.xconfigure.height; ++w.generation;
                    } else if (event.type==DestroyNotify) {
                        windows_.erase(windows_.begin()+i);
                    }
                    break;
                }
            }
            const long long now=milliseconds();
            for (std::size_t i=0;i<windows_.size();++i) {
                Watched &w=windows_[i];
                // Coalesce drag events to at most 12.5 redraws/second. Retry
                // until acknowledged: a wakeup arriving before giza_band starts
                // selecting key events must not leave the viewer stuck.
                if (!w.waiting || w.generation==w.handled || now-w.lastWake<80) continue;
                XEvent event; std::memset(&event,0,sizeof(event));
                event.xkey.type=KeyPress; event.xkey.display=display_;
                event.xkey.window=w.window; event.xkey.root=DefaultRootWindow(display_);
                event.xkey.same_screen=True; event.xkey.state=ControlMask;
                event.xkey.keycode=XKeysymToKeycode(display_,XK_space);
                XSendEvent(display_,w.window,False,KeyPressMask,&event);
                w.lastWake=now;
            }
            XFlush(display_);
            const int fd=ConnectionNumber(display_);
            pthread_mutex_unlock(&mutex_);
            pollfd input={fd,POLLIN,0};
            poll(&input,1,20); // bounded wait also makes shutdown/join prompt
        }
    }
public:
    GplkX11Resize():display_(0),running_(false),stopping_(false),threadsReady_(false),
                    sequence_(0),previousError_(0) { pthread_mutex_init(&mutex_,0); }
    ~GplkX11Resize() { stop(); pthread_mutex_destroy(&mutex_); }
    static GplkX11Resize &instance() { static GplkX11Resize self; return self; }

    std::string openingTitle(const char *device) {
        const char *type=std::strrchr(device,'/');
        if (!type || (std::strcmp(type,"/xw") && std::strcmp(type,"/xs"))) return "gplk";
        // Initialize Xlib threading before Giza opens its first X connection.
        if (!threadsReady_) { if (!XInitThreads()) return "gplk"; threadsReady_=true; }
        if (!display_) display_=XOpenDisplay(0);
        if (!display_) return "gplk";
        char title[96];
        std::snprintf(title,sizeof(title),"gplk-%ld-%lu",static_cast<long>(getpid()),++sequence_);
        return title;
    }
    bool attach(int device,const std::string &title,float paperWidth,float paperHeight) {
        if (!display_ || title=="gplk") return false;
        pthread_mutex_lock(&mutex_); installErrorHandler();
        // The temporary, process-unique title avoids attaching to somebody
        // else's gplk window. Restore the ordinary title immediately afterward.
        const Window window=findWindow(DefaultRootWindow(display_),title.c_str());
        Watched w; w.device=device; w.window=window;
        const bool found=window && geometry(window,w.width,w.height);
        if (found) {
            w.marginX=std::max(0,w.width-static_cast<int>(paperWidth+.5f));
            w.marginY=std::max(0,w.height-static_cast<int>(paperHeight+.5f));
            windows_.push_back(w); XSelectInput(display_,window,StructureNotifyMask);
            XStoreName(display_,window,"gplk"); XFlush(display_);
            if (!running_) {
                stopping_=false;
                if (pthread_create(&thread_,0,run,this)==0) running_=true;
                else windows_.pop_back();
            }
        }
        const bool attached=found && running_;
        pthread_mutex_unlock(&mutex_); return attached;
    }
    unsigned long windowFor(int device) {
        pthread_mutex_lock(&mutex_); Window window=0;
        for (std::size_t i=0;i<windows_.size();++i) if (windows_[i].device==device) window=windows_[i].window;
        pthread_mutex_unlock(&mutex_); return window;
    }
    void waiting(int device,bool value) {
        pthread_mutex_lock(&mutex_);
        for (std::size_t i=0;i<windows_.size();++i) if (windows_[i].device==device) windows_[i].waiting=value;
        pthread_mutex_unlock(&mutex_);
    }
    bool take(int device,float &width,float &height) {
        pthread_mutex_lock(&mutex_); bool pending=false;
        for (std::size_t i=0;i<windows_.size();++i) {
            Watched &w=windows_[i];
            if (w.device!=device || w.generation==w.handled) continue;
            w.handled=w.generation; geometry(w.window,w.width,w.height);
            width=std::max(1,w.width-w.marginX); height=std::max(1,w.height-w.marginY);
            pending=true; break;
        }
        pthread_mutex_unlock(&mutex_); return pending;
    }
    void detach(int device) {
        pthread_mutex_lock(&mutex_);
        for (std::size_t i=0;i<windows_.size();++i) if (windows_[i].device==device) {
            windows_.erase(windows_.begin()+i); break;
        }
        const bool empty=windows_.empty(); pthread_mutex_unlock(&mutex_);
        if (empty) stop();
    }
    void stop() {
        pthread_mutex_lock(&mutex_); stopping_=true;
        const bool join=running_; pthread_mutex_unlock(&mutex_);
        if (join) pthread_join(thread_,0);
        pthread_mutex_lock(&mutex_); running_=false; windows_.clear();
        if (display_) {
            XSync(display_,False);
            XErrorHandler current=XSetErrorHandler(previousError_);
            if (current!=errorHandler) XSetErrorHandler(current);
            XCloseDisplay(display_); display_=0;
        }
        pthread_mutex_unlock(&mutex_);
    }
};
#endif // HAVE_GPLK_X11_RESIZE
#endif
