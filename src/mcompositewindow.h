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

#ifndef DUICOMPOSITEWINDOW_H
#define DUICOMPOSITEWINDOW_H

#include <QGraphicsItem>
#include <QtOpenGL>
#include <QPointer>
#include <X11/Xutil.h>
#include <mwindowpropertycache.h>

class MCompWindowAnimator;
class MTexturePixmapPrivate;
class MCompositeWindowGroup;
class MCompositeWindowAnimation;
class McParallelAnimation;

/*!
 * This is the base class for composited window items. It provided general
 * functions to animate and save the state of individual items on the
 * composited desktop. Also provides functionality for thumbnailed view of
 * the contents.
 */
class MCompositeWindow: public QObject, public QGraphicsItem
{
    Q_OBJECT
#if QT_VERSION >= 0x040600
    Q_INTERFACES(QGraphicsItem)
    Q_PROPERTY(QPointF pos READ pos WRITE setPos)
    Q_PROPERTY(qreal scale READ scale WRITE setScale)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
#endif
public:
    
    enum WindowStatus {
        Normal = 0,
        Hung,
        Minimizing,
        Closing
    };
    enum IconifyState {
        NoIconifyState = 0,
        ManualIconifyState,
        TransitionIconifyState
    };

    /*! Construct a MCompositeWindow
     *
     * \param window id to the window represented by this item
     * \param mpc property cache object for this window
     * \param item QGraphicsItem parent, defaults to none
     */
    MCompositeWindow(Qt::HANDLE window, MWindowPropertyCache *mpc,
                     QGraphicsItem *item = 0);

    /*! Destroys this MCompositeWindow and frees resources
     *
     */
    virtual ~MCompositeWindow();

    virtual Qt::HANDLE window() const { return win_id; }

    // Reimplemented to defer deleteLater()s until transitions are over.
    virtual bool event(QEvent *);

    /*!
     * Iconify window with animation. If deferAnimation is set to true
     * call startTransition() manually. Returns true if signal will come.
    */
    bool iconify(bool deferAnimation = false);

    /*!
     * Sets whether or not the window is obscured and generates the
     * proper VisibilityNotify for the window.
     */
    void setWindowObscured(bool obscured, bool no_notify = false);

    /*!
     * Returns whether this item is iconified or not
     */
    bool isIconified() const;

    /*!
     * Request a Z value for this item. Useful if this window is still animating
     * and setting the zValue prevents the animation from being displayed.
     * This function defers the setting of the zValue until the animation for
     * this item is done.
     *
     * \param zValue the requested z-value
     *
     * TODO: this might be redundant. Move to overriden setZValue in the future
     */
    void requestZValue(int zValue);

    /*!
     * Overriden setVisible so we have complete control of the item
     */
    void setVisible(bool visible);

    /*!
     * Own visibility getter. We can't rely on QGraphicsItem's visibility
     * information because of rendering optimizations.
     */
    bool windowVisible() const;

    /*!
     * Set iconify status manually.
     */
    void setIconified(bool iconified);

    /*!
     * Set scale, opacity etc. to normal values.
     */
    void setUntransformed();

    /*!
     * Returns how this window was iconified.
     */
    IconifyState iconifyState() const;

    /*!
     * Sets how this window was iconified.
     */
    void setIconifyState(IconifyState state);

    /*!
     * Returns true if this window needs a decoration
     */
    bool needDecoration() const;

    /*!
     * Returns true if this window needs compositing
     */
    bool needsCompositing() const;

    /*!
     * Sets whether this window is decorated or not
     */
    void setDecorated(bool decorated);

    void setIsMapped(bool mapped);
    bool isMapped() const;
    
    void setNewlyMapped(bool newlyMapped) { newly_mapped = newlyMapped; }
    bool isNewlyMapped() const { return newly_mapped; }

    /*!
     * Restores window with animation. If deferAnimation is set to true
     * call startTransition() manually.
    */
    void restore(bool deferAnimation = false);

    /*!
     * Overrides QGraphicsItem::update() so we have complete control of item
     * updates.
     */
    static void update();

    bool blurred();

    /*!
     * Returns true if we should give focus to this window.
     */
    bool wantsFocus();

    /*!
     * Returns a WindowStatus enum of the current state of the window
     */
    WindowStatus status() const;

    // For _NET_WM_PING abstraction
    void startPing(bool restart = false);
    void stopPing();
    void receivedPing(ulong timeStamp);

    static MCompositeWindow *compositeWindow(Qt::HANDLE window);

    /*!
     * Ensures that the corresponding texture reflects the contents of the
     * associated pixmap and schedules a redraw of this item.
     */
    virtual void updateWindowPixmap(XRectangle *rects = 0, int num = 0,
                                    Time when = 0) = 0;

    /*!
     * Recreates the pixmap id and saves the offscreen buffer that represents
     * this window. This will update the offscreen backing store.
     */
    virtual void saveBackingStore() = 0;

    /*!
      Clears the texture that is associated with the offscreen pixmap
     */
    virtual void clearTexture() = 0;

    /*!
      Returns pixmap for the window.
     */
    virtual Pixmap windowPixmap() const = 0;

    /*!
      Returns true if the window corresponding to the offscreen pixmap
      is rendering directly to the framebuffer, otherwise return false.
     */
    virtual bool isDirectRendered() const = 0;

    /*!
     * Sets the width and height of the item.
     * Reimplementations must chain back.
     */
    virtual void resize(int w, int h);

    static bool hasTransitioningWindow();

    /*!
     * Tells if this window is transitioning.
     */
    bool isWindowTransitioning() const { return is_transitioning; }
    
    /*!
     * Returns whether this object represents a valid (i.e. viewable) window
     */
    bool isValid() const { return is_valid; }

    /*! 
     * Returns whether this is an application window
     */
    bool isAppWindow(bool include_transients = false);

    bool isClosing() const { return window_status == Closing; }

#ifdef WINDOW_DEBUG
    void hangIt() { window_status = Hung; }
    bool isHung() const { return window_status == Hung; }
#endif

    MWindowPropertyCache *propertyCache() const { return pc; }
    void setPropertyCache(MWindowPropertyCache *p) { pc = p; }
    
    /*!
     * Convenience function returns last visible parent of this window
     */
    Window lastVisibleParent() const;
    
    /*!
     * Returns the index of this window in the stacking list
     */
    int indexInStack() const;
    
    /*!
     * Returns whatever window is directly behind this window. 0 if there is none.
     */
    MCompositeWindow* behind() const { return behind_window; }

    /*!
     *  Returns a pointer to this window's group if it belongs to a group and 0
     * if 0 if not a member
     */
    MCompositeWindowGroup* group() const;
    
    /*! Disabled alpha-blending for a dim-effect instead */
    void setDimmedEffect(bool dimmed) { dimmed_effect = dimmed; }
    
    bool dimmedEffect() const { return dimmed_effect; }
    bool paintedAfterMapping() const { return painted_after_mapping; }
    void waitForPainting();

    MCompositeWindowAnimation* windowAnimator() const;
    
public slots:

    void updateIconGeometry();
    void startTransition();
    void setBlurred(bool);
    
    /* Operations with transition animations*/
    // set to Closing state and send delete/kill
    void closeWindowRequest();
    // start unmap animation
    void closeWindowAnimation();
    bool showWindow();
    
    /*!
     * Called when the window contents are damaged or on timeout.
     */
    void damageReceived();

    /*!
     * Don't start the windowShown() animation until the item is resized,
     * in addition to waiting for the damage(s).
     */
    void expectResize();

    /*!
     * Called to start a reappearance timer for the application hung dialog.
     */
    void startDialogReappearTimer();
    
     /*!
      * This internal slot is called whenever a start of window animation 
      * occurs. This is an atomic operation. 
      */
    virtual void beginAnimation(); 
    
    /*!
      * This internal slot is called whenever the window has finished 
      * animating its effects
      */
    virtual void endAnimation();

private slots:

    /*! Called internally to update how this item looks when the transitions
      are done
     */
    void finalizeState();

    void pingTimeout();
    void reappearTimeout();
    void pingWindow(bool restart = false);
    void q_itemRestored();
    void q_fadeIn();
    
signals:
    /*!
     * Emitted if this window becomes hung or "hung dialog" reappearance timer
     * elapses or stops being hung.
     */
    void windowHung(MCompositeWindow *window, bool is_hung);

    /*! Emitted when this window gets restored from an iconified state */
    void itemRestored(MCompositeWindow *window);
    /*! Emitted just after this window gets iconified  */
    void itemIconified(MCompositeWindow *window);
    /*! Emitted when last animation finished (yeah) */
    void lastAnimationFinished(MCompositeWindow *window);
    /*! Emitted when the user wants to close this window */
    void closeWindowRequest(MCompositeWindow *window);

protected:

    virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value);    
    virtual QPainterPath shape() const;
        
private:
    /* re-implemented in GL/GLES2 backends for internal interaction
      between shader effects */
    virtual MTexturePixmapPrivate* renderer() const = 0;
    void findBehindWindow();
    bool isInanimate();

    QPointer<MWindowPropertyCache> pc;
    QPointer<MCompositeWindow> behind_window;
    QPointer<MCompositeWindowAnimation> animator, orig_animator;
    int zval;
    ulong sent_ping_timestamp;
    ulong received_ping_timestamp;
    bool blur;
    bool iconified;
    bool iconified_final;
    IconifyState iconify_state;
    bool destroyed;
    WindowStatus window_status;
    bool need_decor;
    bool window_visible;
    short window_obscured;
    bool is_valid;
    bool newly_mapped;
    bool is_transitioning;
    bool dimmed_effect;
    char waiting_for_damage;
    bool resize_expected;
    bool painted_after_mapping;

    static int window_transitioning;

    // Main ping timer
    QTimer *t_ping, *t_reappear;
    QTimer *damage_timer;
    Qt::HANDLE win_id;

    friend class MTexturePixmapPrivate;
    friend class MCompositeWindowShaderEffect;
    friend class MCompositeWindowAnimation;
    friend class McParallelAnimation;
};

#endif
