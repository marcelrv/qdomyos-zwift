/*
 * Copyright (c) 2009 Mark Liversedge (liversedge@gmail.com),
                 2024 Marcel Verpaalen
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
 */

// I have consciously avoided putting things like data logging, lap marking,
// intervals or any load management functions in this class. It is restricted
// to controlling an reading telemetry from the device
//
// I expect higher order classes to implement such functions whilst
// other devices (e.g. ANT+ devices) may be implemented with the same basic
// interface
//
// I have avoided a base abstract class at this stage since I am uncertain
// what core methods would be required by say, ANT+ or Tacx devices

#ifndef _SERIALPORT_h
#define _SERIALPORT_h

#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QString>
#include <QThread>

#ifdef WIN32
#include <windows.h>

#include <winbase.h>
#else
#include <sys/ioctl.h>
#include <termios.h> // unix!!
#include <unistd.h>  // unix!!
#ifndef N_TTY        // for OpenBSD, this is a hack XXX
#define N_TTY 0
#endif
#endif

#ifdef Q_OS_ANDROID
#include "keepawakehelper.h"
#include <QAndroidJniObject>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

class Serialport : public QThread {

  public:
    Serialport(QString deviceFilename = "", speed_t baudRate = B9600); // pass device
    ~Serialport();

    QObject *parent;

    // Device management
    void setDevice(const QString &devname);
    void setEndChar(uint8_t endChar);
    void setTimeout(int timeout);

    // Port control
    int openPort();
    int closePort();

    // Data transfer
    int rawWrite(uint8_t *bytes, int size);
    int rawRead(uint8_t bytes[], int size, bool line = false);

    bool isOpen() const;

  private:
    // i/o message holder
    uint8_t buf[7];
    speed_t baudRate = B9600;
    uint8_t endChar = 0x0D;

    int _timeout = 1500;

    // device port
    QString deviceFilename;
#ifdef WIN32
    HANDLE devicePort;  // file descriptor for reading from com3
    DCB deviceSettings; // serial port settings baud rate et al
#else
    int devicePort;                // unix!!
    struct termios deviceSettings; // unix!!
#endif

#ifdef Q_OS_ANDROID
    QList<jbyte> bufRX;
    bool cleanFrame = false;
#endif
};

#endif //
