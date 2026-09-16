/**
 * 文件职责：主题色图标渲染助手实现。
 * Feather 图标（MIT）以 stroke="currentColor" 单色描边存于根目录 icons/icons.qrc
 * （运行时前缀 /theme/icons，文件相对 qrc 位置）。QtSvg 不会解析 currentColor
 * （会落成黑色），故这里读取资源字节、把 currentColor 替换成目标主题色后交给
 * QSvgRenderer 渲染，按宿主控件的 devicePixelRatio 出图，保证高分屏清晰。
 * 颜色通常取 ThemeManager::baseForeground()（正常态）与 disabledForeground()
 * （禁用态），随明暗切换时由调用方重取色值再重建图标。
 */

#include "themeicons.h"

#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QWidget>

namespace {

/*
renderFeatherPixmap - 渲染单个主题色 Feather QPixmap（内部共用）

Parameter：
    name: 图标名（不含路径/后缀），如 "lock" → ":/theme/icons/lock.svg"
    color: 描边色（主题前景色；正常态或禁用态）
    target: 目标控件（取 devicePixelRatio；可为空，默认 1）

Result:
    QPixmap: 渲染好的位图（逻辑尺寸 24、随 dpr 放大）；资源缺失 / 解析失败返回空
*/
QPixmap renderFeatherPixmap(const QString &name, const QColor &color, QWidget *target)
{
    const QString path = QStringLiteral(":/theme/icons/%1.svg").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QPixmap();

    // Feather 用 currentColor 占位描边色：替换成目标色后给 QSvgRenderer 解析
    QString svg = QString::fromUtf8(f.readAll());
    svg.replace(QStringLiteral("currentColor"), color.name());
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid())
        return QPixmap();

    // 按控件缩放比渲染：逻辑尺寸 24（Feather 原始 24×24 网格），像素随 dpr 放大，
    // 设回 devicePixelRatio 后由 QIcon 在小尺寸/高分屏下保持清晰
    const qreal dpr = target ? target->devicePixelRatioF() : 1.0;
    const int logic = 24;
    QPixmap pm(qRound(logic * dpr), qRound(logic * dpr));
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    renderer.render(&p, QRectF(0, 0, logic, logic));
    p.end();
    return pm;
}

} // namespace

/*
themeicon::fromFeather - 由 qrc 内 Feather SVG 染主题色生成 QIcon

Parameter：
    name: 图标名（不含路径/后缀），如 "lock" → ":/theme/icons/lock.svg"
    color: 描边色（主题前景色）
    target: 目标控件（取 devicePixelRatio；可为空，默认 1）

Result:
    QIcon: 渲染好的图标；资源缺失/解析失败返回空 QIcon
*/
QIcon themeicon::fromFeather(const QString &name, const QColor &color, QWidget *target)
{
    const QPixmap pm = renderFeatherPixmap(name, color, target);
    return pm.isNull() ? QIcon() : QIcon(pm);
}

/*
themeicon::buttonIcon - 生成带正常/禁用两态的按钮图标

Parameter：
    name: 图标名（不含路径/后缀），如 "save" → ":/theme/icons/save.svg"
    color: 正常态描边色（主题前景色）
    disabledColor: 禁用态描边色（ThemeManager::disabledForeground()，随明暗）
    target: 目标控件（取 devicePixelRatio；可为空，默认 1）

Result:
    QIcon: 含 QIcon::Normal（color）与 QIcon::Disabled（disabledColor）两态位图；
           资源缺失/解析失败返回空 QIcon

Remark:
    QIcon 不会在控件禁用时自动把启用位图转灰，只会换到 Disabled 态的位图；
    故这里对同一 SVG 渲两张不同描边色的位图分别登记两态。按钮禁用由
    QStyle 按控件 enabled 自动选 Disabled 态，无需调用方在开关时重建图标。
*/
QIcon themeicon::buttonIcon(const QString &name, const QColor &color,
                            const QColor &disabledColor, QWidget *target)
{
    const QPixmap normal = renderFeatherPixmap(name, color, target);
    if (normal.isNull())
        return QIcon();
    QIcon icon;
    icon.addPixmap(normal, QIcon::Normal);
    const QPixmap disabled = renderFeatherPixmap(name, disabledColor, target);
    if (!disabled.isNull())
        icon.addPixmap(disabled, QIcon::Disabled);
    return icon;
}
