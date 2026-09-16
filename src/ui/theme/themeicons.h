#ifndef THEMEICONS_H
#define THEMEICONS_H

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;

// 把 qrc 内 Feather(currentColor 描边) 的 SVG 染成指定主题色的 QIcon。
// QtSvg 不解析 currentColor，故读字节先替换色名再渲染；颜色一般取
// ThemeManager::baseForeground()，随明暗变化时由调用方重新获取并重建图标。
// 图标源集中存于根目录 icons/（icons.qrc，运行时资源前缀 /theme/icons）。
namespace themeicon {

// name: 图标名（不含路径/后缀），如 "lock" → 资源 ":/theme/icons/lock.svg"
QIcon fromFeather(const QString &name, const QColor &color, QWidget *target = nullptr);

// 按钮图标（带禁用态）：正常态描边用 color，禁用态（QIcon::Disabled）用
// disabledColor —— QIcon 不会自动把启用态变灰，需显式补一张禁用态位图，
// 使「撤销/重做」等按钮禁用时图标与 QSS 禁用文字（%DISABLED_FG%）同灰。
QIcon buttonIcon(const QString &name, const QColor &color,
                 const QColor &disabledColor, QWidget *target = nullptr);

} // namespace themeicon

#endif // THEMEICONS_H
