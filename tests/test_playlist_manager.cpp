#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "domain/PlaylistManager.h"

class PlaylistManagerTest : public QObject {
    Q_OBJECT

private slots:
    void initialStateIsEmpty();
    void addFilesDeduplicatesAndSelectsFirst();
    void removeMaintainsCurrentItem();
    void playModesRespectBoundaries();
    void mutationsEmitExpectedSignals();
};

void PlaylistManagerTest::initialStateIsEmpty()
{
    PlaylistManager manager;

    QCOMPARE(manager.Count(), 0);
    QCOMPARE(manager.CurrentIndex(), -1);
    QVERIFY(!manager.CurrentItem().has_value());
    QVERIFY(!manager.MoveNext());
    QVERIFY(!manager.MovePrevious());
}

void PlaylistManagerTest::addFilesDeduplicatesAndSelectsFirst()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString firstPath = tempDir.filePath(QStringLiteral("first.mp4"));
    const QString secondPath = tempDir.filePath(QStringLiteral("second.mp4"));
    PlaylistManager manager;

    manager.AddFiles({firstPath, QStringLiteral("  ") + firstPath + QStringLiteral("  "), secondPath});

    QCOMPARE(manager.Count(), 2);
    QCOMPARE(manager.CurrentIndex(), 0);
    QVERIFY(manager.CurrentItem().has_value());
    QCOMPARE(manager.CurrentItem()->displayName, QStringLiteral("first.mp4"));
}

void PlaylistManagerTest::removeMaintainsCurrentItem()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    PlaylistManager manager;
    manager.AddFiles({tempDir.filePath(QStringLiteral("a.mp4")),
                      tempDir.filePath(QStringLiteral("b.mp4")),
                      tempDir.filePath(QStringLiteral("c.mp4"))});
    QVERIFY(manager.SetCurrentIndex(1));

    manager.RemoveAt(0);
    QCOMPARE(manager.CurrentIndex(), 0);
    QCOMPARE(manager.CurrentItem()->displayName, QStringLiteral("b.mp4"));

    manager.RemoveAt(0);
    QCOMPARE(manager.CurrentIndex(), 0);
    QCOMPARE(manager.CurrentItem()->displayName, QStringLiteral("c.mp4"));

    manager.RemoveAt(0);
    QCOMPARE(manager.CurrentIndex(), -1);
    QVERIFY(!manager.CurrentItem().has_value());
}

void PlaylistManagerTest::playModesRespectBoundaries()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    PlaylistManager manager;
    manager.AddFiles({tempDir.filePath(QStringLiteral("a.mp4")),
                      tempDir.filePath(QStringLiteral("b.mp4"))});

    QVERIFY(!manager.MovePrevious());
    QVERIFY(manager.MoveNext());
    QCOMPARE(manager.CurrentIndex(), 1);
    QVERIFY(!manager.MoveNext());

    manager.SetPlayMode(PlaylistManager::PlayMode::ListLoop);
    QVERIFY(manager.MoveNext());
    QCOMPARE(manager.CurrentIndex(), 0);
    QVERIFY(manager.MovePrevious());
    QCOMPARE(manager.CurrentIndex(), 1);

    manager.SetPlayMode(PlaylistManager::PlayMode::SingleLoop);
    QVERIFY(manager.MovePrevious());
    QCOMPARE(manager.CurrentIndex(), 1);

    manager.SetPlayMode(PlaylistManager::PlayMode::Random);
    QVERIFY(manager.MoveNext());
    QCOMPARE(manager.CurrentIndex(), 0);
}

void PlaylistManagerTest::mutationsEmitExpectedSignals()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    PlaylistManager manager;
    QSignalSpy playlistSpy(&manager, &PlaylistManager::PlaylistChanged);
    QSignalSpy indexSpy(&manager, &PlaylistManager::CurrentIndexChanged);
    const QString filePath = tempDir.filePath(QStringLiteral("a.mp4"));

    manager.AddFile(filePath);
    QCOMPARE(playlistSpy.count(), 1);
    QCOMPARE(indexSpy.count(), 1);

    manager.AddFile(filePath);
    QCOMPARE(playlistSpy.count(), 1);
    QCOMPARE(indexSpy.count(), 1);

    manager.Clear();
    QCOMPARE(playlistSpy.count(), 2);
    QCOMPARE(indexSpy.count(), 2);
}

QTEST_APPLESS_MAIN(PlaylistManagerTest)

#include "test_playlist_manager.moc"
