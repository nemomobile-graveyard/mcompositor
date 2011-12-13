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

#include <QObject>
#include <QMatrix4x4>

#include "scenerender.h"
#include "texturecoords.h"

#ifndef SCENENODE_H
#define SCENENODE_H

class QGLShaderProgram;
class SceneNode
{
 public:

    enum NodeType
    {
        Root,
        Transform,
        Geometry,
        Effect
    };
        
    SceneNode();
    ~SceneNode();
    void appendChild(SceneNode* child);
    void prependChild(SceneNode* child);
    void insertChildBefore(SceneNode* before, SceneNode* child);
    void insertChildAfter(SceneNode* after, SceneNode* child);
    SceneNode* parentNode() const { return _parent; }
    NodeType nodeType() const { return _type; } 
    virtual void setPosition(unsigned int index );
    unsigned int nodePosition() const { return _position; }
    virtual void detach();
    void clearChildren();

 protected:
    virtual SceneRender::WalkNode processNode();
    void setProcessed(bool p) { _processed = p; }
    bool nodeProcessed() const { return _processed; }
    void setNodeType(NodeType t) { _type = t; }
    SceneRender* currentRenderer() const { return _current_renderer; }

 private:
    SceneNode*   _parent;
    SceneNode*   _firstchild;
    SceneNode*   _lastchild;
    SceneNode*   _nextsibling;
    SceneNode*   _previoussibling;
    SceneRender* _current_renderer;
    
    unsigned int _position;
    int          _children;
    bool         _processed;
    NodeType     _type;

    friend class SceneRender;
    friend class MRender;
    friend class EffectNode;
};

class Item2DInterface: public QObject
{
 public:
    Item2DInterface();
    virtual QTransform transform() const = 0;
    virtual bool isVisible() = 0;
    virtual bool hasAlpha() = 0;
    virtual qreal opacity() = 0;
    
    void setFboContainer(bool f) { _fbo_container = f; }
    bool fboContainer() { return _fbo_container; }

    void setBlendFunc(GLenum src, GLenum dest);
    const BlendFunc& blendFunc() const { return _blendfunc; }

 private:
    bool      _fbo_container;
    BlendFunc _blendfunc;
};

class TransformNode: public SceneNode
{
 public:
    TransformNode(const QMatrix4x4&);
    void setTransform(const QMatrix4x4& t) { _transform = t; }
    const QMatrix4x4& transform() const { return _transform; }
    SceneRender::WalkNode processNode();
    
    void setTransformHandler(Item2DInterface* h) {  _transform_handler = h; }

    /* Fbo rendering flags */
    void setInFbo(bool fbo) { _in_fbo = fbo; }
    bool inFbo() { return _in_fbo; }
    
 private:
    QMatrix4x4       _transform;
    bool             _in_fbo;
    QPointer<Item2DInterface> _transform_handler;
};

class EffectInterface: public QObject
{
 public:
    virtual void currentNodeProcessed(SceneNode* node) = 0;
    virtual void setUniforms(QGLShaderProgram* program) = 0;
};

class GeometryNode: public SceneNode
{
 public:

    GeometryNode(const QRectF&);
    SceneRender::WalkNode processNode();
    
    // TODO: vbo upload
    void setGeometry(const QRectF& geometry) { _geometry = geometry; }
    void setTextureCoords(const TextureCoords& coords);
    const TextureCoords& texcoords() const;

    const QRectF& geometry() const { return _geometry; }

    qreal opacity() const { return _opacity; }
    void setOpacity(qreal opacity) { _opacity = opacity; }
        
    bool inheritTransform() { return _inherit_transform; }
    void setInheritTransform(bool inherit) { _inherit_transform = inherit; }
    
    GLuint texture() { return _texture_id; }
    void setTexture(GLuint texture) { _texture_id = texture; }
    void setVisible(bool b) { _visible = b; }
    
    void setShaderId(GLuint id) { _shader_id = id; }
    GLuint shaderId() { return _shader_id; }

    /* this node has translucent pixels */
    void setAlpha(bool a) { _has_alpha = a; }
    bool hasAlpha() const { return _has_alpha; }
    
    void setUniformHandler(EffectInterface* e) { _uniformhandler = e; }
    EffectInterface* uniformHandler() { return _uniformhandler; }
    void setUniforms(QGLShaderProgram* program);

    const BlendFunc& blendFunc() const { return _blendfunc; }

    void setProcessedByEffect(bool processed) { _effect_processed = processed; }
    bool effectProcessed() { return _effect_processed; }
    
 private:
    QRectF           _geometry;
    TextureCoords    _texcoords;
    GLuint           _vboId;
    GLuint           _texture_id;
    GLuint           _shader_id;
    qreal            _opacity;
    bool             _has_alpha;
    bool             _visible;
    bool             _inherit_transform;
    bool             _effect_processed;
    qreal            _z_value;
    BlendFunc        _blendfunc;
    QPointer<EffectInterface> _uniformhandler;

    friend class SgManager;
    friend class EffectNode;
};

class EffectNode: public SceneNode
{
 public:

    EffectNode();
    SceneRender::WalkNode processNode();
    void setPosition(unsigned int);
    void setEffectHandler(EffectInterface* e) { effect = e; }
    EffectInterface* effectHandler() const { return effect; }

 private:
    /* Not accessible on purpose due to having multiple parents! */
    void detach() { /*NOOP*/ };

    EffectInterface* effect;
};

#endif
