// Modified from a tool by Pertti Kellomäki <pertti.kellomaki@nokia.com>

#include <iostream>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdlib.h>

using namespace std;

// Length of all command line arguments plus terminating nulls
int argsLength(int argc, char *argv[]) {
    int len = 0;
    for(int i = 1; i < argc; i++) {
	len += strlen(argv[i]);
	len += 1;
    }
    return len;
}

int main(int argc, char *argv[]) {
    if(argc == 1) {
	cerr << "Usage: " << argv[0] << "arg1 arg2 ..." << endl
	     << "       Sets property _MEEGO_SPLASH_SCREEN in the mcompositor window" << endl
	     << "       to the list of strings arg1, arg2, ..." << endl
	     << "       This fakes the invoker requesting a splash screen." << endl;
	exit(0);
    }
	
    Display * dpy = XOpenDisplay(NULL);
    const char *compositorWindowIdProperty =  "_NET_SUPPORTING_WM_CHECK";
    Atom compositorWindowIdAtom = XInternAtom(dpy, compositorWindowIdProperty, False);
    Atom           type;
    int            format;
    unsigned long  nItems;
    unsigned long  bytesAfter;
    unsigned char *prop = 0;

    Window rootWin = XDefaultRootWindow(dpy);

    // Get the compositor window id
    int retval = XGetWindowProperty(dpy, rootWin, compositorWindowIdAtom,
				    0, 0x7fffffff, False, XA_WINDOW,
				    &type, &format, &nItems, &bytesAfter, &prop);
    if(retval == Success) 
    {
	Window compositorWindow = *reinterpret_cast<Window *>(prop);

	const char* splashProperty =  "_MEEGO_SPLASH_SCREEN";
	Atom splashPropertyAtom = XInternAtom(dpy, splashProperty, False);

	// Package up command line arguments into one string
	int argLen = argsLength(argc, argv);
	char *data = new char[argLen];
	char *d = data;

	for(int i = 1; i < argc; i++) {
	    strcpy(d, argv[i]);
	    d += strlen(argv[i]);
	    d += 1;
	}

	XChangeProperty(dpy, compositorWindow, splashPropertyAtom, XA_STRING,
			8, PropModeReplace, (unsigned char *)data,
			argLen);
    } else {
      cerr << "Did not find mcompositor window" << endl;
    }
    XCloseDisplay(dpy);
}
