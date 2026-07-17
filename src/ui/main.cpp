#include "ui/mainwindow.h"
#include "ui/theme/UiTheme.h"
#include "tiny_log/Logging.h"

#include <QApplication>

/**
 * @brief main 程序入口
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return Qt 应用退出码
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    UiTheme::Apply(a);
    logging::SetGlobalLevel(logging::Level::Trace);
    logging::GetDefaultLogger()->SetLevel(logging::Level::Trace);
    LOG_INFO("TinyVideoProcessor started");
    MainWindow w;
    w.show();
    const int exitCode = a.exec();
    LOG_INFO("TinyVideoProcessor exited");
    logging::FlushAll();

    return exitCode;
}
