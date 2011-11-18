
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "util.h"
#include "application.h"

class DummyDui : public Application
{
public:
    DummyDui()
        : Application(864, 480, false), offset(0.0), dy(+1) {}

    ~DummyDui();

protected:
    void renderFrame(int msecsElapsed);
    void initGl();

private:
    Texture *backgroundTexture;
    float offset;
    int dy;
};

DummyDui::~DummyDui()
{
    delete backgroundTexture;
}

#if USE_RGB565
void DummyDui::initGl()
{
    int textureWidth = width;
    int textureHeight = height * 2;

    int pixelCount = textureWidth*textureHeight;
    int dataSize = pixelCount * 2;
    int scanlineSize = textureWidth*2;
    char* data = new char[dataSize];
    RgbPixel* pixels = (RgbPixel*)data;
    RgbPixel* pixel = (RgbPixel*)data;


    memset(data,0,dataSize);
    backgroundTexture = new Texture(textureWidth, textureHeight);

    // Top (RED)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = 0; y < (textureHeight/4); ++y) {
            RgbPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->red |= 0xf800;
            pixel->green |= 0x0000;
            pixel->blue |= 0x0000;
        }
    }

    // Top-Middle (GREEN)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = (textureHeight/4); y < (textureHeight/2); ++y) {
            RgbPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->red |= 0x0000;
            pixel->green |= 0x07e0;
            pixel->blue |= 0x0000;
        }
    }

    // Bottom-Middle (BLUE)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = (textureHeight/2); y < textureHeight-(textureHeight/4); ++y) {
            RgbPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->red |= 0x0000;
            pixel->green |= 0x0000;
            pixel->blue |= 0x001b;
        }
    }

    // Bottom (MAGENTA)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = textureHeight-(textureHeight/4); y < textureHeight; ++y) {
            RgbPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->red |= 0xF800;
            pixel->green |= 0x0000;
            pixel->blue |= 0x001b;
        }
    }

    backgroundTexture->upload(data, GL_RGB, GL_UNSIGNED_SHORT_5_6_5);

    delete [] data;

    glClearColor(0.0, 0.0, 0.0, 1.0);
}
#else
void DummyDui::initGl()
{
    int textureWidth = width;
    int textureHeight = height * 2;

    int pixelCount = textureWidth*textureHeight;
    int dataSize = pixelCount * 4;
//    int scanlineSize = textureWidth*4;
    char* data = new char[dataSize];
    RgbaPixel* pixels = (RgbaPixel*)data;
//    RgbaPixel* pixel = (RgbaPixel*)data;

    backgroundTexture = new Texture(textureWidth, textureHeight);

    // Top (RED)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = 0; y < (textureHeight/4); ++y) {
            RgbaPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->alpha = 0xFF;
            pixel->red = 0xFF;
            pixel->green = 0x00;
            pixel->blue = 0x00;
        }
    }

    // Top-Middle (GREEN)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = (textureHeight/4); y < (textureHeight/2); ++y) {
            RgbaPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->alpha = 0xFF;
            pixel->red = 0x00;
            pixel->green = 0xFF;
            pixel->blue = 0x00;
        }
    }

    // Bottom-Middle (BLUE)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = (textureHeight/2); y < textureHeight-(textureHeight/4); ++y) {
            RgbaPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->alpha = 0xFF;
            pixel->red = 0x00;
            pixel->green = 0x00;
            pixel->blue = 0xFF;
        }
    }

    // Bottom (MAGENTA)
    for (int x = 0; x < textureWidth; ++x) {
        for (int y = textureHeight-(textureHeight/4); y < textureHeight; ++y) {
            RgbaPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->alpha = 0xFF;
            pixel->red = 0xFF;
            pixel->green = 0x00;
            pixel->blue = 0xFF;
        }
    }

    backgroundTexture->upload(data);

    delete [] data;

    glClearColor(0.0, 0.0, 0.0, 1.0);
}
#endif

void DummyDui::renderFrame(int msecsElapsed)
{
    const float speed = 200; // pixels per sec
    float distance = speed * (msecsElapsed/1000.0);

    if (offset < 0.0) {
        offset = 0.0;
        dy = +1;
    }
    if (offset >= height) {
        offset = height -1.0;
        dy = -1;
    }

    offset += dy * distance;

    Rect destRect(0, 0, width, height);
    Rect srcRect(0, int(offset), backgroundTexture->width(), 480);
    glDisable(GL_BLEND);
    drawTexture(destRect, backgroundTexture, srcRect);

    swapBuffers();
}









class Settings : public Application
{
public:
    Settings()
        : Application(864, 480, true), borderSize(25),
          x1(300), y1(200), dx1(+1), dy1(+1),
          x2(500), y2(200), dx2(-1), dy2(-1)
    {}

    ~Settings();

protected:
    void renderFrame(int msecsElapsed);
    void initGl();

private:
    Texture *backgroundTexture;
    Texture *iconTexture;
    int borderSize;

    void createIconTexture();
    void createBackgroundTexture();

    float x1;
    float y1;
    int dx1;
    int dy1;
    float x2;
    float y2;
    int dx2;
    int dy2;

};

Settings::~Settings()
{
    delete backgroundTexture;
    delete iconTexture;
}


void Settings::initGl()
{
    createIconTexture();
    createBackgroundTexture();
    glClearColor(0.0, 0.0, 0.0, 0.0);
}

void Settings::createIconTexture()
{
    int textureWidth = 128;
    int textureHeight = 128;

    int pixelCount = textureWidth*textureHeight;
    int dataSize = pixelCount * 4;
//    int scanlineSize = textureWidth*4;
    char* data = new char[dataSize];
    RgbaPixel* pixels = (RgbaPixel*)data;

    iconTexture = new Texture(textureWidth, textureHeight);

    // Center
    float cx = 0.5;
    float cy = 0.5;

    float distance;
//    float distanceSquared;
    float alpha;

    for (int y = 0; y < textureHeight; ++y) {
        float ny = y / float(textureHeight); //normalised

        for (int x = 0; x < textureWidth; ++x) {

            float nx = x / float(textureWidth); //normalised

            // sqrt{(x - a)^2 + (y - b)^2}
            distance = sqrtf((nx - cx)*(nx-cx) + (ny-cy)*(ny-cy)); // 0..0.71
            distance *= 2.0;

            if (distance > 1.0)
                alpha = 0.0; // clamp
            else
                alpha = 1.0 - distance*distance*distance;

            RgbaPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->alpha = 255*alpha;
            pixel->red = 127;
            pixel->green = 255; //*alpha + 127;
            pixel->blue = 127; //*alpha + 127;
        }
    }

    iconTexture->upload(data);

    delete [] data;
}

void Settings::createBackgroundTexture()
{
    int textureWidth = width - borderSize*2;
    int textureHeight = height - borderSize*2;

    int pixelCount = textureWidth*textureHeight;
    int dataSize = pixelCount * 4;
//    int scanlineSize = textureWidth*4;
    char* data = new char[dataSize];
    RgbaPixel* pixels = (RgbaPixel*)data;
//    RgbaPixel* pixel = (RgbaPixel*)data;

    backgroundTexture = new Texture(textureWidth, textureHeight);

    for (int x = 0; x < textureWidth; ++x) {
        for (int y = 0; y < (textureHeight); ++y) {
            RgbaPixel* pixel = &(pixels[x + y*textureWidth]);
            pixel->alpha = 127;
            pixel->red = 0;
            pixel->green = 0;
            pixel->blue = 0;
        }
    }

    backgroundTexture->upload(data);

    delete [] data;
}



void Settings::renderFrame(int msecsElapsed)
{
    const float speed = 200; // pixels per sec
    float distance = speed * (msecsElapsed/1000.0);

    if (x1 < borderSize) {
        dx1 = +1;
        x1 = borderSize;
    }
    if (x1 > width - borderSize - iconTexture->width()) {
        dx1 = -1;
        x1 = width - borderSize - iconTexture->width();
    }
    if (y1 < borderSize) {
        y1 = borderSize;
        dy1 = +1;
    }
    if (y1 > height - borderSize - iconTexture->height()) {
        y1 = height - borderSize - iconTexture->height();
        dy1 = -1;
    }
    x1 += dx1 * distance;
    y1 += dy1 * distance;

    if (x2 < borderSize) {
        dx2 = +1;
        x2 = borderSize;
    }
    if (x2 > width - borderSize - iconTexture->width()) {
        dx2 = -1;
        x2 = width - borderSize - iconTexture->width();
    }
    if (y2 < borderSize) {
        y2 = borderSize;
        dy2 = +1;
    }
    if (y2 > height - borderSize - iconTexture->height()) {
        y2 = height - borderSize - iconTexture->height();
        dy2 = -1;
    }
    x2 += dx2 * distance;
    y2 += dy2 * distance;

    glClear(GL_COLOR_BUFFER_BIT);

    // Draw the background
    Rect bgDestRect(borderSize, borderSize, width-borderSize*2, height-borderSize*2);
    Rect bgSrcRect(0, 0, backgroundTexture->width(), backgroundTexture->height());
    glDisable(GL_BLEND);
    drawTexture(bgDestRect, backgroundTexture, bgSrcRect);

    // Draw the icons
    Rect iconSrcRect(0, 0, iconTexture->width(), iconTexture->height());
    Rect icon1DestRect(x1, y1, iconTexture->width(), iconTexture->height());
    Rect icon2DestRect(x2, y2, iconTexture->width(), iconTexture->height());
    glEnable(GL_BLEND);
    if (usingAlpha)
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE );
    else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawTexture(icon1DestRect, iconTexture, iconSrcRect);
    drawTexture(icon2DestRect, iconTexture, iconSrcRect);

    swapBuffers();
}

class ARGBRender : public Application
{
public:
    ARGBRender(int td)
        : Application(864, 480, false), offset(0.0), dy(+1), textureDepth(td) {}

    ~ARGBRender();

protected:
    void renderFrame(int msecsElapsed);
    void initGl();

private:
    Texture *texture;
    float offset;
    int dy;
    int textureDepth;
};

ARGBRender::~ARGBRender()
{
    delete texture;
}

void ARGBRender::initGl()
{
    int textureWidth = width;
    int textureHeight = height * 2;

    int pixelCount = textureWidth*textureHeight;
    int dataSize;
    if (textureDepth == 16)
        dataSize = pixelCount * 2;
    else
        dataSize = pixelCount * 4;

    char* data = new char[dataSize];

    memset(data,0,dataSize);
    texture = new Texture(textureWidth, textureHeight);

    if (textureDepth == 16) {
        unsigned short* pixels = (unsigned short*)data;

        // Top (RED)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = 0; y < (textureHeight/4); ++y) {
                unsigned short * pixel = &(pixels[x + y*textureWidth]);
                // RRRRGGGGBBBBAAAA
                // 1111000000001010
                *pixel = 0xF00A;
            }
        }

        // Top-Middle (GREEN)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = (textureHeight/4); y < (textureHeight/2); ++y) {
                unsigned short * pixel = &(pixels[x + y*textureWidth]);
                *pixel = 0x0F0A;
            }
        }

        // Bottom-Middle (BLUE)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = (textureHeight/2); y < textureHeight-(textureHeight/4); ++y) {
                unsigned short * pixel = &(pixels[x + y*textureWidth]);
                *pixel = 0x00FA;
            }
        }

        // Bottom (MAGENTA)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = textureHeight-(textureHeight/4); y < textureHeight; ++y) {
                unsigned short * pixel = &(pixels[x + y*textureWidth]);
                *pixel = 0xF0FA;
            }
        }

        texture->upload(data, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
    }
    else {
        RgbaPixel* pixels = (RgbaPixel*)data;

        // Top (RED)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = 0; y < (textureHeight/4); ++y) {
                RgbaPixel * pixel = &(pixels[x + y*textureWidth]);
                pixel->red = 0xFF;
                pixel->green = 0x00;
                pixel->blue = 0x00;
                pixel->alpha = 0xA0;
            }
        }

        // Top-Middle (GREEN)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = (textureHeight/4); y < (textureHeight/2); ++y) {
                RgbaPixel* pixel = &(pixels[x + y*textureWidth]);
                pixel->red = 0x00;
                pixel->green = 0xFF;
                pixel->blue = 0x00;
                pixel->alpha = 0xA0;
            }
        }

        // Bottom-Middle (BLUE)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = (textureHeight/2); y < textureHeight-(textureHeight/4); ++y) {
                RgbaPixel * pixel = &(pixels[x + y*textureWidth]);
                pixel->red = 0x00;
                pixel->green = 0x00;
                pixel->blue = 0xFF;
                pixel->alpha = 0xA0;
            }
        }

        // Bottom (MAGENTA)
        for (int x = 0; x < textureWidth; ++x) {
            for (int y = textureHeight-(textureHeight/4); y < textureHeight; ++y) {
                RgbaPixel * pixel = &(pixels[x + y*textureWidth]);
                pixel->red = 0xFF;
                pixel->green = 0x00;
                pixel->blue = 0xFF;
                pixel->alpha = 0xA0;
            }
        }

        texture->upload(data, GL_RGBA, GL_UNSIGNED_BYTE);
    }

    delete [] data;

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE );
}


void ARGBRender::renderFrame(int msecsElapsed)
{
    const float speed = 200; // pixels per sec
    float distance = speed * (msecsElapsed/1000.0);

    if (offset < 0.0) {
        offset = 0.0;
        dy = +1;
    }
    if (offset >= height) {
        offset = height -1.0;
        dy = -1;
    }

    offset += dy * distance;

    glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, width, height/2);
        glClearColor(1.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        glScissor(0, height/2, width, height/2);
        glClearColor(0.0, 1.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    Rect destRect(0, 0, width, height);
    Rect srcRect(0, int(offset), texture->width(), 480);
    drawTexture(destRect, texture, srcRect);

    swapBuffers();
}


int main(int argc, char** argv)
{
    Application* app;

//    bool launchSettingsApp = false;

    if (argc == 2 || argc == 3) {
        if (strncmp(argv[1], "settings", 8) == 0)
            app = new Settings;
        else if (strncmp(argv[1], "dummydui", 8) == 0)
            app = new DummyDui;
        else if (strncmp(argv[1], "argb16", 6) == 0)
            app = new ARGBRender(16);
        else if (strncmp(argv[1], "argb32", 6) == 0)
            app = new ARGBRender(32);
        else if (strncmp(argv[1], "desktop", 7) == 0) {
            app = new DummyDui;
            app->setDesktop(true);
        } else {
            printf("Usage: GfxArchitectureStressTest [settings|desktop|dummydui|argb16|argb32] [syncswap]\n");
            return -1;
        }
        
        if (argc == 3) {
            if (strncmp(argv[2], "syncswap", 8) == 0)
                app->setSyncSwap(true);
            else {
                printf("Usage: GfxArchitectureStressTest [settings|desktop|dummydui|argb16|argb32] [syncswap]\n");
                return -1;
            }
        }
    }
    else {
        printf("Usage: GfxArchitectureStressTest [settings|desktop|dummydui|argb16|argb32] [syncswap]\n");
        return -1;
    }

    if (!app->initWindow())
        return -1;

    if (!app->initTextureShader())
        return -1;

    app->exec();
}

