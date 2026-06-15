#include "ui/mainwindow.h"

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
    MainWindow w;
    w.show();
    return a.exec();
}
