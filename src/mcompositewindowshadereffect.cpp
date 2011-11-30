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

/*!
  \class MCompositeWindowShaderEffect
  \brief MCompositeWindowShaderEffect is the base class for shader effects on windows
  
  Shader effects can change the appearance of composited windows by hooking
  into the rendering pipeline of the compositor. It allows manipulation
  of how a composited window is rendered by using custom GLSL fragment shaders
  and direct access to the texture rendering function.  
  An effect can be disabled by calling setEnabled(false). If effects are 
  disabled, the window texture is rendered directly.
  
  To add special effects to composited windows, subclass MCompositeWindowShaderEffect 
  and reimplement the pure virtual function drawTexture()
*/

#include "mtexturepixmapitem.h"
#include "msplashscreen.h"
#include "mcompositewindowshadereffect.h"
#include "mtexturepixmapitem_p.h"
#include "mcompositewindowgroup.h"
#include "mrender.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/scenerender.h"

#include <QByteArray>

/* \cond
 * Internal class. Do not use! Not part of public API
 */
class MCompositeWindowShaderEffectPrivate: public EffectInterface
{
 public:    
    void currentNodeProcessed(SceneNode*);

    void setUniforms(QGLShaderProgram* program);
    ~MCompositeWindowShaderEffectPrivate();

 private:
    explicit MCompositeWindowShaderEffectPrivate(MCompositeWindowShaderEffect*);
    
    MCompositeWindowShaderEffect* effect;
    MTexturePixmapPrivate* priv_render;
    MCompositeWindow *comp_window;
    QVector<GLuint> pixfrag_ids;
    QVector<GeometryNode*> custom_quads;
    // Windows nodes where this effect is hooked
    QVector<GeometryNode*> hooked_windows;
    GLuint active_fragment;
    EffectNode* effect_node;
    GeometryNode* current_node;
    
    bool enabled;

    friend class MCompositeWindowShaderEffect;
};

/* \endcond */

static const char default_frag[] = "\
    lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords) { \
        return texture2D(imageTexture, textureCoords); \
    }\n";

MCompositeWindowShaderEffectPrivate::MCompositeWindowShaderEffectPrivate(MCompositeWindowShaderEffect* e)
    :effect(e),
     priv_render(0),
     comp_window(0),
     active_fragment(0),
     current_node(0)
{
    effect_node = new EffectNode();
    effect_node->setEffectHandler(this);
}

MCompositeWindowShaderEffectPrivate::~MCompositeWindowShaderEffectPrivate()
{
    MRender::freeNode(effect);
    delete effect_node;
}

void MCompositeWindowShaderEffectPrivate::currentNodeProcessed(SceneNode* n)
{
    current_node = static_cast<GeometryNode*> (n);
    current_node->setShaderId(active_fragment);
    effect->render();
}


void MCompositeWindowShaderEffectPrivate::setUniforms(QGLShaderProgram* program)
{
    effect->setUniforms(program);
}

/*!
 * Creates a window effect object
 */
MCompositeWindowShaderEffect::MCompositeWindowShaderEffect(QObject* parent)
    :QObject(parent),
     d(new MCompositeWindowShaderEffectPrivate(this))
{    
    // install default pixel shader
    QByteArray code = default_frag;
    d->active_fragment = installShader(code);
}

/*!
  Destroys the graphics effect and deletes GLSL fragment programs from
  the compositor's rendering engine if any are installed from this effect
*/
MCompositeWindowShaderEffect::~MCompositeWindowShaderEffect()
{
    delete d;
}

/*!
  Adds and installs the source code for a pixel shader fragment to \a code.
  
  The \a code must define a GLSL function with the signature
  \c{lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords)}
  that returns the source pixel value to use in the paint engine's
  shader program.  The following is the default pixel shader fragment,
  which draws a texture with no effect applied:
  
  \code
  lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords)
  {
      return texture2D(imageTexture, textureCoords);
  }
  \endcode
  
  \return The id of the fragment shader. If the fragment shader is invalid,
  return 0.
  
  \sa setUniforms()
*/   
GLuint MCompositeWindowShaderEffect::installShader(const QByteArray& fragment,
                                                   const QByteArray& vertex)
{
    return SceneRender::installPixelShader(fragment, vertex);
}

const QVector<GLuint>& MCompositeWindowShaderEffect::fragmentIds() const
{
    return d->pixfrag_ids;
}

/*!
  Sets the rendering pipeline to use shader fragment specified by \a id 
*/
void MCompositeWindowShaderEffect::setActiveShaderFragment(GLuint id)
{
    d->active_fragment = id;
}

/*!
  \return The id of the currently active shader fragment
*/
GLuint MCompositeWindowShaderEffect::activeShaderFragment() const
{
    return d->active_fragment;
}

/*!
  \fn void MCompositeWindowShaderEffect::enabledChanged( bool enabled);
  Emmitted if this effect gets disabled or enabled
*/

/*!
  \fn virtual void MCompositeWindowShaderEffect::drawTexture(const QTransform &transform, const QRectF &drawRect, qreal opacity) = 0;

  This pure virtual function is called whenever the windows texture is
  rendered. Reimplement it to completely customize how the texture of this
  window is rendered using the given transformation 
  matrix \a transform, texture geometry \a drawRect, and \a opacity.
*/

/*!
  Install this effect on a composite window object \a window. Note that
  we override QGraphicsItem::setGraphicsEffect() because
  we have a completely different painting engine.
  If a window has a previous effect, it will be overriden by the new one
*/
void MCompositeWindowShaderEffect::installEffect(MCompositeWindow* window)
{
    if (!window || (!window->isValid() && 
                    (window->type() != MCompositeWindowGroup::Type)))
        return;
    
    if (d->comp_window != window) 
        d->comp_window = window;

#ifdef QT_OPENGL_LIB
    if (window) {
        MRender::addNode(this, window);
    }
#endif
}

/*!
  Remove this effect from a composite window object \a window. After removing
  the effect the window will use default shaders.
*/
void MCompositeWindowShaderEffect::removeEffect(MCompositeWindow* window)
{    
#ifdef QT_OPENGL_LIB
    if (window) {
        d->current_node = static_cast<GeometryNode*>(window->item_node.second);
        if (d->current_node) {
            
            setWindowGeometry(window->boundingRect());
            // reset shaders
            d->current_node->setShaderId(0);
        }
        MRender::clearNode(window);
    }
#endif
}

void MCompositeWindowShaderEffect::compWindowDestroyed()
{
    // sender() is half-destroyed, can't call any non-QObject methods on it,
    // so rather than remofeEffect()ing it, just clear @d->comp_window.
    d->comp_window = 0;
}

/*!
  \return Whether this effect is enabled or not
*/
bool MCompositeWindowShaderEffect::enabled() const
{
    return d->enabled;
}

/*!
  Enable or disable this effect 
*/
void MCompositeWindowShaderEffect::setEnabled(bool enabled)
{
    d->enabled = enabled;
    emit enabledChanged(enabled);
}

/*!
  Set the uniform values on the currently active shader \a program.
  Default implementation does nothing. Reimplement this function to
  set values if you specified a custom pixel shader in your effects.
*/
void MCompositeWindowShaderEffect::setUniforms(QGLShaderProgram* program)
{
    Q_UNUSED(program);
}

/*!
  \return The current window where this effect is active
*/
MCompositeWindow* MCompositeWindowShaderEffect::currentWindow()
{
    return d->comp_window;
}

EffectNode* MCompositeWindowShaderEffect::effectNode() const
{
    return d->effect_node;
}

/* Primitives */
int MCompositeWindowShaderEffect::addQuad(GLuint textureId, 
                                          const QRectF& geometry,
                                          const TextureCoords& texcoords,
                                          bool parent_transform,
                                          bool has_alpha,
                                          GLuint fragshaderId)
{
    return MRender::addNode(this, geometry, texcoords,
                              parent_transform, has_alpha, textureId);
}

QVector<GeometryNode*>& MCompositeWindowShaderEffect::customQuads()
{
    return d->custom_quads;
}

void MCompositeWindowShaderEffect::setQuadVisible(int id, bool visible)
{
    GeometryNode* node = d->custom_quads.value(id, 0);
    if (node)
        node->setVisible(visible);
}

void MCompositeWindowShaderEffect::setQuadGeometry(int id, const QRectF& geometry)
{
    GeometryNode* node = d->custom_quads.value(id, 0);
    if (node && (node->geometry() != geometry))
        node->setGeometry(geometry);
}

const QRectF& MCompositeWindowShaderEffect::quadGeometry(int id)
{
    static QRectF r;

    GeometryNode* node = d->custom_quads.value(id, 0);
    if (node)
        return node->geometry();
    
    return r;
}

int MCompositeWindowShaderEffect::addQuad(GeometryNode* node)
{
    d->custom_quads.append(node);
    return d->custom_quads.indexOf(node);
}

void MCompositeWindowShaderEffect::render()
{
    /*NOOP*/
    //qDebug() << __func__;
}

void MCompositeWindowShaderEffect::setWindowGeometry(const QRectF& geometry,
                                                     Qt::AspectRatioMode mode)
{
    if (!d->current_node || d->current_node->geometry() == geometry)
        return;
 
    MRender::setNodeGeometry(d->current_node, geometry, mode);
}

void MCompositeWindowShaderEffect::hookWindowNode(GeometryNode* node)
{
    d->hooked_windows.append(node);
}
