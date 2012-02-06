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

    Q_ENUMS(WindowStatus);
    enum WindowStatus {
        Normal = 0,
        Hung,
        Minimizing,
        Restoring,
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
     * Iconify window with animation. Returns true if signal will come.
    */
    bool iconify();

    /*!
     * Sets whether or not the window is obscured and generates the
     * proper VisibilityNotify for the window.
     */
    void setWindowObscured(bool obscured, bool no_notify = false);
    bool windowObscured() { return window_obscured; }

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
     * Set iconify status manually.
     */
    void setIconified(bool iconified);

    /*!
     * Set scale, opacity etc. to normal values.
     */
    void setUntransformed(bool preserve_iconified = false);

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
     * Restores window with animation.
    */
    void restore();

    /*!
     * Overrides QGraphicsItem::update() so we have complete control of item
     * updates.
     */
    static void update();

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
     * Tells if this window is currently transitioning so that the
     * stacking does not change.
     */
    bool isNotChangingStacking() const { return is_transitioning
                                                && is_not_stacking; }

    void setNotChangingStacking(bool value) { is_not_stacking = value; }

    /*!
     * Returns whether this object represents a valid (i.e. viewable) window
     */
    bool isValid() const { return pc && pc->is_valid; }

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
    MCompositeWindow* behind() const;

    /*!
     *  Returns a pointer to this window's group if it belongs to a group and 0
     * if 0 if not a member
     */
    MCompositeWindowGroup* group() const;

    bool paintedAfterMapping() const { return painted_after_mapping; }
    /*!
     * Needed for the startup time.
     */
    void setPaintedAfterMapping(bool b) { painted_after_mapping = b; }
    void waitForPainting();

    MCompositeWindowAnimation* windowAnimator() const;

    void startCloseTimer();
    void stopCloseTimer();

     /*!
      * This is called whenever a start of window animation occurs.
      */
    void beginAnimation(); 
    
    /*!
      * This is called whenever the window has finished animating.
      */
    void endAnimation();

public slots:

    void updateIconGeometry();
    void startTransition();
    
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
    void closeTimeout();
    
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
    /*! Emitted when the first animation is started */
    void firstAnimationStarted();
    /*! Emitted when last animation finished (yeah) */
    void lastAnimationFinished(MCompositeWindow *window);
    /*! Emitted when the user wants to close this window */
    void closeWindowRequest(MCompositeWindow *window);
    /*! Emitted when a damage event was received */
    void damageReceived(MCompositeWindow *window);

protected:

    virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value);    
    virtual QPainterPath shape() const;
        
private:
    /* re-implemented in GL/GLES2 backends for internal interaction
      between shader effects */
    virtual MTexturePixmapPrivate* renderer() const = 0;
    void findBehindWindow();
    bool isInanimate(bool check_pixmap = true);
    void setAllowDelete(bool setting) { allow_delete = setting; }

    QPointer<MWindowPropertyCache> pc;
    QPointer<MCompositeWindowAnimation> animator, orig_animator;
    ulong sent_ping_timestamp;
    ulong received_ping_timestamp;
    bool iconified;
    IconifyState iconify_state;
    bool in_destructor;
    WindowStatus window_status;
    bool need_decor;
    short window_obscured;
    bool newly_mapped;
    bool is_transitioning, is_not_stacking;
    bool resize_expected;
    bool painted_after_mapping;
    bool allow_delete;

    static int window_transitioning;

    // Main ping timer
    QTimer *t_ping, *t_reappear;
    QTimer *damage_timer;
    QTimer close_timer;
    Qt::HANDLE win_id;

    friend class MTexturePixmapPrivate;
    friend class MCompositeWindowShaderEffect;
    friend class MCompositeWindowAnimation;
    friend class MChainedAnimation;
    friend class McParallelAnimation;
    friend class ut_Anim;
    friend class ut_Compositing;
    friend class ut_splashscreen;
};

#endif
