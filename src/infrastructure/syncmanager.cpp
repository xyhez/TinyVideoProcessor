#include "syncmanager.h"

#include <QMutexLocker>

syncmanager::syncmanager(QObject *parent)
    : QObject{parent}
{

}

syncmanager::~syncmanager() {

}

int syncmanager::RefreshClock(double clock) {
    QMutexLocker locker(&mutex);
    mainclock = clock;

    return 0;
}

double syncmanager::GetClock() const {
    QMutexLocker locker(&mutex);
    return mainclock;
}

int syncmanager::Forward() {
    QMutexLocker locker(&mutex);
    mainclock += 3000;

    return 0;
}
