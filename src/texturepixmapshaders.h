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
#ifndef TEXTUREPIXMAPSHADERS_H
#define TEXTUREPIXMAPSHADERS_H

static const char* TexpVertShaderSource = "\
    attribute highp vec4 inputVertex; \
    attribute mediump vec2 textureCoord; \
    uniform   highp mat4 matProj; \
    uniform   highp mat4 matWorld; \
    varying   mediump vec2 fragTexCoord; \
    void main(void) \
    {\
            gl_Position = (matProj * matWorld) * inputVertex;\
            fragTexCoord = textureCoord; \
    }";

static const char* TexpFragShaderSource = "\
    varying mediump vec2 fragTexCoord;\
    uniform sampler2D texture;\
    void main(void) \
    {\
            gl_FragColor = texture2D(texture, fragTexCoord); \
    }";

static const char* TexpOpacityFragShaderSource = "\
    varying mediump vec2 fragTexCoord;\
    uniform sampler2D texture;\
    uniform lowp float opacity;\n\
    void main(void) \
    {\
            gl_FragColor = texture2D(texture, fragTexCoord) * opacity; \
    }";

static const char* TexpCustomShaderSource = "\
    varying mediump vec2 fragTexCoord;\n\
    uniform lowp sampler2D texture;\n\
    uniform lowp float opacity;\n\
    void main(void) \n\
    {\n\
            gl_FragColor = customShader(texture, fragTexCoord) * opacity; \n\
    }\n\n";
#endif
