/***************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
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

#include <QRectF>
#include <QVector>
#include <QtOpenGL>

#ifndef SCENERENDER_H
#define SCENERENDER_H

class SceneNode;
class GeometryNode;
class SceneRenderPrivate;

typedef QPair<QMatrix4x4, GeometryNode*> AlphaMesh;
typedef QPair<GLenum, GLenum> BlendFunc;

#ifdef DEBUG_SCENEGRAPH
typedef void (*RenderDebug)(void *message);
#endif

class SceneRender
{
 public:
    enum WalkNode
    {
        ProcessNext,
        SkipNext,
        Stop
    };

    SceneRender();
    WalkNode renderScene(SceneNode* root);
    QMatrix4x4 currentTransform() const { return _current_transform; }
    SceneNode* currentParent() const { return _current_parent; }
    static void init(QGLWidget* widget);
    static GLuint installPixelShader(const QByteArray& fragment, 
                                     const QByteArray& vertex);
    void setFboRender(bool fbo) { _fbo_render = fbo; }
    bool fboRender() { return _fbo_render; }
    void setCleared(bool cleared) { _clearscene = cleared; }
    bool isCleared() { return _clearscene; }

#ifdef DEBUG_SCENEGRAPH
    void setNodeDebugFilter(RenderDebug f) {  _render_debug = f; }
    RenderDebug debugFilter() const { return _render_debug; }
#endif
    
 private:
    void renderGeometry(GeometryNode* node, QMatrix4x4& transform);
    bool rectVisible(const QRectF& rect);

    QRectF             _current_bounding;
    QVector<AlphaMesh> _alpha_stack;
    QVector<QRectF>    _visible_rects;
    QMatrix4x4         _current_transform;
    SceneNode*         _current_parent;
    int                _process_depth;
    bool               _fbo_render;
    bool               _current_alpha;
    bool               _clearscene;
    qreal              _current_opacity;
    BlendFunc          _current_blendfunc;
#ifdef DEBUG_SCENEGRAPH
    RenderDebug        _render_debug;
#endif
    static SceneRenderPrivate* d;
    friend class TransformNode;
    friend class GeometryNode;
};

#endif
