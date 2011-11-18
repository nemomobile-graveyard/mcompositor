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

#ifndef MRENDER_H
#define MRENDER_H

#include <QRectF>
#include <QGLWidget>
#include <QGraphicsView>

class MCompositeWindowShaderEffect;
class MCompositeWindow;
class Item2DInterface;
class GeometryNode;
class TextureCoords;

#ifdef HAVE_XSYNC
#define COMPOSITE_PIXMAP_READY    0x5000
#define COMPOSITE_PIXMAP_CONSUMED 0x5001
#define EXPLICIT_RESET 0x5002
#endif

class MGLWidget: public QGLWidget
{
    Q_OBJECT
 public:
    MGLWidget(const QGLFormat & format);

    void setSwapCounter(Qt::HANDLE swapcounter);
    Qt::HANDLE swapCounter() { return swap_counter; }
    void swapBuffers();

 private:
    Qt::HANDLE prev_counter;
    Qt::HANDLE swap_counter;
};

class MGraphicsView: public QGraphicsView
{
 public:
    MGraphicsView(QGraphicsScene * scene, QWidget * parent = 0 );
    
 protected:
    virtual void paintEvent ( QPaintEvent * event );
};

class MRender
{
 public:
    /* node operations */
    static void setFboRendered(MCompositeWindow* item, bool in_fbo);
    static void addNode(MCompositeWindow* item, Item2DInterface* i);
    static void addNode(MCompositeWindowShaderEffect* effect,
                        MCompositeWindow* item);
    static int addNode(MCompositeWindowShaderEffect* effect, 
                       const QRectF& geometry, 
                       const TextureCoords& texcoords,
                       bool parent_transform,
                       bool has_alpha,
                       GLuint texture);
    static void setNodePosition(MCompositeWindow* item, qreal zvalue);
    static void setNodeGeometry(GeometryNode* geometrynode, 
                                const QRectF& geometry,
                                Qt::AspectRatioMode mode);
    static void setWindowGeometry(MCompositeWindow* item, 
                                  const QRectF& geometry,
                                  Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio);
    static void freeNode(MCompositeWindow* item);
    static void freeNode(MCompositeWindowShaderEffect* effect);
    static void clearNode(MCompositeWindow* item);
    
    /* rendering */
    static void renderScene(bool in_fbo = false);
    static void setClearedScene(bool cleared);
    static bool isClearedScene();
};

#endif
