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

/*!
  \class MRender
  \brief MRender is the public interface for MCompositeWindow/QGraphicsItem
  to the scenegraph.
  
  MRender replaces the Graphics View Framework with a hardware Z-buffered 
  scenegraph renderer that is optimized for OpenGL/ES2 while retaining 
  compatibility with existing QGraphicsItem objects.
  MRender exports a public interface to MCompositeWindow objects as nodes
  to the scenegraph in addition to managing an object's shader effect.

  When using MRender, instantiate MGraphicsView object instead of QGraphicsView
  as container for your items.
*/

#include "mrender.h"
#include "scenegraph/scenenode.h"
#include <mtexturepixmapitem.h>
#include <mcompositewindowshadereffect.h>
#include <QtDebug>
#include <QX11Info>

#include <QTimer>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/sync.h>

//#define CONSOLE_DEBUG

static XSyncValue consumed;
static XSyncValue ready;

// root of all creation
static SceneNode* root = 0;
static bool clear_scene = false;

#ifdef DEBUG_SCENEGRAPH
static ExternalRenderDebug external_debugger = 0;
static QHash<GLuint, MCompositeWindow*> textured_windows;
static void render_debug(void* msg)
{
    GLuint* tex = static_cast<GLuint*>(msg);
    if (!tex)
        return;
    MCompositeWindow* w = textured_windows.value(*tex, 0);
    if (!w)
        return;
    if (!external_debugger)
        return;
    external_debugger(w);
}

#endif

MGLWidget::MGLWidget(const QGLFormat & format)
    :QGLWidget(format),
     prev_counter(0),
     swap_counter(0)
{
    setAutoBufferSwap(false);
    XSyncIntToValue(&consumed, COMPOSITE_PIXMAP_CONSUMED);
    XSyncIntToValue(&ready, COMPOSITE_PIXMAP_READY);
}
    
void MGLWidget::swapBuffers()
{
#ifdef HAVE_XSYNC
    // trigger the client swapbuffers
    if (prev_counter != swap_counter) {
        XSyncSetCounter(QX11Info::display(), prev_counter, consumed);
    }
    XSyncSetCounter(QX11Info::display(), swap_counter, consumed);
    
#endif
    QGLWidget::swapBuffers();
}

void MGLWidget::setSwapCounter(Qt::HANDLE swapcounter)
{
    if (swap_counter == swapcounter)
        return;
    prev_counter = swap_counter;
    swap_counter = swapcounter;
}

MGraphicsView::MGraphicsView(QGraphicsScene * scene, QWidget * parent )
    :QGraphicsView(scene,parent)
{
}

void MGraphicsView::paintEvent(QPaintEvent*)
{    
#if (defined DEBUG_SCENEGRAPH && defined CONSOLE_DEBUG)     
    static QTime t;
    t.start();
#endif
    MRender::renderScene();
    
#if (defined DEBUG_SCENEGRAPH && defined CONSOLE_DEBUG) 
    static int el = 0;
    static int avg = 0;
    avg++;
    el += t.elapsed();
    
    static int meanavg = 0;
    static qreal score = 0;
    
    if (avg == 50) {
        qDebug() << "average ms spent / frame:" << el / avg << " | frames" << avg
                 << "| ms elapsed since timer start():" << el;
        score +=  el / avg; 
        avg = 0;
        el = 0;
        meanavg++;
    }
    
    if (meanavg == 20) {
        qDebug() << "== mean" << score / meanavg;
        meanavg = 0;
        score = 0;    
    }
#endif    
    MGLWidget* glw = (MGLWidget*) viewport();
    glw->swapBuffers();
}

/*!
 * Adds composite window \item as a node in the graph
 */
void MRender::addNode(MCompositeWindow* item, Item2DInterface* i)
{
    // Support for nested QGraphicItems unnecessary for compositor use case. 
    // For now, we assume that all MCompositeWindow(s) will be toplevel items
    if (item->parentItem() || item->item_node.first || item->item_node.second) {
        qWarning("MRender::%s(): error adding node", __func__);
        return;
    }
    if (!root)
        root = new SceneNode();
    TransformNode* t = new TransformNode(QMatrix4x4());
    GeometryNode*  g = new GeometryNode(item->boundingRect());
    g->setTexture(item->texture());
    if (i->fboContainer()) {
        TextureCoords tc;
        tc.setOrientation(TextureCoords::StartBottomLeft | 
                          TextureCoords::RotateCW);
        g->setTextureCoords(tc);
    }     
    
    t->setTransformHandler(i);
    t->appendChild(g);
    item->item_node.first = t;
    item->item_node.second = g;
    root->appendChild(t);
#if (defined DEBUG_SCENEGRAPH && defined CONSOLE_DEBUG)
    qDebug("MRender::%s() Window: %d Texture: %d", __func__, 
           item->window(), item->texture());
#elif defined DEBUG_SCENEGRAPH
    textured_windows[item->texture()] = item;
#endif
}

/*!
 * Helper function to map the composite window's Z value to its node's 
 * position in the scenegraph.
 */
void MRender::setNodePosition(MCompositeWindow* item, qreal zvalue)
{
    SceneNode* gnode = item->item_node.second;
    if (gnode) {
        gnode->setPosition(zvalue);
        // Effects
        if (gnode->_firstchild && 
            (gnode->_firstchild->nodeType() == SceneNode::Effect))
            gnode->_firstchild->setPosition(zvalue);
    }
}

/*!
 * Adds a graphics effect as a child node of composite window \item
 */
void MRender::addNode(MCompositeWindowShaderEffect* effect,
                      MCompositeWindow* item)
{
    GeometryNode* g = static_cast<GeometryNode*> (item->item_node.second);
    if (!g)
        return;
    
    // parent has no children
    Q_ASSERT(!g->_firstchild && !_g->_lastchild);  
    if (!g->_firstchild && !g->_lastchild) {
        EffectNode* e = effect->effectNode();
        g->appendChild(e);
        // EffectNode's share position with its parent
        e->_position = g->nodePosition();
        
        // Uniform handler;
        g->setUniformHandler(e->effectHandler());
        g->setProcessedByEffect(false);
        effect->hookWindowNode(g);
    }
}

/*! Adds a node within an effect node */
int MRender::addNode(MCompositeWindowShaderEffect* effect,
                     const QRectF& geometry,
                     const TextureCoords& texcoords,
                     bool parent_transform,
                     bool has_alpha,
                     GLuint texture)
{
    GeometryNode* g = new GeometryNode(geometry);
    g->setInheritTransform(parent_transform);
    g->setTexture(texture);
    g->setTextureCoords(texcoords);
    g->setAlpha(has_alpha);
    effect->effectNode()->appendChild(g);
    g->_position = effect->effectNode()->nodePosition();
#if (defined DEBUG_SCENEGRAPH && defined CONSOLE_DEBUG)
    qDebug("MRender::%s() Effect Texture: %d", __func__, g->texture());
#endif
    
    return effect->addQuad(g);
}

/*!
  Sets the geometry of \a geometrynode to \a geometry. The scaling aspect ratio
  mode can be set by \mode
 */
void MRender::setNodeGeometry(GeometryNode* geometrynode, 
                              const QRectF& geometry,
                              Qt::AspectRatioMode mode)
{
    if (mode == Qt::KeepAspectRatio) {
        QRectF oldgeom = geometrynode->geometry();
        TextureCoords::Orientation old_t = geometrynode->texcoords().textureOrientation();
        TextureCoords texcoords(QRectF(geometry.left()  / oldgeom.right(),
                                       geometry.top()   / oldgeom.bottom(),
                                       geometry.right() / oldgeom.right(),
                                       geometry.bottom()/ oldgeom.bottom()));
        
        texcoords.setOrientation(old_t);
        geometrynode->setTextureCoords(texcoords);
    } else
        geometrynode->setTextureCoords(QRectF(0,0,1,1));
    geometrynode->setGeometry(geometry);
}

/*!
  Sets the window geometry of \a item to \a geometry. The scaling aspect ratio
  mode can be set by \mode
 */
void MRender::setWindowGeometry(MCompositeWindow* item, 
                                  const QRectF& geometry,
                                  Qt::AspectRatioMode mode)
{
    GeometryNode* g = static_cast<GeometryNode*> (item->item_node.second);
    if (!g)
        return;
    setNodeGeometry(g, geometry, mode);
}

/*!
  Instructs the renderer that whether \a item should be rendered inside an
  fbo or not
 */
void MRender::setFboRendered(MCompositeWindow* item, bool in_fbo)
{
    TransformNode* t = static_cast<TransformNode*> (item->item_node.first);
    if (t)
        t->setInFbo(in_fbo);
}

/*!
  Frees the node resources associated with MCompositeWindow \a item
 */
void MRender::freeNode(MCompositeWindow* item)
{
#ifdef DEBUG_SCENEGRAPH
    GeometryNode* g = static_cast<GeometryNode*> (item->item_node.second);
    if (g)
        textured_windows.remove(g->texture());
#endif
    
    if (item->item_node.first) 
        delete item->item_node.first;
    if (item->item_node.second)
        delete item->item_node.second;
}

/*!
  Frees the node resources associated with MCompositeWindowShaderEffect 
  \a effect
 */
void MRender::freeNode(MCompositeWindowShaderEffect* effect)
{
    qDeleteAll(effect->customQuads().begin(), effect->customQuads().end());
    effect->customQuads().clear();
}

/*!
  Remove children nodes associated with MCompositeWindow \a item. Child nodes
  are not freed.
  \a effect
 */
void MRender::clearNode(MCompositeWindow* item)
{    
    GeometryNode* g = static_cast<GeometryNode*> (item->item_node.second);
    if (!g)
        return;
    g->clearChildren();
}

/*!
  Renders the entire scene. The default is render to the framebuffer. If 
  \a in_fbo is true, render's the entire scene on an fbo instead 
 */
void MRender::renderScene(bool in_fbo)
{
    if (!root)
        return;

    static SceneRender s;
    s.setCleared(clear_scene);
    s.setFboRender(in_fbo);
#ifdef DEBUG_SCENEGRAPH
    s.setNodeDebugFilter(render_debug);
#endif
    s.renderScene(root);
}

/*!
  Do not render anything and clear the scene to black
 */
void MRender::setClearedScene(bool cleared)
{
    clear_scene = cleared;
}

/*!
  /return Whether the scene is cleared and not rendering anything
 */
bool MRender::isClearedScene()
{
    return clear_scene;
}

#ifdef DEBUG_SCENEGRAPH

/*! 
  This function is used for purely for debugging purposes only. Sets an external
  function \a f that gets called everytime a MCompositeWindow is rendered.

  ExternalRenderDebug is a typedef for a function with the signature
  void customfilter(MCompositeWindow* window);
  
*/
void MRender::setNodeDebugFilter(ExternalRenderDebug f)
{
    external_debugger = f;
}
#endif
