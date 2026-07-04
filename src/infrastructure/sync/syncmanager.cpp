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

void syncmanager::SetSyncType(SYNCTYPE type) {
    QMutexLocker locker(&mutex);
    currentSYNCTYPE = type;
}

SYNCTYPE syncmanager::SyncType() const {
    QMutexLocker locker(&mutex);
    return currentSYNCTYPE;
}

int syncmanager::SeekTo(double clockMs) {
    QMutexLocker locker(&mutex);
    mainclock = clockMs;

    return 0;
}

int syncmanager::Forward() {
    QMutexLocker locker(&mutex);
    mainclock += 3000;

    return 0;
}
