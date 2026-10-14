#include <QtTest>

#include <chrono>
#include <future>
#include <memory>

#include "core/queue.h"

using namespace std::chrono_literals;

class SyncQueueTest : public QObject {
    Q_OBJECT

private slots:
    void pushPopMaintainsOrder();
    void fullQueueBlocksProducerUntilPop();
    void interruptUnblocksWaitingConsumer();
    void stopAndResumeControlsQueueLifecycle();
    void popWithStatusPreservesPriorityAndItems();
    void endOfStreamDrainsItemsAndRejectsPush();
    void endOfStreamUnblocksWaitingOperations();
    void resumeWaitOnlyClearsInterrupt();
};

void SyncQueueTest::pushPopMaintainsOrder()
{
    SyncQueue<std::unique_ptr<int>> queue(2);

    QVERIFY(queue.Push(std::make_unique<int>(11)));
    QVERIFY(queue.Push(std::make_unique<int>(22)));
    QCOMPARE(queue.Size(), size_t(2));

    std::unique_ptr<int> first;
    std::unique_ptr<int> second;
    QVERIFY(queue.Pop(first));
    QVERIFY(queue.Pop(second));
    QCOMPARE(*first, 11);
    QCOMPARE(*second, 22);
    QVERIFY(queue.Empty());
}

void SyncQueueTest::fullQueueBlocksProducerUntilPop()
{
    SyncQueue<std::unique_ptr<int>> queue(1);
    QVERIFY(queue.Push(std::make_unique<int>(1)));

    auto producer = std::async(std::launch::async, [&queue]() {
        return queue.Push(std::make_unique<int>(2));
    });

    QCOMPARE(producer.wait_for(50ms), std::future_status::timeout);

    std::unique_ptr<int> first;
    QVERIFY(queue.Pop(first));
    QCOMPARE(*first, 1);

    QVERIFY(producer.wait_for(1s) == std::future_status::ready);
    QVERIFY(producer.get());

    std::unique_ptr<int> second;
    QVERIFY(queue.Pop(second));
    QCOMPARE(*second, 2);
}

void SyncQueueTest::interruptUnblocksWaitingConsumer()
{
    SyncQueue<std::unique_ptr<int>> queue(1);
    std::promise<void> consumerStarted;

    auto consumer = std::async(std::launch::async, [&queue, &consumerStarted]() {
        consumerStarted.set_value();
        std::unique_ptr<int> item;
        return queue.Pop(item);
    });

    consumerStarted.get_future().wait();
    queue.InterruptWait();

    QVERIFY(consumer.wait_for(1s) == std::future_status::ready);
    QVERIFY(!consumer.get());
    QVERIFY(queue.IsWaitInterrupted());

    queue.ResumeWait();
    QVERIFY(!queue.IsWaitInterrupted());
    QVERIFY(queue.Push(std::make_unique<int>(3)));
}

void SyncQueueTest::stopAndResumeControlsQueueLifecycle()
{
    SyncQueue<std::unique_ptr<int>> queue(1);

    queue.RequestStop();
    QVERIFY(queue.IsStopRequested());
    QVERIFY(!queue.Push(std::make_unique<int>(1)));

    queue.Resume();
    QVERIFY(!queue.IsStopRequested());
    QVERIFY(queue.Push(std::make_unique<int>(2)));

    std::unique_ptr<int> item;
    QVERIFY(queue.Pop(item));
    QCOMPARE(*item, 2);
}

void SyncQueueTest::popWithStatusPreservesPriorityAndItems()
{
    SyncQueue<std::unique_ptr<int>> queue(2);
    QVERIFY(queue.Push(std::make_unique<int>(7)));
    queue.SignalEndOfStream();
    queue.InterruptWait();
    queue.RequestStop();

    std::unique_ptr<int> item;
    QVERIFY(queue.PopWithStatus(item) == SyncQueuePopResult::Stopped);
    QCOMPARE(queue.Size(), size_t(1));

    queue.SetStopFlag(false);
    QVERIFY(queue.PopWithStatus(item) == SyncQueuePopResult::Interrupted);
    QCOMPARE(queue.Size(), size_t(1));

    queue.ResumeWait();
    QVERIFY(queue.PopWithStatus(item) == SyncQueuePopResult::Item);
    QCOMPARE(*item, 7);
    QVERIFY(queue.PopWithStatus(item) == SyncQueuePopResult::EndOfStream);
}

void SyncQueueTest::endOfStreamDrainsItemsAndRejectsPush()
{
    SyncQueue<std::unique_ptr<int>> queue(2);
    QVERIFY(queue.Push(std::make_unique<int>(11)));

    queue.SignalEndOfStream();
    QVERIFY(queue.IsEndOfStream());
    QVERIFY(!queue.Push(std::make_unique<int>(22)));

    std::unique_ptr<int> item;
    QVERIFY(queue.PopWithStatus(item) == SyncQueuePopResult::Item);
    QCOMPARE(*item, 11);
    QVERIFY(queue.PopWithStatus(item) == SyncQueuePopResult::EndOfStream);

    queue.Resume();
    QVERIFY(!queue.IsStopRequested());
    QVERIFY(!queue.IsWaitInterrupted());
    QVERIFY(!queue.IsEndOfStream());
    QVERIFY(queue.Push(std::make_unique<int>(33)));
}

void SyncQueueTest::endOfStreamUnblocksWaitingOperations()
{
    SyncQueue<std::unique_ptr<int>> consumerQueue(1);
    std::promise<void> consumerStarted;
    auto consumer = std::async(std::launch::async, [&consumerQueue, &consumerStarted]() {
        consumerStarted.set_value();
        std::unique_ptr<int> item;
        return consumerQueue.PopWithStatus(item);
    });

    consumerStarted.get_future().wait();
    consumerQueue.SignalEndOfStream();
    QVERIFY(consumer.wait_for(1s) == std::future_status::ready);
    QVERIFY(consumer.get() == SyncQueuePopResult::EndOfStream);

    SyncQueue<std::unique_ptr<int>> producerQueue(1);
    QVERIFY(producerQueue.Push(std::make_unique<int>(1)));
    auto producer = std::async(std::launch::async, [&producerQueue]() {
        return producerQueue.Push(std::make_unique<int>(2));
    });

    QCOMPARE(producer.wait_for(50ms), std::future_status::timeout);
    producerQueue.SignalEndOfStream();
    QVERIFY(producer.wait_for(1s) == std::future_status::ready);
    QVERIFY(!producer.get());
}

void SyncQueueTest::resumeWaitOnlyClearsInterrupt()
{
    SyncQueue<std::unique_ptr<int>> queue(1);
    queue.SignalEndOfStream();
    queue.InterruptWait();

    queue.ResumeWait();
    QVERIFY(!queue.IsWaitInterrupted());
    QVERIFY(queue.IsEndOfStream());

    std::unique_ptr<int> item;
    QVERIFY(queue.PopWithStatus(item) == SyncQueuePopResult::EndOfStream);
}

QTEST_APPLESS_MAIN(SyncQueueTest)

#include "test_sync_queue.moc"
