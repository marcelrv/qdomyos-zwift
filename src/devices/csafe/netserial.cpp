/*
 * Copyright (c) 2024 Marcel Verpaalen (marcel@verpaalen.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * This emulates a serial port over a network connection.
 * e.g. as created by ser2net or hardware serial to ethernet converters
 *
 */

#include "netserial.h"
#include <QDebug>
#include <QHostAddress>

NetSerial::NetSerial(QString deviceFilename) : socket(new QTcpSocket()), _timeout(1000), endChar('\n') {
    setDevice(deviceFilename);
}

NetSerial::~NetSerial() {
    closePort();
    delete socket;
}

void NetSerial::setTimeout(int timeout) { this->_timeout = timeout; }

void NetSerial::setDevice(const QString &devname) {
    if (!devname.isEmpty()) {
        deviceFilename = devname;
        parseDeviceFilename(devname);
    }
}

void NetSerial::setEndChar(uint8_t endChar) { this->endChar = endChar; }

bool NetSerial::isOpen() const { return socket->state() == QAbstractSocket::ConnectedState; }

int NetSerial::openPort() {
    if (serverAddress.isEmpty() || serverPort == 0) {
        qDebug() << "Invalid server address or port";
        return -1;
    }

    socket->connectToHost(serverAddress, serverPort);
    if (!socket->waitForConnected(_timeout)) {
        qDebug() << "Failed to connect to server:" << socket->errorString();
        return -1;
    }

    return 0;
}

int NetSerial::closePort() {
    if (isOpen()) {
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            socket->waitForDisconnected(_timeout);
        }
    }
    return 0;
}

int NetSerial::dataAvailable() {
    if (!isOpen()) {
        qDebug() << "Socket not connected.";
        return -1;
    }
    if (socket->bytesAvailable() > 0) {
        qDebug() << "Socket data is available!!!!!!!!!!!!!!.";
    }
    return socket->bytesAvailable();
}

int NetSerial::rawWrite(uint8_t *bytes, int size) {
    if (!isOpen()) {
        qDebug() << "Socket not connected.";
        return -1;
    }

    QByteArray data(reinterpret_cast<const char *>(bytes), size);
    qint64 bytesWritten = socket->write(data);
    if (bytesWritten == -1) {
        qDebug() << "Failed to write to socket:" << socket->errorString();
        return -1;
    }

    if (!socket->waitForBytesWritten(_timeout)) {
        qDebug() << "Write operation timed out.";
        return -1;
    }

    return static_cast<int>(bytesWritten);
}

int NetSerial::rawRead(uint8_t bytes[], int size, bool line) {
    if (!isOpen()) {
        qDebug() << "Socket not connected.";
        return -1;
    }

    QByteArray buffer;
    while (buffer.size() < size) {
        if (!socket->waitForReadyRead(_timeout)) {
            qDebug() << "Read operation timed out.";
            return buffer.size() > 0 ? buffer.size() : -1;
        }

        buffer.append(socket->read(size - buffer.size()));
        if (line && buffer.contains(static_cast<char>(endChar))) {
            int index = buffer.indexOf(static_cast<char>(endChar)) + 1;
            memcpy(bytes, buffer.data(), index);
            return index;
        }
    }

    memcpy(bytes, buffer.data(), buffer.size());
    return buffer.size();
}

bool NetSerial::parseDeviceFilename(const QString &filename) {
    // Format: "server:port", e.g., "127.0.0.1:12345"
    QStringList parts = filename.split(':');
    if (parts.size() == 2) {
        serverAddress = parts[0];
        serverPort = parts[1].toUShort();
        return true;
    } else {
        qDebug() << "Invalid device filename format. Expected 'server:port'.";
        return false;
    }
}