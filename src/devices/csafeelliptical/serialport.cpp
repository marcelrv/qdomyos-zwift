

#include serialport.h


/* ----------------------------------------------------------------------
 * CONSTRUCTOR/DESRTUCTOR
 * ---------------------------------------------------------------------- */
Serialport::Serialport(QObject *parent, QString devname) : QThread(parent) {
 
    setDevice(devname);
    deviceStatus = 0;
    this->parent = parent;
 
}

Serialport::~Serialport() {}

void Serialport::setDevice(QString devname) {
    // if not null, replace existing if set, otherwise set
    deviceFilename = devname;
}


int Serialport::closePort() {
#ifdef WIN32
    return (int)!CloseHandle(devicePort);
#else
    tcflush(devicePort, TCIOFLUSH); // clear out the garbage
    return close(devicePort);
#endif
}

int Serialport::openPort() {
#ifdef Q_OS_ANDROID
    QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/Usbserial", "open",
                                              "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
#elif !defined(WIN32)

    // LINUX AND MAC USES TERMIO / IOCTL / STDIO

#if defined(Q_OS_MACX)
    int ldisc = TTYDISC;
#else
    int ldisc = N_TTY;                    // LINUX
#endif

    if ((devicePort = open(deviceFilename.toLatin1(), O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1)
        return errno;

    tcflush(devicePort, TCIOFLUSH); // clear out the garbage

    if (ioctl(devicePort, TIOCSETD, &ldisc) == -1)
        return errno;

    // get current settings for the port
    tcgetattr(devicePort, &deviceSettings);

    // set raw mode i.e. ignbrk, brkint, parmrk, istrip, inlcr, igncr, icrnl, ixon
    //                   noopost, cs8, noecho, noechonl, noicanon, noisig, noiexn
    cfmakeraw(&deviceSettings);
    cfsetspeed(&deviceSettings, B2400);

    // further attributes
    deviceSettings.c_iflag &=
        ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ICANON | ISTRIP | IXON | IXOFF | IXANY);
    deviceSettings.c_iflag |= IGNPAR;
    deviceSettings.c_cflag &= (~CSIZE & ~CSTOPB);
    deviceSettings.c_oflag = 0;

#if defined(Q_OS_MACX)
    deviceSettings.c_cflag &= (~CCTS_OFLOW & ~CRTS_IFLOW); // no hardware flow control
    deviceSettings.c_cflag |= (CS8 | CLOCAL | CREAD | HUPCL);
#else
    deviceSettings.c_cflag &= (~CRTSCTS); // no hardware flow control
    deviceSettings.c_cflag |= (CS8 | CLOCAL | CREAD | HUPCL);
#endif
    deviceSettings.c_lflag = 0;
    deviceSettings.c_cc[VSTART] = 0x11;
    deviceSettings.c_cc[VSTOP] = 0x13;
    deviceSettings.c_cc[VEOF] = 0x20;
    deviceSettings.c_cc[VMIN] = 0;
    deviceSettings.c_cc[VTIME] = 0;

    // set those attributes
    if (tcsetattr(devicePort, TCSANOW, &deviceSettings) == -1)
        return errno;
    tcgetattr(devicePort, &deviceSettings);

    tcflush(devicePort, TCIOFLUSH); // clear out the garbage
#else
    // WINDOWS USES SET/GETCOMMSTATE AND READ/WRITEFILE

    COMMTIMEOUTS timeouts; // timeout settings on serial ports

    // if deviceFilename references a port above COM9
    // then we need to open "\\.\COMX" not "COMX"
    QString portSpec;
    int portnum = deviceFilename.midRef(3).toString().toInt();
    if (portnum < 10)
        portSpec = deviceFilename;
    else
        portSpec = "\\\\.\\" + deviceFilename;
    wchar_t deviceFilenameW[32]; // \\.\COM32 needs 9 characters, 32 should be enough?
    MultiByteToWideChar(CP_ACP, 0, portSpec.toLatin1(), -1, (LPWSTR)deviceFilenameW, sizeof(deviceFilenameW));

    // win32 commport API
    devicePort = CreateFile(deviceFilenameW, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (devicePort == INVALID_HANDLE_VALUE)
        return -1;

    if (GetCommState(devicePort, &deviceSettings) == false)
        return -1;

    // so we've opened the comm port lets set it up for
    deviceSettings.BaudRate = CBR_2400;
    deviceSettings.fParity = NOPARITY;
    deviceSettings.ByteSize = 8;
    deviceSettings.StopBits = ONESTOPBIT;
    deviceSettings.XonChar = 11;
    deviceSettings.XoffChar = 13;
    deviceSettings.EofChar = 0x0;
    deviceSettings.ErrorChar = 0x0;
    deviceSettings.EvtChar = 0x0;
    deviceSettings.fBinary = true;
    deviceSettings.fOutX = 0;
    deviceSettings.fInX = 0;
    deviceSettings.XonLim = 0;
    deviceSettings.XoffLim = 0;
    deviceSettings.fRtsControl = RTS_CONTROL_ENABLE;
    deviceSettings.fDtrControl = DTR_CONTROL_ENABLE;
    deviceSettings.fOutxCtsFlow = FALSE; // TRUE;

    if (SetCommState(devicePort, &deviceSettings) == false) {
        CloseHandle(devicePort);
        return -1;
    }

    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutConstant = 1000;
    timeouts.ReadTotalTimeoutMultiplier = 50;
    timeouts.WriteTotalTimeoutConstant = 2000;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(devicePort, &timeouts);

#endif

    // success
    return 0;
}

int Serialport::rawWrite(uint8_t *bytes, int size) // unix!!
{
    qDebug() << size << QByteArray((const char *)bytes, size).toHex(' ');

    int rc = 0;

#ifdef Q_OS_ANDROID

    QAndroidJniEnvironment env;
    jbyteArray d = env->NewByteArray(size);
    jbyte *b = env->GetByteArrayElements(d, 0);
    for (int i = 0; i < size; i++)
        b[i] = bytes[i];
    env->SetByteArrayRegion(d, 0, size, b);
    QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/Usbserial", "write", "([B)V", d);
#elif defined(WIN32)
    DWORD cBytes;
    rc = WriteFile(devicePort, bytes, size, &cBytes, NULL);
    if (!rc)
        return -1;
    return rc;

#else
    int ibytes;
    ioctl(devicePort, FIONREAD, &ibytes);

    // timeouts are less critical for writing, since vols are low
    rc = write(devicePort, bytes, size);

    // but it is good to avoid buffer overflow since the
    // computrainer microcontroller has almost no RAM
    if (rc != -1)
        tcdrain(devicePort); // wait till its gone.

    ioctl(devicePort, FIONREAD, &ibytes);
#endif

    return rc;
}

int Serialport::rawRead(uint8_t bytes[], int size) {
    int rc = 0;

#ifdef Q_OS_ANDROID

    int fullLen = 0;
    cleanFrame = false;

    // previous buffer?
    while (bufRX.count()) {
        bytes[fullLen++] = bufRX.at(0);
        bufRX.removeFirst();
        qDebug() << "byte popped from rxBuf";
        if (fullLen >= size) {
            qDebug() << size << QByteArray((const char *)bytes, size).toHex(' ');
            return size;
        }
    }

    QAndroidJniEnvironment env;
    while (fullLen < size) {
        QAndroidJniObject dd =
            QAndroidJniObject::callStaticObjectMethod("org/cagnulen/qdomyoszwift/Usbserial", "read", "()[B");
        jint len = QAndroidJniObject::callStaticMethod<jint>("org/cagnulen/qdomyoszwift/Usbserial", "readLen", "()I");
        jbyteArray d = dd.object<jbyteArray>();
        jbyte *b = env->GetByteArrayElements(d, 0);
        if (len + fullLen > size) {
            QByteArray tmpDebug;
            qDebug() << "buffer overflow! Truncate from" << len + fullLen << "requested" << size;
            /*for(int i=0; i<len; i++) {
                qDebug() << b[i];
            }*/

            for (int i = fullLen; i < size; i++) {
                bytes[i] = b[i - fullLen];
            }
            for (int i = size; i < len + fullLen; i++) {
                jbyte bb = b[i - fullLen];
                bufRX.append(bb);
                tmpDebug.append(bb);
            }
            qDebug() << len + fullLen - size << "bytes to the rxBuf" << tmpDebug.toHex(' ');
            qDebug() << size << QByteArray((const char *)b, size).toHex(' ');
            return size;
        }
        for (int i = fullLen; i < len + fullLen; i++) {
            bytes[i] = b[i - fullLen];
        }
        qDebug() << len << QByteArray((const char *)b, len).toHex(' ');
        fullLen += len;
    }

    qDebug() << "FULL BUFFER RX: << " << fullLen << QByteArray((const char *)bytes, size).toHex(' ');
    cleanFrame = true;

    return fullLen;
#elif defined(WIN32)
    Q_UNUSED(size);
    // Readfile deals with timeouts and readyread issues
    DWORD cBytes;
    rc = ReadFile(devicePort, bytes, 7, &cBytes, NULL);
    if (rc)
        return (int)cBytes;
    else
        return (-1);

#else

    int timeout = 0, i = 0;
    uint8_t byte;

    // read one byte at a time sleeping when no data ready
    // until we timeout waiting then return error
    for (i = 0; i < size; i++) {
        timeout = 0;
        rc = 0;
        while (rc == 0 && timeout < CT_READTIMEOUT) {
            rc = read(devicePort, &byte, 1);
            if (rc == -1)
                return -1; // error!
            else if (rc == 0) {
                msleep(50); // sleep for 1/20th of a second
                timeout += 50;
            } else {
                bytes[i] = byte;
            }
        }
        if (timeout >= CT_READTIMEOUT)
            return -1; // we timed out!
    }

    qDebug() << i << QString::fromLocal8Bit((const char *)bytes, i);

    return i;

#endif
}

// check to see of there is a port at the device specified
// returns true if the device exists and false if not
bool Serialport::discover(QString filename) {
    uint8_t *greeting = (uint8_t *)"RacerMate";
    uint8_t handshake[7];
    int rc;

    if (filename.isEmpty())
        return false; // no null filenames thanks

    // lets set the port
    setDevice(filename);

    // lets open it
    openPort();

    // send a probe
    if ((rc = rawWrite(greeting, 9)) == -1) {
        closePort();
        return false;
    }

    // did we get something back from the device?
    if ((rc = rawRead(handshake, 6)) < 6) {
        closePort();
        return false;
    }

    closePort();

 
}
