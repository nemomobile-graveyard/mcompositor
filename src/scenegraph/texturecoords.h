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

#ifndef TEXTURECOORDS_H
#define TEXTURECOORDS_H

#include <QtOpenGL>

class TextureCoords
{
 public:
    enum _Orientation
    {
        StartTopLeft = 0x1,
        StartBottomLeft = 0x2,
        StartBottomRight = 0x4,
        StartTopRight = 0x8,
        RotateCW = 0x10,
        RotateCCW = 0x20
    };
    Q_DECLARE_FLAGS(Orientation, _Orientation)

    TextureCoords(const QRectF& = QRectF(0,0,1,1));
    bool operator == (const TextureCoords& other);
    
    void setOrientation(Orientation orient);
    Orientation textureOrientation() const;
    
    // remove later on vbo implementation (for testing purposes)
    void getTexCoords(GLfloat coords[8]);

    const QRectF& rect() { return tex_rect; }
    
 private:
    QRectF tex_rect;
    QFlags<TextureCoords::Orientation> orientation;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TextureCoords::Orientation)

#endif
