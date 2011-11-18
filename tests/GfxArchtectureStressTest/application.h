#ifndef APPLICATION_H
#define APPLICATION_H

#include <X11/extensions/sync.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "util.h"

const GLint VERTEX_COORD_ARRAY = 0;
const GLint TEXTURE_COORD_ARRAY = 1;

class Application
{
public:
    Application(int w, int h, bool tryUsingAlpha);
    virtual ~Application();

    bool initWindow();
    bool initTextureShader();

    void drawTexture(const Rect& destRect, Texture *texture, const Rect& srcRect);

    void swapBuffers();
    void setSyncSwap(bool syncswap) { syncSwap = syncswap; }
    void setDesktop(bool d) { desktop = d; }
    void exec(); // main loop

protected:
    int             width;
    int             height;
    bool            quit;
    bool            usingAlpha;

    virtual void renderFrame(int msecsElapsed) = 0;
    virtual void initGl() = 0;

private:
    Window          x11Window;
    Display*        x11Display;
    long            x11Screen;

    EGLDisplay      eglDisplay;
    EGLConfig       eglConfig;
    EGLSurface      eglWindowSurface;
    EGLContext      eglContext;

    GLint           textureDrawingProgId;

    struct timeval  timeOfLastUpdate;

    bool            tryAlpha;
    bool            syncSwap;
    bool            desktop;

    XSyncValue ready;
    XSyncValue consumed;
    XSyncValue explicit_reset;
    XSyncCounter swap_counter;
    XSyncWaitCondition wc;
};

#endif // APPLICATION_H
