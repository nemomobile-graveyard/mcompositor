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
  \class MCompositeWindowGroup
  \brief MCompositeWindowGroup allows a collection of windows to be rendered
  as a single texture.
  
  This class is useful for rendering a list of windows that needs to 
  be treated as one item with transformations applied only to a single texture.
  Unlike QGraphicsItemGroup where each item is rendered separately for each
  frame, MCompositeWindowGroup can pre-render all items to an off-screen buffer 
  before a frame starts and can render the resulting texture afterwards as a 
  single quad which reduces GPU load and helps performance especially in panning
  and scaling transformations. 
  
  Use this class to render a main window with its transient windows. Another
  use case is for a window that needs to have similar transformations to a
  main window. The addChildWindow() function adds windows that need to 
  animate synchronously  with the main window. The removeChildWindow() function
  removes a window from the group.  

  Implementation is hw-dependent. It relies on framebuffer objects on GLES2 
  and the GL_EXT_framebuffer_object extension on the desktop.
*/

#include <QList>
#include "mcompositewindowgroup.h"

#include "mtexturepixmapitem.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mrender.h"

#ifdef GLES2_VERSION
#define FORMAT GL_RGBA
#define DEPTH GL_DEPTH_COMPONENT16
#else
#define FORMAT GL_RGBA8
#define DEPTH GL_DEPTH_COMPONENT
#endif


class MCompositeWindowGroupPrivate: public Item2DInterface
{
public:
    MCompositeWindowGroupPrivate(MTexturePixmapItem* mainWindow, 
                                 MCompositeWindowGroup* wg);

    ~MCompositeWindowGroupPrivate();
    QTransform transform() const;
    virtual bool isVisible();
    virtual bool hasAlpha();
    virtual qreal opacity();

private:
    QPointer<MTexturePixmapItem> main_window;
    GLuint texture;
    QGLFramebufferObject* fbo;
    GLuint depth_buffer;
    
    bool valid;
    QList<MTexturePixmapItem*> item_list;
    QPointer<MCompositeWindowGroup> group;
   
    friend class MCompositeWindowGroup;
};

MCompositeWindowGroupPrivate::MCompositeWindowGroupPrivate(MTexturePixmapItem* mw,
                                                           MCompositeWindowGroup* wg)
    :main_window(mw),
     texture(0),
     fbo(0),
     depth_buffer(0),
     valid(false),
     group(wg)
{               
    MRender::setFboRendered(main_window, true);
    setFboContainer(true);
}

MCompositeWindowGroupPrivate::~MCompositeWindowGroupPrivate()
{
    delete fbo;
}

QTransform MCompositeWindowGroupPrivate::transform() const
{
    if (group)
        return group->sceneTransform();
    return QTransform();
}
    
bool MCompositeWindowGroupPrivate::isVisible()
{
    if (group)
        return group->isVisible();
    return false;
}

bool MCompositeWindowGroupPrivate::hasAlpha()
{
    // never render FBOs themselves as translucent unless
    // we have a good reason!
    return false;
}

qreal MCompositeWindowGroupPrivate::opacity()
{
    return 1.0;
}

/*!
  Creates a window group object. Specify the main window
  with \a mainWindow
 */
MCompositeWindowGroup::MCompositeWindowGroup(MTexturePixmapItem* mainWindow)
    :MCompositeWindow(0, MWindowDummyPropertyCache::get()),
     d_ptr(new MCompositeWindowGroupPrivate(mainWindow, this))
{    
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->scene()->addItem(this);
    
    init();
    setZValue(mainWindow->zValue());
    
    MRender::addNode(this, d_ptr.data());
    stackBefore(mainWindow);
}

/*!
  Destroys this window group and frees resources. All windows that are within 
  this group revert back to directly rendering its own texture.
 */
MCompositeWindowGroup::~MCompositeWindowGroup()
{
    Q_D(MCompositeWindowGroup);
    
    if (!QGLContext::currentContext()) {
        qWarning("MCompositeWindowGroup::%s(): no current GL context",
                 __func__);
        return;
    }
    
    if (d->main_window)
        MRender::setFboRendered(d->main_window, false);
    
    // if stacking is dirty, stack windows now, otherwise we paint the scene
    // according to the old stacking
    MCompositeManager *p = (MCompositeManager*)qApp;
    if (p->d->stacking_timer.isActive())
        p->d->stackingTimeout();
}

void MCompositeWindowGroup::init()
{
    Q_D(MCompositeWindowGroup);
    
    QGLContext* ctx = const_cast<QGLContext*>(QGLContext::currentContext());
    if (!ctx || !d->main_window) {
        qWarning("MCompositeWindowGroup::%s(): no current GL context",
                 __func__);
        d->valid = false;
        return;
    }
    ctx->makeCurrent();
    d->fbo = new QGLFramebufferObject(d->main_window->boundingRect().size().toSize(),
                                      QGLFramebufferObject::Depth);    
}

static bool behindCompare(MTexturePixmapItem* a, MTexturePixmapItem* b)
{
    return a->indexInStack() > b->indexInStack();
}

/*!
  Adds the given \a window to this group except if it's already in the group.
  The window's transformations will remain unmodified.  Returns whether the
  \a window has been added.  You're supposed to call updateWindowPixmap()
  when you're finished adding children.
 */
bool MCompositeWindowGroup::addChildWindow(MTexturePixmapItem* window)
{
    Q_D(MCompositeWindowGroup);
    if (d->item_list.contains(window) || !d->main_window)
        return false;

    MRender::setFboRendered(window, true);
    connect(window, SIGNAL(destroyed()), SLOT(q_removeWindow()));

    if (d->item_list.isEmpty())
        d->main_window->stackBefore(window);
    else
        d->item_list.last()->stackBefore(window);
    
    d->item_list.append(window);
    
    // ensure group windows are already stacked in proper order in advance
    // for back to front rendering. Could use depth buffer attachment at some 
    // point so this might be unecessary
    qSort(d->item_list.begin(), d->item_list.end(), behindCompare);

    return true;
}

/*!
  Removes the given \a window to from group. 
 */
void MCompositeWindowGroup::removeChildWindow(MTexturePixmapItem* window)
{
    Q_D(MCompositeWindowGroup);
    
    MRender::setFboRendered(window, false);
    d->item_list.removeOne(window);
    disconnect(window, SIGNAL(destroyed()), this, SLOT(q_removeWindow()));
}

MCompositeWindow *MCompositeWindowGroup::topWindow() const
{
    static const QRegion fs_r(0, 0,
                    ScreenOfDisplay(QX11Info::display(),
                        DefaultScreen(QX11Info::display()))->width,
                    ScreenOfDisplay(QX11Info::display(),
                        DefaultScreen(QX11Info::display()))->height);
    Q_D(const MCompositeWindowGroup);
    MCompositeWindow *cw = 0;
    for (int i = d->item_list.size() - 1; i >= 0; --i) {
        cw = d->item_list[i];
        if (cw->propertyCache()->isDecorator() ||
            cw->propertyCache()->isOverrideRedirect() ||
            cw->propertyCache()->windowType() == MCompAtoms::INPUT ||
            !fs_r.subtracted(cw->propertyCache()->shapeRegion()).isEmpty()) {
            cw = 0;
            continue;
        }
        break;
    }
    if (!cw)
        cw = d->main_window;
    return cw;
}

void MCompositeWindowGroup::q_removeWindow()
{
    Q_D(MCompositeWindowGroup);
    d->item_list.removeOne(static_cast<MTexturePixmapItem*>(sender()));
}

void MCompositeWindowGroup::saveBackingStore() {}

void MCompositeWindowGroup::resize(int , int) {}

void MCompositeWindowGroup::clearTexture() {}

bool MCompositeWindowGroup::isDirectRendered() const { return false; }

QRectF MCompositeWindowGroup::boundingRect() const 
{
    Q_D(const MCompositeWindowGroup);
    return d->main_window ? d->main_window->boundingRect() : QRectF();
}

void MCompositeWindowGroup::paint(QPainter* painter, 
                                  const QStyleOptionGraphicsItem* options, 
                                  QWidget* widget) 
{    
    Q_D(MCompositeWindowGroup);
    Q_UNUSED(options)
    Q_UNUSED(widget)
}

void MCompositeWindowGroup::windowRaised()
{
}

// Renders @main_window and all children into an FBO.
// Unredirects all involved windows.
void MCompositeWindowGroup::updateWindowPixmap(XRectangle *rects, int num,
                                               Time t)
{
    Q_UNUSED(rects)
    Q_UNUSED(num)
    Q_UNUSED(t)
    Q_D(MCompositeWindowGroup);

    if (!d->main_window)
        return;
    if (d->main_window->isWindowTransitioning()) {
        // updates during transitioning cause issues when texcoords_from_rect
        // is used in MTexturePixmapItemPrivate and is heavy, too
        return;
    }
    if (!d->fbo->bind()) {
        qDebug() << "invalid fbo";
        return;
    }

   
    // The redirection method is expected not to play with GL_FRAMEBUFFER.
    d->main_window->enableRedirectedRendering();
    for (int i = 0; i < d->item_list.size(); ++i) {
        MTexturePixmapItem* item = d->item_list[i];
        item->enableRedirectedRendering();
    }
    
    MRender::renderScene(true);
    d->fbo->release();
}

// internal re-implementation from MCompositeWindow
MTexturePixmapPrivate* MCompositeWindowGroup::renderer() const
{
    return 0;
}

/*!
  \return Returns the texture used by the off-screen buffer
*/
GLuint MCompositeWindowGroup::texture()
{
    Q_D(MCompositeWindowGroup);
    return d->fbo->texture();
}

