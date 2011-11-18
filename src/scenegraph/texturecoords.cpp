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
#include "texturecoords.h"

/*!
  \class TextureCoords
  \brief TextureCoords represents texture coordinates of a quad

  TextureCoords provides a flexible method to specify the texture coordinates
  of a quad, supporting rotated and inverted texture coordinates in any 
  direction.
 */

/*!
  TextureCoords constructor. Specifies the initial coordinates of the
  texture as a QRectF. The default values are (0,0,1,1)
 */
TextureCoords::TextureCoords(const QRectF& rect)
    :tex_rect(rect),
     orientation(TextureCoords::StartTopLeft | TextureCoords::RotateCCW)
{}

/*!
  Sets the orientation flags of this TextureCoords to \a neworient. All
  orientation values in \a neworient are enabled; all flags not in
  \a neworient are disabled
*/
void TextureCoords::setOrientation(TextureCoords::Orientation neworient)
{
    orientation = (orientation & ~(StartTopLeft|StartBottomLeft|
                                   StartBottomRight|StartTopRight|
                                   RotateCW|RotateCCW)) 
                   | (neworient & (StartTopLeft|StartBottomLeft|
                                   StartBottomRight|StartTopRight|
                                   RotateCW|RotateCCW));
}

/*!
  \return Returns the orientation flags of this TextureCoords
 */
TextureCoords::Orientation TextureCoords::textureOrientation() const
{
    return QFlag(orientation & (StartTopLeft|StartBottomLeft|
                                StartBottomRight|StartTopRight|
                                RotateCW|RotateCCW));
}

/*!
  Returns the texture coordinates in the specified GLfloat array \a coords

  Note: This function will be deprecated soon in favor of VBO id's
 */
void TextureCoords::getTexCoords(GLfloat coords[8])
{
     QFlags<TextureCoords::Orientation> t = textureOrientation();
     
     if (t.testFlag(TextureCoords::StartTopLeft)) {
         coords[0] = tex_rect.left(); coords[1] = tex_rect.top();
         if (t.testFlag(TextureCoords::RotateCCW)) {
             coords[2] = tex_rect.left();  coords[3] = tex_rect.height();
             coords[4] = tex_rect.width(); coords[5] = tex_rect.height();
             coords[6] = tex_rect.width(); coords[7] = tex_rect.top();
         } else {
             
             coords[2] = tex_rect.width(); coords[3] = tex_rect.top();
             coords[4] = tex_rect.width(); coords[5] = tex_rect.height();
             coords[6] = tex_rect.left(); coords[7] = tex_rect.height();
         }
     } 
     if (t.testFlag(TextureCoords::StartBottomLeft)) {
         coords[0] = tex_rect.left(); coords[1] = tex_rect.height();
         if (t.testFlag(TextureCoords::RotateCCW)) {
             coords[2] = tex_rect.width();  coords[3] = tex_rect.height();
             coords[4] = tex_rect.width(); coords[5] = tex_rect.top();
             coords[6] = tex_rect.left(); coords[7] = tex_rect.top();
         } else {
             
             coords[2] = tex_rect.left(); coords[3] = tex_rect.top();
             coords[4] = tex_rect.width(); coords[5] = tex_rect.top();
             coords[6] = tex_rect.width(); coords[7] = tex_rect.height();
         }
     }
     if (t.testFlag(TextureCoords::StartBottomRight)) {
         coords[0] = tex_rect.width(); coords[1] = tex_rect.height();
         if (t.testFlag(TextureCoords::RotateCCW)) {
             coords[2] = tex_rect.width();  coords[3] = tex_rect.top();
             coords[4] = tex_rect.left(); coords[5] = tex_rect.top();
             coords[6] = tex_rect.left(); coords[7] = tex_rect.height();
         } else {
             
             coords[2] = tex_rect.left(); coords[3] = tex_rect.height();
             coords[4] = tex_rect.left(); coords[5] = tex_rect.top();
             coords[6] = tex_rect.width(); coords[7] = tex_rect.top();
         }
     }
     if (t.testFlag(TextureCoords::StartTopRight)) {
         coords[0] = tex_rect.width(); coords[1] = tex_rect.top();
         if (t.testFlag(TextureCoords::RotateCCW)) {
             coords[2] = tex_rect.left();  coords[3] = tex_rect.top();
             coords[4] = tex_rect.left(); coords[5] = tex_rect.height();
             coords[6] = tex_rect.width(); coords[7] = tex_rect.height();
         } else {
             
             coords[2] = tex_rect.width(); coords[3] = tex_rect.height();
             coords[4] = tex_rect.left(); coords[5] = tex_rect.height();
             coords[6] = tex_rect.left(); coords[7] = tex_rect.top();
         }
     }
}

bool TextureCoords::operator == (const TextureCoords& other)
{
    return (textureOrientation() == other.textureOrientation() &&
            tex_rect == other.tex_rect);
}
