/*
 * Copyright (c) 2024 Marcel Verpaalen (marcel@verpaalen.com)
 * based on csaferower
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

csafeelliptical::csafeelliptical(bool noWriteResistance, bool noHeartService, bool noVirtualDevice,
                                 int8_t bikeResistanceOffset, double bikeResistanceGain) {
    m_watt.setType(metric::METRIC_WATT);
    Speed.setType(metric::METRIC_SPEED);
    refresh = new QTimer(this);
    this->noWriteResistance = noWriteResistance;
    this->noHeartService = noHeartService;
    this->noVirtualDevice = noVirtualDevice; // noVirtualDevice;
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
    connect(t, &csafeellipticalThread::portavailable, this, &csafeelliptical::portavailable);
    connect(t, &csafeellipticalThread::onCsafeFrame, this, &csafeelliptical::onCsafeFrame);
    t->start();
}

// Life Fitness 95x does not return pace. Other models might
void csafeelliptical::onPace(double pace) {
    qDebug() << "Current Pace received:" << pace << " updated:" << distanceIsChanging;
    if (distanceIsChanging && pace > 0)
        Speed = (60.0 / (double)(pace)) * 60.0;
    else
        Speed = 0;
    qDebug() << "Current Speed calculated:" << Speed.value() << pace;
}

void csafeelliptical::onSpeed(double speed) {
    qDebug() << "Current Speed received:" << speed << " updated:" << distanceIsChanging;
    if (distanceIsChanging)
        Speed = speed;
}

void csafeelliptical::onPower(double power) {
    qDebug() << "Current Power received:" << power << " updated:" << distanceIsChanging;
    if (distanceIsChanging)
        m_watt = power;
}

void csafeelliptical::onCadence(double cadence) {
    qDebug() << "Current Cadence received:" << cadence << " updated:" << distanceIsChanging;
    if (distanceIsChanging)
        Cadence = cadence;
}

void csafeelliptical::onCsafeFrame(const QVariantMap &frame) {
    qDebug() << "Current CSAFE frame received:" << frame;
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
    qDebug() << "Current Distance received:" << distance / 1000.0 << " updated:" << distanceReceived.value();
    Distance = distance / 1000.0;

    if (distance != distanceReceived.value() &&
        abs(distanceReceived.lastChanged().secsTo(QDateTime::currentDateTime())) > 0) {
        distanceIsChanging = true;

        qDebug() << "Current Distance received:" << distance / 1000.0 << " PREVIOUS:" << distanceReceived.value();

        if (distanceReceived.value() > 1) {
            Speed = 3600 * (distance - distanceReceived.value()) /
                    abs(distanceReceived.lastChanged().msecsTo(QDateTime::currentDateTime()));

            qDebug() << abs(distanceReceived.lastChanged().secsTo(QDateTime::currentDateTime())) << "MS "
                     << distanceReceived.lastChanged().msecsTo(QDateTime::currentDateTime());
            qDebug() << "speed" << Speed.value();
        }
        distanceReceived = distance;
    } else if (abs(distanceReceived.lastChanged().secsTo(QDateTime::currentDateTime())) > 30) {
        distanceIsChanging = false;
        m_watt = 0.0;
        Cadence = 0.0;
        Speed = 0.0;
    }
}

void csafeelliptical::onStatus(char status) {
    QString statusString = CSafeUtility::statusByteToText(status);
    qDebug() << "Current Status code:" << status << " status: " << statusString;

    /*
        0x00: Error
    0x01: Ready
    0x02: Idle
    0x03: Have ID
    0x05: In Use
    0x06: Pause
    0x07: Finish
    0x08: Manual
    0x09: Off line
    */
}

void csafeelliptical::portavailable(bool available) {
    if (available) {
        qDebug() << "CSAFE port available";
        _connected = true;
    } else {
        qDebug() << "CSAFE port not available";
        _connected = false;
    }
}

csafeellipticalThread::csafeellipticalThread() {}

void csafeellipticalThread::run() {
    QSettings settings;

    QString deviceFilename =
        settings.value(QZSettings::csafe_elliptical_port, QZSettings::default_csafe_elliptical_port).toString();

    int rc = 0;

    SerialHandler *serial = SerialHandler::create(deviceFilename, B9600);
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
                emit portavailable(false);
                connectioncounter++;
                qDebug() << "Error opening serial port " << deviceFilename << "rc=" << rc << " sleeping for "
                         << connectioncounter << "s";
                QThread::msleep(connectioncounter * 1000);
                continue;
            } else {
                emit portavailable(true);
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
            qDebug() << "Error writing serial port " << deviceFilename << "rc=" << rc;
            connectioncounter++;
            continue;
        }

        static uint8_t rx[120];
        rc = serial->rawRead(rx, 100, true);
        if (rc > 0) {
            qDebug() << "CSAFE << " << QByteArray::fromRawData((const char *)rx, rc).toHex(' ');
        } else {
            qDebug() << "Error reading serial port " << deviceFilename << " rc=" << rc;
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

        if (f["CSAFE_GETCADENCE_CMD"].isValid()) {
            emit onCadence(f["CSAFE_GETCADENCE_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETPACE_CMD"].isValid()) {
            emit onPace(f["CSAFE_GETPACE_CMD"].value<QVariantList>()[0].toDouble());
        }
        if (f["CSAFE_GETSPEED_CMD"].isValid()) {
            double speed = f["CSAFE_GETSPEED_CMD"].value<QVariantList>()[0].toDouble();
            int unit = f["CSAFE_GETSPEED_CMD"].value<QVariantList>()[1].toInt();
            qDebug() << "Speed value:" << speed << "unit:" << CSafeUtility::getUnitName(unit) << "(" << unit << ")";

            if (unit == 82) { // revs/minute
                emit onCadence(speed);
                //   emit onSpeed(CSafeUtility::convertToStandard(unit, speed) * 60 * 2.35 / 1000);
            } else {
                emit onSpeed(CSafeUtility::convertToStandard(unit, speed));
            }
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
        if (f["CSAFE_GETHORIZONTAL_CMD"].isValid()) {
            double distance = f["CSAFE_GETHORIZONTAL_CMD"].value<QVariantList>()[0].toDouble();
            int unit = f["CSAFE_GETHORIZONTAL_CMD"].value<QVariantList>()[1].toInt();
            qDebug() << "Distance value:" << distance << "unit:" << CSafeUtility::getUnitName(unit) << "(" << unit
                     << ")" << CSafeUtility::convertToStandard(unit, distance);
            emit onDistance(CSafeUtility::convertToStandard(unit, distance));
        }
        if (f["CSAFE_GETSTATUS_CMD"].isValid()) {
            u_int16_t statusvalue = f["CSAFE_GETSTATUS_CMD"].value<QVariantList>()[0].toUInt();
            qDebug() << "Status value:" << statusvalue << " lastStatus:" << lastStatus
                     << " statusvalue & 0x0f:" << (statusvalue & 0x0f);
            if (statusvalue != lastStatus) {
                lastStatus = statusvalue;
                char statusChar = static_cast<char>(statusvalue & 0x0f);
                emit onStatus(statusChar);
            }
        }

        memset(rx, 0x00, sizeof(rx));
        QThread::msleep(250);
    }
    serial->closePort();
}

void csafeelliptical::update() {
    QSettings settings;
    QString heartRateBeltName =
        settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();

    update_metrics(true, watts());
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
        } else {
            debug("not creating virtual interface... not enabled");
        }
        // ********************************************************************************************************
    }

    if (!firstStateChanged) {
        emit connectedAndDiscovered();
    }

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
        Resistance = requestResistance;
        emit debug(QStringLiteral("Writing resistance ") + QString::number(requestResistance));
    }
}

void csafeelliptical::ftmsCharacteristicChanged(const QLowEnergyCharacteristic &characteristic,
                                                const QByteArray &newValue) {
    QByteArray b = newValue;
    qDebug() << "Routing FTMS packet to the bike from virtualbike" << characteristic.uuid() << newValue.toHex(' ');
}

void csafeelliptical::changeInclinationRequested(double grade, double percentage) {
    if (percentage < 0)
        percentage = 0;
    //   changeInclination(grade, percentage);
    emit debug(QStringLiteral("Running    changeInclination(grade, percentage);"));
    emit debug(QStringLiteral("Writing grade ") + QString::number(grade));
    emit debug(QStringLiteral("Writing percentage  ") + QString::number(percentage));
}

bool csafeelliptical::connected() { return _connected; }

void csafeelliptical::deviceDiscovered(const QBluetoothDeviceInfo &device) {
    emit debug(QStringLiteral("Found new device: ") + device.name() + " (" + device.address().toString() + ')');
}

uint16_t csafeelliptical::watts() { return m_watt.value(); }
