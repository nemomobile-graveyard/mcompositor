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
// -*- C++ -*-

#ifndef MRMISERVER_H
#define MRMISERVER_H

#include <QObject>
#include <QVector>
#include <QVariant>

class MRmiPrivate;

class MRmi: public QObject
{
protected:
    MRmi(const QString& key, QObject* parent, bool isServer);

public:
    /*!
     * Disconnects all connections and destroys this object
     */
    ~MRmi();

    /*!
     * Export a QObject for remote invocation. Currently only one QObject per
     * MRmiServer is supported.
     *
     * \param object QObject to be exported.
     */
    void exportObject(QObject* object);

    /*!
     * Invoke the remote function
     *
     * \param objectName which object to call
     * \param methodName the name of the method to call
     * \param args QVariant representation of the arguments of the remote method
     */
    void invoke(const char* objectName, const char* methodName,
                const QVector<QVariant> &args);
    void invoke(const char* objectName, const char* methodName,
                const QVariant &arg);
    void invoke(const char* objectName, const char* methodName)
    { invoke(objectName, methodName, QVector<QVariant>()); }

private:
    Q_DISABLE_COPY(MRmi)

    MRmiPrivate* d_ptr;
};

/*!
 * \class MRmiServer
 * \brief The MRmiServer provides a way for member functions of QObject-based
 * classes to be directly invoked from another process without relying on an
 * external transport such as d-bus.
 *
 * To make a QObject-based class remotely callable from another process,
 * ensure that the object has Q_OBJECT macro and the member functions you want
 * to export are public slots themselves.
 *
 * The types of the arguments of those member functions need to be supported
 * by QVariant as well. Most Qt types are supported. This include from plain
 * old data types to complex GUI types such as QRect, QColor, QImages, and more.
 * Container types such as QList are even supported.
 *
 * If needed, custom complex classes can also be sent across the wire
 * provided the operator QVariant(), operator<<() and
 * operator>>() are overloaded, and the class is declared in Qt's metaobject
 * system using qRegisterMetaType(). Qt uses this class internally for
 * mashalling/unmarshalling types (see QMetaType for details).
 */
class MRmiServer: public MRmi
{
public:
    /*!
     * Creates a MRmiServer
     *
     * \param key a unique key that identifies this server
     * \param parent QObject.
     */
    MRmiServer(const QString& key, QObject* parent = 0):
        MRmi(key, parent, true) { }

private:
    Q_DISABLE_COPY(MRmiServer)
};

/*!
 * \class MRmiClient
 *
 * \brief The MRmiClient allows member functions of QObjects exported by
 * MRmiServer from another process to be invoked remotely.
 */
class MRmiClient: public MRmi
{
public:
    /*!
     * Creates a MRmiClient
     *
     * \param key the key used to identify the remote MRmiServer
     * \param parent the parent QObject
     */
    MRmiClient(const QString& key, QObject* parent  = 0):
        MRmi(key, parent, false) { }

private:
    Q_DISABLE_COPY(MRmiClient)
};

#endif
