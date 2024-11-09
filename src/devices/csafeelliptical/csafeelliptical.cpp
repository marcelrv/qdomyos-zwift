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
 */

#include "csafeelliptical.h"

using namespace std::chrono_literals;

csafeelliptical::csafeelliptical(bool noWriteResistance, bool noHeartService, int8_t bikeResistanceOffset,
                                 double bikeResistanceGain) {
    m_watt.setType(metric::METRIC_WATT);
    Speed.setType(metric::METRIC_SPEED);
    refresh = new QTimer(this);
    this->noWriteResistance = noWriteResistance;
    this->noHeartService = noHeartService;
    this->noVirtualDevice = false; // noVirtualDevice;
    initDone = false;
    connect(refresh, &QTimer::timeout, this, &csafeelliptical::update);
    refresh->start(200ms);
    csafeellipticalThread *t = new csafeellipticalThread();
    connect(t, &csafeellipticalThread::onPower, this, &csafeelliptical::onPower);
    connect(t, &csafeellipticalThread::onCadence, this, &csafeelliptical::onCadence);
    connect(t, &csafeellipticalThread::onHeart, this, &csafeelliptical::onHeart);
    connect(t, &csafeellipticalThread::onCalories, this, &csafeelliptical::onCalories);
    connect(t, &csafeellipticalThread::onDistance, this, &csafeelliptical::onDistance);
    connect(t, &csafeellipticalThread::onPace, this, &csafeelliptical::onPace);
    connect(t, &csafeellipticalThread::onStatus, this, &csafeelliptical::onStatus);
    connect(t, &csafeellipticalThread::onSpeed, this, &csafeelliptical::onSpeed);
    emit debug(QStringLiteral("init  bikeResistanceOffset ") + QString::number(bikeResistanceOffset));
    emit debug(QStringLiteral("init  bikeResistanceGain ") + QString::number(bikeResistanceGain));
    t->start();
}

// speed
// pace
// resistance
// currentInclination
// elevationGain

void csafeelliptical::onPace(double pace) {
    qDebug() << "Current Pace received:" << pace;
    if (distanceIsChanging && pace > 0)
        Speed = (60.0 / (double)(pace)) * 60.0;
    else
        Speed = 0;

    qDebug() << "Current Speed calculated:" << Speed.value() << pace;
}

void csafeelliptical::onSpeed(double speed) {
    qDebug() << "Current Speed received:" << speed;
    // if(distanceIsChanging)
    Speed = speed;
}

void csafeelliptical::onPower(double power) {
    qDebug() << "Current Power received:" << power;
    if (distanceIsChanging)
        m_watt = power;
}

void csafeelliptical::onCadence(double cadence) {
    qDebug() << "Current Cadence received:" << cadence;
    // if(distanceIsChanging)
    //     Cadence = cadence;
}

void csafeelliptical::onHeart(double hr) {
    qDebug() << "Current Heart received:" << hr;
    QSettings settings;
    QString heartRateBeltName =
        settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();
    bool disable_hr_frommachinery =
        settings.value(QZSettings::heart_ignore_builtin, QZSettings::default_heart_ignore_builtin).toBool();

#ifdef Q_OS_ANDROID
    if (settings.value(QZSettings::ant_heart, QZSettings::default_ant_heart).toBool())
        Heart = (uint8_t)KeepAwakeHelper::heart();
    else
#endif
    {
        if (heartRateBeltName.startsWith(QStringLiteral("Disabled"))) {
            uint8_t heart = ((uint8_t)hr);
            if (heart == 0 || disable_hr_frommachinery) {
                update_hr_from_external();
            } else
                Heart = heart;
        }
    }
}

void csafeelliptical::onCalories(double calories) {
    qDebug() << "Current Calories received:" << calories;
    KCal = calories;
}

void csafeelliptical::onDistance(double distance) {
    qDebug() << "Current Distance received:" << distance / 1000.0;

    if (distance != distanceReceived.value()) {
        distanceIsChanging = true;
        distanceReceived = distance;
    } else if (abs(distanceReceived.lastChanged().secsTo(QDateTime::currentDateTime())) > 20) {
        // TODO: check if this is still needed

        distanceIsChanging = false;
        distanceIsChanging = true;
        m_watt = 0.0;
        Cadence = 0.0;
        Speed = 0.0;
    }
}

void csafeelliptical::onStatus(uint16_t status) { qDebug() << "Current Status received:" << status; }

csafeellipticalThread::csafeellipticalThread() {}

void csafeellipticalThread::run() {
    QSettings settings;
    /*devicePort =
        settings.value(QZSettings::computrainer_serialport, QZSettings::default_computrainer_serialport).toString();*/

    openPort();
    csafe *aa = new csafe();
    int p = 0;
    while (1) {

        QStringList command;
        //        command << "CSAFE_PM_GET_WORKTIME";
        //       command << "CSAFE_PM_GET_WORKDISTANCE";
        //        command << "CSAFE_GETCADENCE_CMD";  //not supported
        if (p == 0) {
            command << "CSAFE_GETPOWER_CMD";
        }
        if (p == 1) {
            command << "CSAFE_GETSPEED_CMD";
        } else if (p == 2) {
            command << "CSAFE_GETCALORIES_CMD";
        } else if (p == 3) {
            command << "CSAFE_GETHRCUR_CMD";
        } else if (p == 4) {
            //            command << "CSAFE_GETPACE_CMD"; //not supported
            command << "CSAFE_GETHORIZONTAL_CMD";

        } else if (p == 5) {
            command << "CSAFE_GETSTATUS_CMD";
        }
        if (p == 6) {
            command << "CSAFE_GETHORIZONTAL_CMD";
        }
        p++;
        if (p > 6)
            p = 0;

        //        command << "CSAFE_GETPOWER_CMD";
        //        command << "CSAFE_GETCALORIES_CMD";
        //        command << "CSAFE_GETHRCUR_CMD";
        //        command << "CSAFE_GETPACE_CMD";
        //        command << "CSAFE_GETSTATUS_CMD";
        QByteArray ret = aa->write(command, false);

        qDebug() << " >> " << ret.toHex(' ');
        rawWrite((uint8_t *)ret.data(), ret.length());
        static uint8_t rx[100];
        rawRead(rx, 100);
        qDebug() << " << " << QByteArray::fromRawData((const char *)rx, 64).toHex(' ');

        QVector<quint8> v;
        for (int i = 0; i < 64; i++)
            v.append(rx[i]);
        QVariantMap f = aa->read(v);
        if (f["CSAFE_GETCADENCE_CMD"].isValid()) {
            emit onCadence(f["CSAFE_GETCADENCE_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETPACE_CMD"].isValid()) {
            emit onPace(f["CSAFE_GETPACE_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETSPEED_CMD"].isValid()) {
            emit onSpeed(f["CSAFE_GETSPEED_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETPOWER_CMD"].isValid()) {
            emit onPower(f["CSAFE_GETPOWER_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETHRCUR_CMD"].isValid()) {
            emit onHeart(f["CSAFE_GETHRCUR_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETCALORIES_CMD"].isValid()) {
            emit onCalories(f["CSAFE_GETCALORIES_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_PM_GET_WORKDISTANCE"].isValid()) {
            emit onDistance(f["CSAFE_PM_GET_WORKDISTANCE"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETHORIZONTAL_CMD"].isValid()) {
            emit onDistance(f["CSAFE_GETHORIZONTAL_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETSTATUS_CMD"].isValid()) {
            emit onStatus(f["CSAFE_GETSTATUS_CMD"].value<QVariantList>()[0].toUInt());
        }

        memset(rx, 0x00, sizeof(rx));
        QThread::msleep(50);
    }
    closePort();
}

int csafeellipticalThread::closePort() {
#ifdef WIN32
    return (int)!CloseHandle(devicePort);
#else
    tcflush(devicePort, TCIOFLUSH); // clear out the garbage
    return close(devicePort);
#endif
}

int csafeellipticalThread::openPort() {
#ifdef Q_OS_ANDROID
    QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/csafeellipticalUSBHID", "open",
                                              "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
#elif !defined(WIN32)

    // LINUX AND MAC USES TERMIO / IOCTL / STDIO

#if defined(Q_OS_MACX)
    int ldisc = TTYDISC;
#else
    int ldisc = N_TTY; // LINUX
#endif

    QSettings settings;

    QString deviceFilename =
        settings.value(QZSettings::csafe_elliptical_port, QZSettings::default_csafe_elliptical_port).toString();

    qDebug() << "Device Filename:" << deviceFilename;
    qDebug() << QStringLiteral("Opening CSAVE communication...");

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
    cfsetspeed(&deviceSettings, B9600);

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

#endif
    // success
    return 0;
}

int csafeellipticalThread::rawWrite(uint8_t *bytes, int size) // unix!!
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
    QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/csafeellipticalUSBHID", "write", "([B)V", d);
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

int csafeellipticalThread::rawRead(uint8_t bytes[], int size) {
    int rc = 0;

#ifdef Q_OS_ANDROID
    int64_t start = QDateTime::currentMSecsSinceEpoch();
    jint len = 0;

    do {
        QAndroidJniEnvironment env;
        QAndroidJniObject dd = QAndroidJniObject::callStaticObjectMethod(
            "org/cagnulen/qdomyoszwift/csafeellipticalUSBHID", "read", "()[B");
        len = QAndroidJniObject::callStaticMethod<jint>("org/cagnulen/qdomyoszwift/csafeellipticalUSBHID", "readLen",
                                                        "()I");
        if (len > 0) {
            jbyteArray d = dd.object<jbyteArray>();
            jbyte *b = env->GetByteArrayElements(d, 0);
            for (int i = 0; i < len; i++) {
                bytes[i] = b[i];
            }
            qDebug() << len << QByteArray((const char *)b, len).toHex(' ');
        }
    } while (len == 0 && start + 2000 > QDateTime::currentMSecsSinceEpoch());

    return len;
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

void csafeelliptical::update() {
    QSettings settings;
    QString heartRateBeltName =
        settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();

    update_metrics(false, watts());

/*
    qDebug() << QStringLiteral("Current speed: ") + QString::number(Speed.value());
    qDebug() << QStringLiteral("Current incline: ") + QString::number(Inclination.value());
    qDebug() << QStringLiteral("Current heart: ") + QString::number(Heart.value());
    qDebug() << QStringLiteral("Current KCal: ") + QString::number(KCal.value());
    qDebug() << QStringLiteral("Current KCal from the machine: ") + QString::number(KCal.value());
    qDebug() << QStringLiteral("Current Distance: ") + QString::number(Distance.value());
    qDebug() << QStringLiteral("Current Distance Calculated: ") + QString::number(Distance.value());
*/

    /*
        if (Cadence.value() > 0) {
            CrankRevs++;
            LastCrankEventTime += (uint16_t)(1024.0 / (((double)(Cadence.value())) / 60.0));
        }


        Distance += ((Speed.value() / (double)3600.0) /
                     ((double)1000.0 /
       (double)(lastRefreshCharacteristicChanged.msecsTo(QDateTime::currentDateTime()))));

    */
    lastRefreshCharacteristicChanged = QDateTime::currentDateTime();

    // ******************************************* virtual treadmill init *************************************
    if (!firstStateChanged && !this->hasVirtualDevice()
#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
        && !h
#endif
#endif
    ) {
        QSettings settings;
        bool virtual_device_enabled =
            settings.value(QZSettings::virtual_device_enabled, QZSettings::default_virtual_device_enabled).toBool();

#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
        bool cadence =
            settings.value(QZSettings::bike_cadence_sensor, QZSettings::default_bike_cadence_sensor).toBool();
        bool ios_peloton_workaround =
            settings.value(QZSettings::ios_peloton_workaround, QZSettings::default_ios_peloton_workaround).toBool();
        if (ios_peloton_workaround && cadence && !virtual_device_rower) {
            qDebug() << "ios_peloton_workaround activated!";
            h = new lockscreen();
            h->virtualbike_ios();
        } else
#endif
#endif
            bool virtual_device_force_bike =
                settings.value(QZSettings::virtual_device_force_bike, QZSettings::default_virtual_device_force_bike)
                    .toBool();
        if (virtual_device_enabled) {
            if (!virtual_device_force_bike) {
                debug("creating virtual treadmill interface...");
                auto virtualTreadmill = new virtualtreadmill(this, noHeartService);
                connect(virtualTreadmill, &virtualtreadmill::debug, this, &csafeelliptical::debug);
                connect(virtualTreadmill, &virtualtreadmill::changeInclination, this,
                        &csafeelliptical::changeInclinationRequested);
                this->setVirtualDevice(virtualTreadmill, VIRTUAL_DEVICE_MODE::PRIMARY);
            } else {
                debug("creating virtual bike interface...");
                auto virtualBike = new virtualbike(this);
                //        auto virtualBike = new virtualbike(this, noWriteResistance, noHeartService);
                connect(virtualBike, &virtualbike::changeInclination, this,
                        &csafeelliptical::changeInclinationRequested);
                connect(virtualBike, &virtualbike::ftmsCharacteristicChanged, this,
                        &csafeelliptical::ftmsCharacteristicChanged);
                this->setVirtualDevice(virtualBike, VIRTUAL_DEVICE_MODE::ALTERNATIVE);
            }
        }
        // ********************************************************************************************************
    }
    if (!firstStateChanged)
        emit connectedAndDiscovered();

    /*        m_watt = 0.0;
            Cadence = 0.0;
            Speed = 0.0;
            Distance = 0.0;
            Heart = 75;
            KCal = 0;
            Inclination = 0;

                emit onCadence(0.0);
                emit onPace(0.0);
                emit onSpeed(0.0);
                emit onPower(0.0);
                emit onHeart(0.0);
                emit onCalories(0.0);
                emit onDistance(0.0);
                emit onStatus(1);
    */
    firstStateChanged = 1;
    // ********************************************************************************************************

    if (!noVirtualDevice) {
#ifdef Q_OS_ANDROID
        if (settings.value(QZSettings::ant_heart, QZSettings::default_ant_heart).toBool()) {
            Heart = (uint8_t)KeepAwakeHelper::heart();
            debug("Current Heart: " + QString::number(Heart.value()));
        }
#endif
        if (heartRateBeltName.startsWith(QStringLiteral("Disabled"))) {
            update_hr_from_external();
        }

#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
        bool cadence =
            settings.value(QZSettings::bike_cadence_sensor, QZSettings::default_bike_cadence_sensor).toBool();
        bool ios_peloton_workaround =
            settings.value(QZSettings::ios_peloton_workaround, QZSettings::default_ios_peloton_workaround).toBool();
        if (ios_peloton_workaround && cadence && h && firstStateChanged) {
            h->virtualbike_setCadence(currentCrankRevolutions(), lastCrankEventTime());
            h->virtualbike_setHeartRate((uint8_t)metrics_override_heartrate());
        }
#endif
#endif
    }

    /*
    if (Heart.value()) {
        static double lastKcal = 0;
        if (KCal.value() < 0) // if the user pressed stop, the KCAL resets the accumulator
            lastKcal = abs(KCal.value());
        KCal = metric::calculateKCalfromHR(Heart.average(), elapsed.value()) + lastKcal;
    }*/

    if (requestResistance != -1 && requestResistance != currentResistance().value()) {
        //    Resistance = requestResistance;
        emit debug(QStringLiteral("writing resistance ") + QString::number(requestResistance));
    }
}

void csafeelliptical::ftmsCharacteristicChanged(const QLowEnergyCharacteristic &characteristic,
                                                const QByteArray &newValue) {
    QByteArray b = newValue;
    qDebug() << "routing FTMS packet to the bike from virtualbike" << characteristic.uuid() << newValue.toHex(' ');
}

void csafeelliptical::changeInclinationRequested(double grade, double percentage) {
    if (percentage < 0)
        percentage = 0;
    //   changeInclination(grade, percentage);
    emit debug(QStringLiteral("Running    changeInclination(grade, percentage);"));
    emit debug(QStringLiteral("writing grade ") + QString::number(grade));
    emit debug(QStringLiteral("writing percentage  ") + QString::number(percentage));
}

bool csafeelliptical::connected() { return true; }

void csafeelliptical::deviceDiscovered(const QBluetoothDeviceInfo &device) {
    emit debug(QStringLiteral("Found new device: ") + device.name() + " (" + device.address().toString() + ')');
}

void csafeelliptical::serviceDiscovered(const QBluetoothUuid &gatt) {
    emit debug(QStringLiteral("serviceDiscovered ") + gatt.toString());
}

void csafeelliptical::newPacket(QByteArray p) {}

uint16_t csafeelliptical::watts() { return m_watt.value(); }
