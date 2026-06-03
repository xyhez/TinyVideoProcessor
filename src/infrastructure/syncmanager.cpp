#include "syncmanager.h"

syncmanager::syncmanager(QObject *parent)
    : QObject{parent}
{

}

syncmanager::~syncmanager() {

}

int syncmanager::refreshClock(double clock) {
    mutex.lock();
    mainclock = clock;
    mutex.unlock();

    return 0;
}

double syncmanager::getClock() const {
    return mainclock;
}

int syncmanager::Forword() {
    mutex.lock();
    mainclock+=3000;
    mutex.unlock();

    return 0;
}
