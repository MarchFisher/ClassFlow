/**
 * 文件职责：程序入口。创建 QApplication，按系统语言加载翻译，显示主窗口。
 * 启动时先恢复并应用主题（ThemeManager），再创建主窗口避免首帧闪白。
 */

#include "core/schedule/scheduler.h"
#include "core/store/datastore.h"
#include "ui/mainwindow.h"
#include "ui/theme/theme.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>

/*
main - 程序入口，加载翻译并启动主窗口

Parameter：
    argc: 命令行参数个数
    argv: 命令行参数数组

Result:
    int: QApplication::exec() 事件循环的返回码

Remark:
    按系统 UI 语言依次尝试加载翻译文件 ClassFlow_*.qm；
    主题偏好经 QSettings 恢复（依赖组织名 / 应用名）。
*/
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ClassFlow"));
    QCoreApplication::setApplicationName(QStringLiteral("ClassFlow"));

    // 窗口图标：加载 qrc 内 app.png（icons.qrc 中 classflow.png 的别名），
    // 作为标题栏 / 任务栏 / 所有对话框的默认图标
    a.setWindowIcon(QIcon(QStringLiteral(":/theme/icons/app.png")));

    // 跨线程 queued 信号回传自定义类型前先注册元类型
    qRegisterMetaType<DataStore>("DataStore");
    qRegisterMetaType<ScheduleResult>("ScheduleResult");

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "ClassFlow_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    // 先恢复持久化主题并应用，再创建主窗口（避免默认亮色首帧）
    ThemeManager::instance().apply();

    MainWindow w;
    w.show();
    return a.exec();
}
