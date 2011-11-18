
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/Xatom.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>


#include "application.h"

static XSyncCounter create_counter(Display* dpy, int val)
{
    XSyncCounter counter;
    XSyncValue value;
    
    XSyncIntToValue(&value, val);
    counter = XSyncCreateCounter(dpy, value);
    
    printf("%s: val:%X\n", __func__, counter);

    return counter;
}

static bool composited = false;

Application::Application(int w, int h, bool tryUsingAlpha)
    : width(w), height(h), quit(false), usingAlpha(false), tryAlpha(tryUsingAlpha), syncSwap(false)
{
    gettimeofday(&timeOfLastUpdate, 0);
}

Application::~Application()
{
}

bool Application::initWindow()
{
    bool tryArgbVisual = tryAlpha;

    x11Display = XOpenDisplay(0);
    x11Screen = XDefaultScreen(x11Display);
    
    int sync_event, sync_error = 0;
    if(!XSyncQueryExtension(x11Display, &sync_event, &sync_error)) 
        printf("error initializing sync extension\n");

    eglDisplay = eglGetDisplay((EGLNativeDisplayType)x11Display);
    eglInitialize(eglDisplay, 0, 0);

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_DEPTH_SIZE,         1,
        EGL_STENCIL_SIZE,       1,
//        EGL_SAMPLE_BUFFERS,     1,
        EGL_ALPHA_SIZE,         1,
        EGL_NONE
    };
    int alphaElement = (sizeof(configAttribs) / sizeof(EGLint)) -3;

    if (!tryAlpha)
        configAttribs[alphaElement] = EGL_NONE;

    EGLint configCount;
    eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &configCount);

    if (configCount) {
        EGLint configId;
        eglGetConfigAttrib(eglDisplay, eglConfig, EGL_CONFIG_ID, &configId);
        if (tryAlpha)
            printf("Got an EGL config (%d) with an alpha\n", configId);
        else
            printf("Got an EGL config (%d) without an alpha\n", configId);
    }

    if (configCount == 0 && tryAlpha) {
        printf("Couldn't find a config with an alpha, falling back to non-alpha\n");
        configAttribs[alphaElement] = EGL_NONE;
        eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &configCount);
        tryArgbVisual = usingAlpha = false;
    }

    if (configCount == 0) {
        printf("Couldn't find an egl config I liked\n");
        return false;
    }

    EGLint nativeVisualId = 0;
    eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &nativeVisualId);

    XVisualInfo vi;
    memset(&vi, 0, sizeof(XVisualInfo));

    if (nativeVisualId > 0) {
        vi.visualid = nativeVisualId;

        // EGL has suggested a visual id, so get the rest of the visual info for that id:
        XVisualInfo *chosenVisualInfo;
        int matchingCount = 0;
        chosenVisualInfo = XGetVisualInfo(x11Display, VisualIDMask, &vi, &matchingCount);

        if (chosenVisualInfo) {
            if (tryArgbVisual) {
                // Check to make sure the EGL provided visual is ARGB:
                XRenderPictFormat *format;
                format = XRenderFindVisualFormat(x11Display, chosenVisualInfo->visual);
                if (format->type == PictTypeDirect && format->direct.alphaMask) {
                    printf("Using ARGB X Visual ID (%d) provided by EGL\n", (int)vi.visualid);
                    vi = *chosenVisualInfo;
                    usingAlpha = true;
                }
                else {
                    printf("WARNING: EGL provided a visual to use, but it didn't have an alpha\n");
                    vi.visualid = 0;
                }
            }
            else {
                printf("Using Non-ARGB X Visual ID (%d) provided by EGL\n", (int)vi.visualid);
                vi = *chosenVisualInfo;
                usingAlpha = false;
            }
            XFree(chosenVisualInfo);
        }
        else {
            printf("EGL provided visual id (%d) is invalid\n", vi.visualid);
            vi.visualid = 0;
        }
    }

    if (vi.visualid == 0) {
        vi.screen  = x11Screen;
        vi.c_class = TrueColor;
    }

    if (vi.visualid == 0 && tryArgbVisual) {
        // Try to use XRender to find an ARGB visual we can use
        vi.depth   = 32;
        XVisualInfo *matchingVisuals;
        int matchingCount = 0;
        matchingVisuals = XGetVisualInfo(x11Display,
                                         VisualScreenMask|VisualDepthMask|VisualClassMask,
                                         &vi, &matchingCount);

        for (int i = 0; i < matchingCount; ++i) {
            XRenderPictFormat *format;
            format = XRenderFindVisualFormat(x11Display, matchingVisuals[i].visual);
            if (format->type == PictTypeDirect && format->direct.alphaMask) {
                vi = matchingVisuals[i];
                printf("Using X Visual ID (%d) for ARGB visual as provided by XRender\n", (int)vi.visualid);
                usingAlpha = true;
                break;
            }
        }

        XFree(matchingVisuals);
    }

    if (vi.visualid == 0) {
        usingAlpha = false;
        XVisualInfo *matchingVisuals;
        int matchingCount = 0;
        matchingVisuals = XGetVisualInfo(x11Display,
                                         VisualScreenMask|VisualClassMask,
                                         &vi, &matchingCount);

        if (matchingVisuals) {
            vi = matchingVisuals[0];
            XFree(matchingVisuals);
        }

    }

    if (vi.visualid == 0) {
        printf("Failed to get a valid visual :-(\n");
        return false;
    }

    if (tryAlpha && !usingAlpha)
        printf("WARNING: ARGB window requested but could only get an RGB window!\n");

//    printf("Visual Info:");
//    printf("   bits_per_rgb=%d\n", vi.bits_per_rgb);
//    printf("   red_mask=0x%x\n", vi.red_mask);
//    printf("   green_mask=0x%x\n", vi.green_mask);
//    printf("   blue_mask=0x%x\n", vi.blue_mask);
//    printf("   colormap_size=%d\n", vi.colormap_size);
//    printf("   c_class=%d\n", vi.c_class);
//    printf("   depth=%d\n", vi.depth);
//    printf("   screen=%d\n", vi.screen);
//    printf("   visualid=%d\n", vi.visualid);

    Window rootWindow = RootWindow(x11Display, x11Screen);

    XSetWindowAttributes windowAttribs;
    memset(&windowAttribs, 0, sizeof(XSetWindowAttributes));

    unsigned int valueMask = CWEventMask|CWBackPixel|CWBorderPixel;

    windowAttribs.event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask;
    windowAttribs.background_pixel = 0;
    windowAttribs.border_pixel = 0;

    if (tryArgbVisual) {
        windowAttribs.colormap = XCreateColormap( x11Display, rootWindow, vi.visual, AllocNone );
        valueMask |= CWColormap;
    }

    x11Window = XCreateWindow(x11Display, rootWindow, 0, 0, width, height,
                             0, vi.depth, InputOutput, vi.visual, valueMask, &windowAttribs);
    Atom a = XInternAtom(x11Display,"_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", False);
    XChangeProperty(x11Display, x11Window, XInternAtom(x11Display, "_NET_WM_WINDOW_TYPE",
                                                       False), XA_ATOM, 32,
                    PropModeReplace,(unsigned char *) &a, 1);
    if (syncSwap) {
        
        swap_counter = create_counter(x11Display, COMPOSITE_PIXMAP_CONSUMED);
        XChangeProperty(x11Display, x11Window,
                        XInternAtom(x11Display, "_MEEGOTOUCH_WM_SWAP_COUNTER", False),
                        XA_CARDINAL,
                        32, PropModeReplace,
                        (unsigned char *) &swap_counter, 1);
        
        XSyncIntToValue(&ready, COMPOSITE_PIXMAP_READY);
        XSyncIntToValue(&consumed, COMPOSITE_PIXMAP_CONSUMED);
        XSyncIntToValue(&explicit_reset, EXPLICIT_RESET);

        // CompositeSwap counter wait condition 1
        wc.trigger.counter = swap_counter;
        wc.trigger.value_type = XSyncAbsolute;
        wc.trigger.test_type = XSyncPositiveComparison;
        XSyncIntToValue(&wc.event_threshold, 0);
        // (wait for CompositeDrawableConsumed)
        XSyncIntToValue(&wc.trigger.wait_value, COMPOSITE_PIXMAP_CONSUMED);
    }

    XMapWindow(x11Display, x11Window);
    XFlush(x11Display);

    eglWindowSurface = eglCreateWindowSurface(eglDisplay, eglConfig, (EGLNativeWindowType)x11Window, NULL);


    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    eglContext = eglCreateContext(eglDisplay, eglConfig, 0, contextAttribs);

    if (eglWindowSurface == EGL_NO_SURFACE || eglContext == EGL_NO_CONTEXT) {
        printf("Failed to create EGL surface/ctx\n");
        return false;
    }

    eglMakeCurrent(eglDisplay, eglWindowSurface, eglWindowSurface, eglContext);

    initGl();

    return true;
}




void dumpShaderLog(GLint shaderId)
{
    GLint logLength;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);

    char* log = new char[logLength];
    glGetShaderInfoLog(shaderId, logLength, 0, log);
    printf("%s\n", log);
    delete [] log;
}

void dumpProgramLog(GLint programId)
{
    GLint logLength;
    glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLength);

    char* log = new char[logLength];
    glGetProgramInfoLog(programId, logLength, 0, log);
    printf("%s\n", log);
    delete [] log;
}

bool Application::initTextureShader()
{
    GLint success = 0;

    const char* vertexShaderSrc = "\
        attribute highp vec4    vertexCoordArray;\
        attribute lowp  vec2    textureCoordArray; \
        varying   lowp  vec2    textureCoords; \
        void main(void) \
        {\
                gl_Position = vertexCoordArray;\
                textureCoords = textureCoordArray; \
        }";

    GLint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShaderId, 1, &vertexShaderSrc, 0);
    glCompileShader(vertexShaderId);
    glGetShaderiv(vertexShaderId, GL_COMPILE_STATUS, &success);
    if (!success) {
        printf("Vertex shader failed to compile:\n");
        dumpShaderLog(vertexShaderId);
        return false;
    }

    const char* fragmentShaderSrc = " \
            uniform         sampler2D   texture; \
            varying mediump vec2        textureCoords; \
            void main (void) { \
                gl_FragColor = texture2D(texture, textureCoords); \
            }";

    GLint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShaderId, 1, &fragmentShaderSrc, 0);
    glCompileShader(fragmentShaderId);
    glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &success);
    if (!success) {
        printf("Fragment shader failed to compile:\n");
        dumpShaderLog(fragmentShaderId);
        return false;
    }

    textureDrawingProgId = glCreateProgram();
    glAttachShader(textureDrawingProgId, fragmentShaderId);
    glAttachShader(textureDrawingProgId, vertexShaderId);
    glBindAttribLocation(textureDrawingProgId, VERTEX_COORD_ARRAY, "vertexCoordArray");
    glBindAttribLocation(textureDrawingProgId, TEXTURE_COORD_ARRAY, "textureCoordArray");
    glLinkProgram(textureDrawingProgId);
    glGetProgramiv(textureDrawingProgId, GL_LINK_STATUS, &success);
    if (!success) {
        printf("Shader program failed to link:\n");
        dumpProgramLog(textureDrawingProgId);
        return false;
    }

    return true;
}


void Application::drawTexture(const Rect& destRect, Texture *texture, const Rect& srcRect)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture->id());
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


    glUseProgram(textureDrawingProgId);
    glUniform1i(glGetUniformLocation(textureDrawingProgId, "texture"), 0);

    GLfloat top, bottom, left, right;

    top = ((height - destRect.y) / GLfloat(height/2)) - 1.0;
    bottom = ((height - (destRect.y + destRect.height)) / GLfloat(height/2)) - 1.0;
    left = (destRect.x / GLfloat(width/2)) - 1.0;
    right = ((destRect.x + destRect.width) / GLfloat(width/2)) - 1.0;
    GLfloat vertexCoords[] = {
        left, top,
        right, top,
        right, bottom,
        left, bottom
    };

    glEnableVertexAttribArray(VERTEX_COORD_ARRAY);
    glVertexAttribPointer(VERTEX_COORD_ARRAY, 2, GL_FLOAT, GL_FALSE, 0, vertexCoords);

    top = srcRect.y / GLfloat(texture->height());
    bottom = (srcRect.y + srcRect.height) / GLfloat(texture->height());
    left = srcRect.x / GLfloat(texture->width());
    right = (srcRect.x + srcRect.width) / GLfloat(texture->width());
//    printf("Texture coords: top=%.3f bottom=%.3f left=%.3f right=%.3f\n", top, bottom, left, right);
    GLfloat textureCoords[] = {
        left, top,
        right, top,
        right, bottom,
        left, bottom
    };

    glEnableVertexAttribArray(TEXTURE_COORD_ARRAY);
    glVertexAttribPointer(TEXTURE_COORD_ARRAY, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void Application::swapBuffers()
{

    // eglSwapBuffers(eglDisplay, eglWindowSurface);
    if (composited && syncSwap) {
        // inform compositor that this window has finished rendering
        // and ready to swap
        
        // blocking client within itself scenario:
        // Works as long as this client quickly post another swapbuffer
        // that waits on the condition below. The compositor can unblock it
        // coming from the previous swap signal
        
        // If this client was doing something else that delayed another
        // swapbuffers, the compositor already posted an unblock due to 
        // it's previous swapbuffer. And this client missed it.
        // By the time it reaches this section of code
        // it already overwrites and blocks the await counter.
        XSyncSetCounter(x11Display, swap_counter, ready);        
        
        
        // wait until compositor is ready to swap
        XSyncAwait(x11Display, &wc, 1);
    }

    // blocking client in compositor sceneario:
    // this will post damage events to the comp which in turn will unblock
    // the wait condition above - provided there is a swap event immediately.

    // Otherwise, if this client is doing somthing else, it will block forever
    // because the compositor will block this after unblocking when
    // handling this client's damage 
    eglSwapBuffers(eglDisplay, eglWindowSurface);
}

static void cli(Display * display, XClientMessageEvent *event)
{
    static Atom comp = XInternAtom(display,"_MEEGOTOUCH_COMPOSITED_STATUS", 
                                   False);
    
    if (event->message_type == comp) {
        switch(event->data.l[0])
            {
            case 1:
                composited = true;
                break;
            case 0:
                composited = false;
                break;
            }
    }
}

void Application::exec()
{
    int frameBatchCount = 50;
    struct timeval startTime;
    struct timeval endTime;
    struct timeval currentTime;

    do {
        int messageCount = XPending( x11Display );
        for(int i = 0; i < messageCount; ++i) {
            XEvent	event;
            XNextEvent( x11Display, &event );

            switch( event.type ) {
            case ButtonPress:
                quit = true; // exit if clicked
                break;
            case ClientMessage:
                cli(x11Display, &event.xclient);
                break;
            default:
                break;
            }
        }


        if (quit)
            break;

        gettimeofday(&startTime, 0);
        {
            for (int i = 0; i < frameBatchCount; ++i) {
                gettimeofday(&currentTime, 0);
                int msecsElapsed = getTimeDiff(&timeOfLastUpdate, &currentTime);
                renderFrame(msecsElapsed ? msecsElapsed : 1);
                timeOfLastUpdate = currentTime;
            }
        }
        gettimeofday(&endTime, 0);

        int msecsTaken = getTimeDiff(&startTime, &endTime);
        fflush(stdout);

        static int cycle = 0;
        printf("%d\n", cycle);
        if (cycle++ == 100)
            break;

        if (msecsTaken > 1500)
            frameBatchCount -= (frameBatchCount/2) +1;
        if (msecsTaken < 500)
            frameBatchCount += (frameBatchCount/2) +1;   

    } while (1);
}

// returns true if we should exit
