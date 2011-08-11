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

#ifndef MTEXTUREPIXMAPITEM_H
#define MTEXTUREPIXMAPITEM_H

#include <QtOpenGL>

#include <X11/Xlib.h>

class MTextureFromPixmapPrivate;

/*!
 * This is a helper class to implement texture from pixmap for different use cases.
 */
class MTextureFromPixmap {
public:
    MTextureFromPixmap();
    virtual ~MTextureFromPixmap();

    /*!
     * Bind given drawable to the texture
     */
    void bind(Drawable drawable);
    /*!
     * Unbind pixmap from the texture
     *
     * This sets drawable to None to keep track if there is bound pixmap
     */
    void unbind();
    /*!
     * Update texture content if using fallback implementation without TFP
     */
    void update();

    /*!
     * Query if texture is inverted
     */
    bool invertedTexture() const;

    /*!
     * Returns if TFP is valid texture
     */
    bool isValid() const;

    Drawable drawable;
    GLuint textureId;
    bool alpha;
private:
    MTextureFromPixmapPrivate *d;
    bool valid;
};

#endif
