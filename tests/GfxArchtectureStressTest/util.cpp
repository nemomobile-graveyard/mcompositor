#include <sys/time.h>

#include "util.h"

Texture::Texture(int w, int h)
    : m_id(0), m_width(w), m_height(h)
{
    glGenTextures(1, &m_id);
}

void Texture::upload(char* data, GLenum format, GLenum type)
{
    glBindTexture(GL_TEXTURE_2D, m_id);
//#if USE_RGB565
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, data);
//#else
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
//#endif
    glTexImage2D(GL_TEXTURE_2D, 0, format, m_width, m_height, 0, format, type, data);
}

int getTimeDiff(struct timeval* startTime, struct timeval* endTime)
{
    long long secsDiff = endTime->tv_sec - startTime->tv_sec;
    long long msecsDiff = secsDiff * 1000 + (endTime->tv_usec - startTime->tv_usec)/1000;
    return msecsDiff;
}
