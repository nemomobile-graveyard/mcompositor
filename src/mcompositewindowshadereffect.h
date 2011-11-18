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

#ifndef MCOMPOSITEWINDOWSHADEREFFECT_H
#define MCOMPOSITEWINDOWSHADEREFFECT_H

#include <QObject>
#include <QVector>
#include <QGLShaderProgram>

#include <texturecoords.h>

class QTransform;
class QRectF;
class MCompositeWindowShaderEffect;
class MTexturePixmapPrivate;
class MCompositeWindow;
class MCompositeWindowShaderEffectPrivate;
class EffectNode;
class GeometryNode;

class MCompositeWindowShaderEffect: public QObject
{
    Q_OBJECT
 public:
    MCompositeWindowShaderEffect(QObject* parent = 0);
    virtual ~MCompositeWindowShaderEffect();
    
    GLuint installShader(const QByteArray& fragment,
                         const QByteArray& vertex = QByteArray());
    GLuint texture() const;
    void setActiveShaderFragment(GLuint id);
    GLuint activeShaderFragment() const;

    virtual void installEffect(MCompositeWindow* window);
    void removeEffect(MCompositeWindow* window);
    bool enabled() const;

    /* Primitives */
    int addQuad(GLuint textureId, 
                const QRectF& geometry, 
                const TextureCoords& texcoords = TextureCoords(),
                bool parent_transform = true,
                bool has_alpha = false,
                GLuint fragshaderId = 0);
    void setQuadVisible(int id, bool visible);
    void setQuadGeometry(int id, const QRectF& geometry);
    const QRectF& quadGeometry(int id);
    
    /* Painting control */
    void setWindowGeometry(const QRectF& geometry, 
                           Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio);
    
 public slots:
    void setEnabled(bool enabled);

 signals:
    void enabledChanged( bool enabled);
    
 protected: 
    virtual void render();
    
    MCompositeWindow* currentWindow();
    virtual void setUniforms(QGLShaderProgram* program);

 private:    
    /* \cond */
    const QVector<GLuint>& fragmentIds() const;
    int addQuad(GeometryNode*);
    EffectNode* effectNode() const;
    void hookWindowNode(GeometryNode* node);
    // for node manager
    QVector<GeometryNode*>& customQuads();

    MCompositeWindowShaderEffectPrivate* d;
    friend class MTexturePixmapPrivate;
    friend class MCompositeWindowShaderEffectPrivate;
    friend class MRender;
    /* \endcond */

 private slots:
    void compWindowDestroyed();
};

#endif
