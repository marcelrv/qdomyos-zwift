#include "csafe.h"
#include "qzsettings.h"
#include "serialhandler.h"
#include <QDebug>
#include <QSettings>
#include <QThread>
#include <QVariantMap>
#include <QVector>

class CsafeRunnerThread : public QThread {
    Q_OBJECT

  public:
    explicit CsafeRunnerThread();
    explicit CsafeRunnerThread(QString deviceFileName, int sleepTime = 200);
    void setDevice(const QString &device);
    void setBaudRate(speed_t baudRate = B9600);
    void setSleepTime(int time);
    void setRefreshCommands(const QStringList &commands);
    void run();

  signals:
    void onCsafeFrame(const QVariantMap &frame);
    void portAvailable(bool available);

  private:
    QString deviceName;
    speed_t baudRate = B9600;
    int sleepTime = 200;
    QStringList refreshCommands;
};
