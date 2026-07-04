#include "syncmanager.h"

#include <QMutexLocker>

SyncManager::SyncManager(QObject *parent)
    : QObject{parent}
{

}

SyncManager::~SyncManager() {

}

int SyncManager::RefreshClock(double clock) {
    QMutexLocker locker(&mutex);
    mainclock = clock;

    return 0;
}

double SyncManager::GetClock() const {
    QMutexLocker locker(&mutex);
    return mainclock;
}

void SyncManager::SetSyncType(SYNCTYPE type) {
    QMutexLocker locker(&mutex);
    currentSYNCTYPE = type;
}

SYNCTYPE SyncManager::SyncType() const {
    QMutexLocker locker(&mutex);
    return currentSYNCTYPE;
}

int SyncManager::SeekTo(double clockMs) {
    QMutexLocker locker(&mutex);
    mainclock = clockMs;

    return 0;
}

int SyncManager::Forward() {
    QMutexLocker locker(&mutex);
    mainclock += 3000;

    return 0;
}
