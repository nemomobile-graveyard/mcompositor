/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
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
#include "mrmi.h"

#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QBuffer>
#include <QDataStream>
#include <QMetaObject>
#include <QGenericArgument>
#include <QVariant>
#include <QPointer>

class MRmiPrivate: public QObject
{
    Q_OBJECT

public:
    MRmiPrivate(const QString &key, bool isServer);

    void exportObject(QObject* p);
    void invokeRemote(const char* objectName, const char* methodName,
                      const QVector<QVariant> &args);
    void invokeRemote(const char* objectName, const char* methodName,
                      const QVariant &arg);

private slots:
    void _q_incoming();
    void _q_readData();

private:
    void invokeLocal(QLocalSocket* socket, QDataStream& stream);

    QString _key;
    QObject* _obj;
    int method_size;

    QPointer<QObject> _server;
    QBuffer output_buffer;
    QDataStream stream;

    QVector<QVariant> args;
};

MRmiPrivate::MRmiPrivate(const QString &key, bool isServer)
        : _key(key), _obj(0), method_size(0)
{
    _key = "/var/run/" + key;
    output_buffer.open(QIODevice::WriteOnly);
    if (!isServer)
          return;

    // We're a server, listen right away.
    QLocalServer *server = new QLocalServer(this);
    bool isok = server->listen(_key);
    if (!isok && server->serverError() == QAbstractSocket::AddressInUseError) {
        // Is it by a living mdecorator?  MApplication shouldn't have let us
        // progress this far in this case. but check it just in case.
        QLocalSocket *mate = new QLocalSocket();
        mate->connectToServer(_key);
        if (mate->waitForConnected())
            qFatal("another mdecorator is already running");
        delete mate;

        // Remove the corpse.
        QFile::remove(_key);
        isok = server->listen(_key);
    }
    if (!isok) {
        qWarning() << "MRmiPrivate:" << server->errorString();
        delete server;
        return;
    }

    connect(server, SIGNAL(newConnection()), this, SLOT(_q_incoming()));
    _server = server;
}

void MRmiPrivate::exportObject(QObject* p)
{
    _obj = p;

    // If we're a client tell the server what sort of object we have.
    if (!dynamic_cast<QLocalServer*>(_server.data()))
        invokeRemote("MRmiServer", "exportObject",
                     QVector<QVariant>(1, _obj
                         ? _obj->metaObject()->className()
                         : ""));
}

void MRmiPrivate::_q_readData()
{
    QLocalSocket* socket = static_cast<QLocalSocket*>(sender());

    // Read and execute as much requests we can, there could be more than one
    // in the pipe and if we didn't they would be left there until there is
    // more incoming data.
    for (;;) {
        // Reset @stream every time in the loop because the method called by
        // invokeLocal() might have inveokeRemote()d.
        stream.setDevice(socket);

        // How many bytes to expect
        if (method_size == 0) {
            if (socket->bytesAvailable() < (int)sizeof(method_size))
                return;
            stream >> method_size;
        }

        // Have we got that many?
        if (socket->bytesAvailable() < method_size)
            return;

        // Read and dispatch the message.
        method_size = 0;
        invokeLocal(socket, stream);
    }
}

void MRmiPrivate::invokeLocal(QLocalSocket* socket, QDataStream& stream)
{
    char *objectName = 0, *methodName = 0;

    // Make sure args[0..9] are indexable.
    args.resize(0);
    stream >> objectName;
    stream >> methodName;
    stream >> args;
    args.resize(10);

    // Is the message for us?
    if (!objectName || !methodName) {
        qWarning("MRmiPrivate::invokeLocal: don't know what to call");
    } else if (!strcmp(objectName, "MRmiServer") &&
        !strcmp(methodName, "exportObject")) {
        socket->setObjectName(args[0].toString());
    } else { // Call @methodName on _obj.
        // Copy @args so the invoked method is safe from us modifying it.
        QVector<QVariant> marg = args;
        QMetaObject::invokeMethod(_obj, methodName,
                   QGenericArgument(marg[0].typeName(), marg[0].data()),
                   QGenericArgument(marg[1].typeName(), marg[1].data()),
                   QGenericArgument(marg[2].typeName(), marg[2].data()),
                   QGenericArgument(marg[3].typeName(), marg[3].data()),
                   QGenericArgument(marg[4].typeName(), marg[4].data()),
                   QGenericArgument(marg[5].typeName(), marg[5].data()),
                   QGenericArgument(marg[6].typeName(), marg[6].data()),
                   QGenericArgument(marg[7].typeName(), marg[7].data()),
                   QGenericArgument(marg[8].typeName(), marg[8].data()),
                   QGenericArgument(marg[9].typeName(), marg[9].data()));
    }

    delete[] objectName;
    delete[] methodName;
}

void MRmiPrivate::invokeRemote(const char* objectName,
                               const char* methodName,
                               const QVector<QVariant> &args)
{
    QLocalSocket* socket;

    if (dynamic_cast<QLocalServer*>(_server.data())) {
        // We're a server, let's see which client has the object,
        // and send the message it.
        const QString name(objectName);
        socket = _server->findChild<QLocalSocket*>(name);
        if (!socket) {
            qWarning("MRmi::invoke(): unknown object %s", objectName);
            return;
        }
    } else if (!_server) {
        // We're an unconnected client.  Connect to _key.
        if (!QFile::exists(_key))
            // socket does not exist yet, probably mdecorator starts later
            return;
        socket = new QLocalSocket(this);
        socket->connectToServer(_key);
        if (!socket->waitForConnected()) {
            qWarning() << "MRmi::invoke():" << socket->errorString() << _key;
            delete socket;
            return;
        }

        connect(socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()));
        connect(socket, SIGNAL(readyRead()), this, SLOT(_q_readData()));
        method_size = 0;
        _server = socket;

        // Tell the server what we have unless we're in an exportObject().
        if (strcmp(objectName, "MRmiServer") ||
            strcmp(methodName, "exportObject"))
            exportObject(_obj);
    } else
        // We're a client connected to _server.
        socket = static_cast<QLocalSocket*>(_server.data());

    // Count the number of bytes of the serialized message.
    QByteArray &buf = output_buffer.buffer();
    Q_ASSERT(!buf.size());
    stream.setDevice(&output_buffer);
    stream << objectName;
    stream << methodName;
    stream << args;

    // Send the message prefixed by its size.
    stream.setDevice(socket);
    stream << buf.size();
    socket->write(buf);

    // Flush the pipe.  We need to be kind of reentrant because
    // waitForBytesWritten() spins the main loop and we might start
    // processing incoming messages, which in turn may send some.
    buf.truncate(0);
    output_buffer.seek(0);
    socket->waitForBytesWritten();
}

void MRmiPrivate::invokeRemote(const char* objectName,
                               const char* methodName,
                               const QVariant &arg)
{
    args.resize(1);
    args[0] = arg;
    invokeRemote(objectName, methodName, args);
}

void MRmiPrivate::_q_incoming()
{
    QLocalSocket* client;

    client = static_cast<QLocalServer*>(_server.data())->nextPendingConnection();
    if (!client)
        return;

    connect(client, SIGNAL(disconnected()), client, SLOT(deleteLater()));
    connect(client, SIGNAL(readyRead()), this, SLOT(_q_readData()));

    // This is broken if we had multiple clients..
    method_size = 0;
}

/// Class MRmi
MRmi::MRmi(const QString& key, QObject* p, bool isServer)
        : QObject(p), d_ptr(new MRmiPrivate(key, isServer))
{
}

MRmi::~MRmi()
{
    delete d_ptr;
}

void MRmi::exportObject(QObject* obj)
{
    d_ptr->exportObject(obj);
}

void MRmi::invoke(const char* objectName, const char* methodName,
                  const QVector<QVariant> &args)
{
    d_ptr->invokeRemote(objectName, methodName, args);
}

void MRmi::invoke(const char* objectName, const char* methodName,
                  const QVariant &arg)
{
    d_ptr->invokeRemote(objectName, methodName, arg);
}

#include "moc_mrmi.cpp"
