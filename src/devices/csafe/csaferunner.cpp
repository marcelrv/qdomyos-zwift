#include "csaferunner.h"

CsafeRunnerThread::CsafeRunnerThread() {}

CsafeRunnerThread::CsafeRunnerThread(QString deviceFileName, int sleepTime) {
    setDevice(deviceFileName);
    setSleepTime(sleepTime);
}

void CsafeRunnerThread::setDevice(const QString &device) { deviceName = device; }

void CsafeRunnerThread::setBaudRate(speed_t _baudRate) { baudRate = _baudRate; }

void CsafeRunnerThread::setSleepTime(int time) { sleepTime = time; }

void CsafeRunnerThread::setRefreshCommands(const QStringList &commands) { refreshCommands = commands; }

void CsafeRunnerThread::sendCommand(const QStringList &commands) {
    mutex.lock();
    if (commandQueue.size() < MAX_QUEUE_SIZE) {
        commandQueue.enqueue(commands);
        mutex.unlock();
    } else {
        qDebug() << "CSAFE port commands QUEUE FULL!!!!!!!!!!!!!!!!!!!!!!!!!!!";
        
    }
    qDebug() << "CSAFE port commands RECEIVED!!!!!!!!!!!!!!!!!!!!!!!!!!!";
}

void CsafeRunnerThread::run() {

    int rc = 0;

    SerialHandler *serial = SerialHandler::create(deviceName, baudRate);
    serial->setEndChar(0xf2); // end of frame for CSAFE
    serial->setTimeout(1200); // CSAFE spec says 1s timeout

    csafe *csafeInstance = new csafe();
    int connectioncounter = 20; // counts timeouts. If 10 timeouts in a row, then the port is closed and reopened

    while (1) {

        if (connectioncounter > 10) { //! serial->isOpen()) {
            rc = serial->openPort();
            if (rc != 0) {
                emit portAvailable(false);
                connectioncounter++;
                qDebug() << "Error opening serial port " << deviceName << "rc=" << rc << " sleeping for "
                         << "10s";
                QThread::msleep(10000);
                continue;
            } else {
                emit portAvailable(true);
                connectioncounter = 0;
            }
        }

        mutex.lock();
        if (!commandQueue.isEmpty()) {
            qDebug() << "CSAFE port commands PROCESSSED!!!!!!!!!!!!!!!!!!!!!!!!!!!";
        }
        QByteArray ret = csafeInstance->write(commandQueue.isEmpty() ? refreshCommands : commandQueue.dequeue(), false);
        mutex.unlock();

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
        QVariantMap frame = csafeInstance->read(v);
        //  qDebug() << f;

        emit onCsafeFrame(frame);

        memset(rx, 0x00, sizeof(rx));
        QThread::msleep(sleepTime);
    }
    serial->closePort();
}
