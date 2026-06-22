#pragma once

#include <memory>
#include <string>

#include <QDebug>
#include <QString>

#include "Level.h"
#include "SimpleFormatter.h"
#include "Sink.h"
#include "detail/NullMutex.h"

namespace logging {

/**
 * @brief Qt output sink for Qt Creator/Application Output integration.
 *
 * Debug/Trace -> qDebug, Info -> qInfo, Warn -> qWarning, Error/Critical -> qCritical.
 */
template<typename Mutex>
class QtSink : public BaseSink<Mutex> {
    using BaseSink<Mutex>::formatter_;

public:
    QtSink() {
        formatter_ = std::make_unique<SimpleFormatter>();
    }

protected:
    void SinkItImpl(const Record& record) override {
        const QString text = QString::fromUtf8(formatter_->Format(record).c_str());
        switch (record.GetLevel()) {
        case Level::Trace:
        case Level::Debug:
            qDebug().noquote() << text;
            break;
        case Level::Info:
            qInfo().noquote() << text;
            break;
        case Level::Warn:
            qWarning().noquote() << text;
            break;
        case Level::Error:
        case Level::Critical:
            qCritical().noquote() << text;
            break;
        case Level::Off:
            break;
        }
    }

    void FlushImpl() override {
    }
};

using QtSinkST = QtSink<detail::NullMutex>;
using QtSinkMT = QtSink<std::mutex>;

} // namespace logging
