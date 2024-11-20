#include "csaferunner.h"

CsafeRunnerThread::CsafeRunnerThread() {}

CsafeRunnerThread::CsafeRunnerThread(QString deviceFileName, int sleepTime) {
    setDevice(deviceFileName);
    setSleepTime(sleepTime);
}

void CsafeRunnerThread::setDevice(const QString &device) { deviceName = device; }

void CsafeRunnerThread::setBaudRate(speed_t _baudRate) { baudRate = _baudRate; }

void CsafeRunnerThread::setSleepTime(int time) { sleepTime = time; }

void CsafeRunnerThread::run() {
    /*  QSettings settings;

      QString deviceFilename =
          settings.value(QZSettings::csafe_elliptical_port, QZSettings::default_csafe_elliptical_port).toString();
  */
    int rc = 0;

    SerialHandler *serial = SerialHandler::create(deviceName, B9600);
    serial->setEndChar(0xf2); // end of frame for CSAFE
    serial->setTimeout(1200); // CSAFE spec says 1s timeout

    csafe *csafeInstance = new csafe();
    // int p = 0;
    int connectioncounter = 20;
    int lastStatus = -1;
    while (1) {

        if (connectioncounter > 10) { //! serial->isOpen()) {
            rc = serial->openPort();
            if (rc != 0) {
                emit portAvailable(false);
                connectioncounter++;
                qDebug() << "Error opening serial port " << deviceName << "rc=" << rc << " sleeping for "
                         << connectioncounter << "s";
                QThread::msleep(connectioncounter * 1000);
                continue;
            } else {
                emit portAvailable(true);
                connectioncounter = 0;
            }
        }

        QStringList command;

        command << "CSAFE_GETPOWER_CMD";
        command << "CSAFE_GETSPEED_CMD";
        command << "CSAFE_GETCALORIES_CMD";
        command << "CSAFE_GETHRCUR_CMD";
        command << "CSAFE_GETHORIZONTAL_CMD";

        QByteArray ret = csafeInstance->write(command, false);

        qDebug() << "CSAFE >> " << ret.toHex(' ');
        rc = serial->rawWrite((uint8_t *)ret.data(), ret.length());
        if (rc < 0) {
            qDebug() << "Error writing serial port " << deviceName << "rc=" << rc;
            connectioncounter++;
            continue;
        }

        static uint8_t rx[120];
        rc = serial->rawRead(rx, 100, true);
        if (rc > 0) {
            qDebug() << "CSAFE << " << QByteArray::fromRawData((const char *)rx, rc).toHex(' ');
        } else {
            qDebug() << "Error reading serial port " << deviceName << " rc=" << rc;
            connectioncounter++;
            continue;
        }

        // TODO: check if i needs to be set to rc to process full line

        QVector<quint8> v;
        for (int i = 0; i < 64; i++)
            v.append(rx[i]);
        QVariantMap f = csafeInstance->read(v);
        //  qDebug() << f;

        emit onCsafeFrame(f);

        memset(rx, 0x00, sizeof(rx));
        QThread::msleep(250);
    }
    serial->closePort();
}
