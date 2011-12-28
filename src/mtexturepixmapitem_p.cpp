/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#ifdef DESKTOP_VERSION
#define GL_GLEXT_PROTOTYPES 1
#endif
#include "mtexturepixmapitem.h"
#include "texturepixmapshaders.h"
#include "mcompositewindowshadereffect.h"
#include "mcompositemanager.h"

#include <QX11Info>
#include <QRect>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#ifdef GLES2_VERSION
#include <GLES2/gl2.h>
#elif DESKTOP_VERSION
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#endif

#include "mtexturepixmapitem_p.h"

QGLWidget *MTexturePixmapPrivate::glwidget = 0;
MGLResourceManager *MTexturePixmapPrivate::glresource = 0;

static const GLuint D_VERTEX_COORDS = 0;
static const GLuint D_TEXTURE_COORDS = 1;

#ifdef QT_OPENGL_LIB

static void bindAttribLocation(QGLShaderProgram *p, const char *attrib, int location)
{
    p->bindAttributeLocation(attrib, location);
}

class MShaderProgram : public QGLShaderProgram
{
public:
    MShaderProgram(const QGLContext* context, QObject* parent)
        : QGLShaderProgram(context, parent)
    {
        texture = -1;
        opacity = -1;
        blurstep = -1;
    }
    void setWorldMatrix(GLfloat m[4][4]) {
        static bool init = true;
        if (init || memcmp(m, worldMatrix, sizeof(worldMatrix))) {
            setUniformValue("matWorld", m);
            memcpy(worldMatrix, m, sizeof(worldMatrix));
            init = false;
        }
    }

    void setTexture(GLuint t) {
        if (t != texture) {
            setUniformValue("texture", t);
            texture = t;
        }
    }

    void setOpacity(GLfloat o) {
        if (o != opacity) {
            setUniformValue("opacity", o);
            opacity = o;
        }
    }
    void setBlurStep(GLfloat b) {
        if (b != blurstep) {
            setUniformValue("blurstep", b);
            blurstep = b;
        }
    }

private:
    // static because this is set in the shared vertex shader
    static GLfloat worldMatrix[4][4];
    GLfloat opacity, blurstep;
    GLuint texture;
};

GLfloat MShaderProgram::worldMatrix[4][4];

// OpenGL ES 2.0 / OpenGL 2.0 - compatible texture painter
class MGLResourceManager: public QObject
{
public:

    /*
     * This is the default set of shaders.
     * Use MCompositeWindowShaderEffect class to add more shader effects 
     */
    enum ShaderType {
        NormalShader = 0,
        BlurShader,
        ShaderTotal
    };

    MGLResourceManager(QGLWidget *glwidget)
        : QObject(glwidget),
          glcontext(glwidget->context()),
          currentShader(0)
    {
        sharedVertexShader = new QGLShader(QGLShader::Vertex,
                glwidget->context(), this);
        if (!sharedVertexShader->compileSourceCode(QLatin1String(TexpVertShaderSource)))
            qWarning("vertex shader failed to compile");

        MShaderProgram *normalShader = new MShaderProgram(glwidget->context(),
                                                          this);
        normalShader->addShader(sharedVertexShader);
        if (!normalShader->addShaderFromSourceCode(QGLShader::Fragment,
                QLatin1String(TexpFragShaderSource)))
            qWarning("normal fragment shader failed to compile");
        shader[NormalShader] = normalShader;

        MShaderProgram *blurShader = new MShaderProgram(glwidget->context(),
                                                        this);
        shader[BlurShader] = blurShader;
        blurShader->addShader(sharedVertexShader);
        if (!blurShader->addShaderFromSourceCode(QGLShader::Fragment,
                QLatin1String(blurshader)))
            qWarning("blur fragment shader failed to compile");

        bindAttribLocation(normalShader, "inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(normalShader, "textureCoord", D_TEXTURE_COORDS);
        bindAttribLocation(blurShader, "inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(blurShader, "textureCoord", D_TEXTURE_COORDS);

        normalShader->link();
        blurShader->link();
    }

    void initVertices(QGLWidget *glwidget) {
        width = glwidget->width();
        height = glwidget->height();
        texCoords[0] = 0.0f; texCoords[1] = 1.0f;
        texCoords[2] = 0.0f; texCoords[3] = 0.0f;
        texCoords[4] = 1.0f; texCoords[5] = 0.0f;
        texCoords[6] = 1.0f; texCoords[7] = 1.0f;
        texCoordsInv[0] = 0.0f; texCoordsInv[1] = 0.0f;
        texCoordsInv[2] = 0.0f; texCoordsInv[3] = 1.0f;
        texCoordsInv[4] = 1.0f; texCoordsInv[5] = 1.0f;
        texCoordsInv[6] = 1.0f; texCoordsInv[7] = 0.0f;

        projMatrix[0][0] =  2.0 / width; projMatrix[1][0] =  0.0;
        projMatrix[2][0] =  0.0;         projMatrix[3][0] = -1.0;
        projMatrix[0][1] =  0.0;         projMatrix[1][1] = -2.0 / height;
        projMatrix[2][1] =  0.0;         projMatrix[3][1] =  1.0;
        projMatrix[0][2] =  0.0;         projMatrix[1][2] =  0.0;
        projMatrix[2][2] = -1.0;         projMatrix[3][2] =  0.0;
        projMatrix[0][3] =  0.0;         projMatrix[1][3] =  0.0;
        projMatrix[2][3] =  0.0;         projMatrix[3][3] =  1.0;

        worldMatrix[0][2] = 0.0;
        worldMatrix[1][2] = 0.0;
        worldMatrix[2][0] = 0.0;
        worldMatrix[2][1] = 0.0;
        worldMatrix[2][2] = 1.0;
        worldMatrix[2][3] = 0.0;
        worldMatrix[3][2] = 0.0;
        glViewport(0, 0, width, height);

        for (int i = 0; i < ShaderTotal; i++) {
            shader[i]->bind();
            shader[i]->setUniformValue("matProj", projMatrix);
        }
    }

    void updateVertices(const QTransform &t) 
    {
        worldMatrix[0][0] = t.m11();
        worldMatrix[0][1] = t.m12();
        worldMatrix[0][3] = t.m13();
        worldMatrix[1][0] = t.m21();
        worldMatrix[1][1] = t.m22();
        worldMatrix[1][3] = t.m23();
        worldMatrix[3][0] = t.dx();
        worldMatrix[3][1] = t.dy();
        worldMatrix[3][3] = t.m33();
    }

    void updateVertices(const QTransform &t, ShaderType type) 
    {        
        if (shader[type] != currentShader)
            currentShader = shader[type];
        
        updateVertices(t);
        if (!currentShader->bind())
            qWarning() << __func__ << "failed to bind shader program";
        currentShader->setWorldMatrix(worldMatrix);
    }

    
    void updateVertices(const QTransform &t, GLuint customShaderId) 
    {                
        if (!customShaderId)
            return;
        MShaderProgram* frag = customShadersById.value(customShaderId, 0);
        if (!frag)
            return;
        currentShader = frag;        
        updateVertices(t);
        if (!currentShader->bind())
            qWarning() << __func__ << "failed to bind shader program";
        currentShader->setWorldMatrix(worldMatrix);
    }

    GLuint installPixelShader(const QByteArray& code)
    {
        QHash<QByteArray, MShaderProgram *>::iterator it = customShadersByCode.find(code);
        if (it != customShadersByCode.end())  {
            return it.value()->programId();
        }

        QByteArray source = code;
        source.append(TexpCustomShaderSource);
        MShaderProgram *p = new MShaderProgram(glcontext, this);
        p->addShader(sharedVertexShader);
        if (!p->addShaderFromSourceCode(QGLShader::Fragment,
                QLatin1String(source)))
            qWarning("custom fragment shader failed to compile");

        bindAttribLocation(p, "inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(p, "textureCoord", D_TEXTURE_COORDS);

        if (p->link()) {
            customShadersByCode[code] = p;
            customShadersById[p->programId()] = p;
            return p->programId();
        } 
       
        qWarning() << "failed installing custom fragment shader:"
                   << p->log();
        p->deleteLater();
        
        return 0;
    }

private:
    static MShaderProgram *shader[ShaderTotal];
    QHash<GLuint, MShaderProgram *> customShadersById;
    QHash<QByteArray, MShaderProgram *> customShadersByCode;
    QGLShader *sharedVertexShader;
    const QGLContext* glcontext;    
    
    GLfloat projMatrix[4][4];
    GLfloat worldMatrix[4][4];
    GLfloat vertCoords[8];
    GLfloat texCoords[8];
    GLfloat texCoordsInv[8];
    MShaderProgram *currentShader;
    int width;
    int height;

    friend class MTexturePixmapPrivate;
};

MShaderProgram *MGLResourceManager::shader[ShaderTotal];
#endif

void MTexturePixmapPrivate::paint(QPainter *painter)
{
    if (direct_fb_render) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

#ifdef GLES2_VERSION
    if (painter->paintEngine()->type() != QPaintEngine::OpenGL2)
        return;
    if (current_window_group.isNull()) 
        renderTexture(painter->combinedTransform());
#else
    if (painter->paintEngine()->type() != QPaintEngine::OpenGL2
        && painter->paintEngine()->type() != QPaintEngine::OpenGL)
        return;
    painter->beginNativePainting();
    renderTexture(painter->combinedTransform());
    painter->endNativePainting();
#endif
}

void MTexturePixmapPrivate::renderTexture(const QTransform& transform)
{
    if (item->propertyCache()->hasAlphaAndIsNotOpaque() ||
        item->opacity() < 1.0f) {
        // Blend differently if fading in on the top of a splash screen.
        glEnable(GL_BLEND);
        glBlendFunc(static_cast<MCompositeManager*>(qApp)->splashed(item)
                        ? GL_ONE : GL_SRC_ALPHA,
                    GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindTexture(GL_TEXTURE_2D, TFP.textureId);

    const QRegion &shape = item->propertyCache()->shapeRegion();
    // FIXME: not optimal. probably would be better to replace with 
    // eglSwapBuffersRegionNOK()

    bool shape_on = !QRegion(item->boundingRect().toRect()).subtracted(shape).isEmpty();
    bool scissor_on = damageRegion.numRects() > 1 || shape_on;
    
    if (scissor_on)
        glEnable(GL_SCISSOR_TEST);
    
    // Damage regions taking precedence over shape rects 
    if (damageRegion.numRects() > 1) {
        for (int i = 0; i < damageRegion.numRects(); ++i) {
            glScissor(damageRegion.rects().at(i).x(),
                      brect.height() -
                      (damageRegion.rects().at(i).y() +
                       damageRegion.rects().at(i).height()),
                      damageRegion.rects().at(i).width(),
                      damageRegion.rects().at(i).height());
            drawTexture(transform, item->boundingRect(), item->opacity());        
        }
    } else if (shape_on) {
        // draw a shaped window using glScissor
        for (int i = 0; i < shape.numRects(); ++i) {
            glScissor(shape.rects().at(i).x(),
                      brect.height() -
                      (shape.rects().at(i).y() +
                       shape.rects().at(i).height()),
                      shape.rects().at(i).width(),
                      shape.rects().at(i).height());
            drawTexture(transform, item->boundingRect(), item->opacity());
        }
    } else
        drawTexture(transform, item->boundingRect(), item->opacity());
    
    if (scissor_on)
        glDisable(GL_SCISSOR_TEST);

    //    qDebug() << __func__ << item->window() << item->pos();

    // Explicitly disable blending. for some reason, the latest drivers
    // still has blending left-over even if we call glDisable(GL_BLEND)
    glBlendFunc(GL_ONE, GL_ZERO);
    glDisable(GL_BLEND);
#ifdef WINDOW_DEBUG
    ++item_painted;
#endif
}

void MTexturePixmapPrivate::clearTexture()
{
    glBindTexture(GL_TEXTURE_2D, TFP.textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, 0);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void MTexturePixmapPrivate::drawTexture(const QTransform &transform,
                                        const QRectF &drawRect,
                                        qreal opacity)
{
    if (current_effect) {
        current_effect->d->drawTexture(this, transform, drawRect, opacity);
    } else
        q_drawTexture(transform, drawRect, opacity);
}

void MTexturePixmapPrivate::q_drawTexture(const QTransform &transform,
                                          const QRectF &drawRect,
                                          qreal opacity,
                                          const GLvoid* texCoords)
{
    if (current_effect)
        glresource->updateVertices(transform, current_effect->activeShaderFragment());
    else
        glresource->updateVertices(transform, MGLResourceManager::NormalShader);
    GLfloat vertexCoords[] = {
        drawRect.left(),  drawRect.top(),
        drawRect.left(),  drawRect.bottom(),
        drawRect.right(), drawRect.bottom(),
        drawRect.right(), drawRect.top()
    };
    glEnableVertexAttribArray(D_VERTEX_COORDS);
    glEnableVertexAttribArray(D_TEXTURE_COORDS);
    glVertexAttribPointer(D_VERTEX_COORDS, 2, GL_FLOAT, GL_FALSE, 0, vertexCoords);
    glVertexAttribPointer(D_TEXTURE_COORDS, 2, GL_FLOAT, GL_FALSE, 0, texCoords);

    if (current_effect)
        current_effect->setUniforms(glresource->currentShader);
    
    glresource->currentShader->setOpacity((GLfloat) opacity);
    glresource->currentShader->setTexture(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(D_VERTEX_COORDS);
    glDisableVertexAttribArray(D_TEXTURE_COORDS);

#ifdef DESKTOP_VERSION
    glwidget->paintEngine()->syncState();
#endif
    glActiveTexture(GL_TEXTURE0);
}

void MTexturePixmapPrivate::q_drawTexture(const QTransform &transform,
                                          const QRectF &drawRect,
                                          qreal opacity,
                                          bool texcoords_from_rect)
{
    GLfloat texCoords[8];
    const GLvoid* textureCoords;

    if (texcoords_from_rect) {
        float w, h, x, y, cx, cy, cw, ch;
        w = item->boundingRect().width();
        h = item->boundingRect().height();
        x = item->boundingRect().x();
        y = item->boundingRect().y();

        cx = (drawRect.x() - x) / w;
        cy = (drawRect.y() - y) / h;
        cw = drawRect.width() / w;
        ch = drawRect.height() / h;
        if (inverted_texture) {
            texCoords[0] = cx;      texCoords[1] = cy;
            texCoords[2] = cx;      texCoords[3] = ch + cy;
            texCoords[4] = cx + cw; texCoords[5] = ch + cy;
            texCoords[6] = cx + cw; texCoords[7] = cy;
        } else {
            texCoords[0] = cx;      texCoords[1] = ch + cy;
            texCoords[2] = cx;      texCoords[3] = cy;
            texCoords[4] = cx + cw; texCoords[5] = cy;
            texCoords[6] = cx + cw; texCoords[7] = ch + cy;
        }
        textureCoords = &texCoords;
    }
    else if (inverted_texture)
        textureCoords = glresource->texCoordsInv;
    else
        textureCoords = glresource->texCoords;
    
    q_drawTexture(transform, drawRect, opacity, textureCoords);
}

void MTexturePixmapPrivate::installEffect(MCompositeWindowShaderEffect* effect)
{
    if (effect == prev_effect)
        return;

    if (prev_effect) {
        disconnect(prev_effect, SIGNAL(enabledChanged(bool)), this,
                   SLOT(activateEffect(bool)));
        disconnect(prev_effect, SIGNAL(destroyed()), this,
                   SLOT(removeEffect()));
    }
    current_effect = effect;
    prev_effect = effect;
    if (effect) {
        connect(effect, SIGNAL(enabledChanged(bool)),
                SLOT(activateEffect(bool)), Qt::UniqueConnection);
        connect(effect, SIGNAL(destroyed()), SLOT(removeEffect()),
                Qt::UniqueConnection);
    }
}

void MTexturePixmapPrivate::removeEffect()
{
    MCompositeWindowShaderEffect* e= (MCompositeWindowShaderEffect* ) sender();
    if (e == prev_effect)
        prev_effect = 0;
}

GLuint MTexturePixmapPrivate::installPixelShader(const QByteArray& code)
{
    if (!glwidget) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        glwidget = m->glWidget();
    }
    if (!glresource) {
        glresource = new MGLResourceManager(glwidget);
        glresource->initVertices(glwidget);
    }
    if (glresource)
        return glresource->installPixelShader(code);

    return 0;
}

void MTexturePixmapPrivate::activateEffect(bool enabled)
{
    if (enabled)
        current_effect = (MCompositeWindowShaderEffect* ) sender();
    else
        current_effect = 0;
}

void MTexturePixmapPrivate::init()
{
    if (!item->isValid())
        return;

    if (!glresource) {
        glresource = new MGLResourceManager(glwidget);
        glresource->initVertices(glwidget);
    }

    resize(item->propertyCache()->realGeometry().width(),
           item->propertyCache()->realGeometry().height());
    item->setPos(item->propertyCache()->realGeometry().x(),
                 item->propertyCache()->realGeometry().y());
}

MTexturePixmapPrivate::MTexturePixmapPrivate(Qt::HANDLE window,
                                             MTexturePixmapItem *p)
    : window(window),
      TFP(),
      inverted_texture(false),
      direct_fb_render(false), // root's children start redirected
      angle(0),
      item(p),
      prev_effect(0),
      pastDamages(0)
{
    if (!glwidget) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        glwidget = m->glWidget();
    }
    if (item->propertyCache())
        item->propertyCache()->damageTracking(true);
    init();
}

MTexturePixmapPrivate::~MTexturePixmapPrivate()
{
    if (item->propertyCache())
        item->propertyCache()->damageTracking(false);

    if (TFP.drawable && !item->propertyCache()->isVirtual())
        XFreePixmap(QX11Info::display(), TFP.drawable);

    if (pastDamages)
        delete pastDamages;
}

void MTexturePixmapPrivate::saveBackingStore()
{
    if (item->propertyCache()->isVirtual()) {
        TFP.bind(item->windowPixmap());
        return;
    }
    if ((item->propertyCache()->is_valid && !item->propertyCache()->isMapped())
        || item->propertyCache()->isInputOnly()
        || !window)
        return;

    if (TFP.drawable)
        XFreePixmap(QX11Info::display(), TFP.drawable);

    // Pixmap is already freed. No sense to bind it to texture
    if (item->isClosing() || item->window() < QX11Info::appRootWindow())
        return;

    Drawable pixmap = XCompositeNameWindowPixmap(QX11Info::display(), item->window());
    TFP.bind(pixmap);
}

void MTexturePixmapPrivate::resize(int w, int h)
{
    if (!window)
        return;
    
    if (!brect.isEmpty() && !item->isDirectRendered() && (brect.width() != w || brect.height() != h)) {
        item->saveBackingStore();
        item->updateWindowPixmap();
    }
    brect.setWidth(w);
    brect.setHeight(h);
}

void MTexturePixmapItem::updateWindowPixmapProxy()
{
    updateWindowPixmap(0, 0, ((MCompositeManager*)qApp)->getServerTime());
}

bool MTexturePixmapItem::isDirectRendered() const
{
    return d->direct_fb_render;
}

void MTexturePixmapItem::paint(QPainter *painter,
                               const QStyleOptionGraphicsItem *option,
                               QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    d->paint(painter);
}

void MTexturePixmapItem::renderTexture(const QTransform& transform)
{
    d->renderTexture(transform);
}

void MTexturePixmapItem::clearTexture()
{
    d->clearTexture();
}

void MTexturePixmapItem::saveBackingStore()
{
    d->saveBackingStore();
}

void MTexturePixmapItem::resize(int w, int h)
{
    d->resize(w, h);
    this->MCompositeWindow::resize(w, h);
}

QSizeF MTexturePixmapItem::sizeHint(Qt::SizeHint, const QSizeF &) const
{
    return boundingRect().size();
}

QRectF MTexturePixmapItem::boundingRect() const
{
    return d->brect;
}

MTexturePixmapPrivate* MTexturePixmapItem::renderer() const
{
    return d;
}
