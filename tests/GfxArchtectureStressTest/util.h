#ifndef UTIL_H
#define UTIL_H

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define USE_RGB565 0
//#define USE_RGB565 1	/* This only works for settings */

#define COMPOSITE_PIXMAP_READY    0x5000
#define COMPOSITE_PIXMAP_CONSUMED 0x5001
#define EXPLICIT_RESET 0x5002

struct Rect {
    Rect()
            : x(0), y(0), width(0), height(0) {}
    Rect(int _x, int _y, int w, int h)
            : x(_x), y(_y), width(w), height(h) {}

    int x;
    int y;
    int width;
    int height;
};

class Texture {

public:
    Texture(int w, int h);

    void upload(char* data, GLenum format = GL_RGBA, GLenum type = GL_UNSIGNED_BYTE);

    int width() const {return m_width;}
    int height() const {return m_height;}
    GLuint id() const {return m_id;}

private:
    GLuint  m_id;
    int     m_width;
    int     m_height;

};

typedef struct {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
} RgbaPixel;

typedef union {
    unsigned short red;
    unsigned short green;
    unsigned short blue;
} RgbPixel;

int getTimeDiff(struct timeval* startTime, struct timeval* endTime);

#endif // UTIL_H
