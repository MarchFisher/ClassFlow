/**
 * 文件职责：主题系统实现（ThemeManager 单例）。
 * 明 / 暗两套基础色 × 若干 accent 强调色自由组合；
 * 渲染 theme.qss 模板（占位符替换）后经 qApp->setStyleSheet 应用到全应用。
 * 偏好持久化于 QSettings；跟随系统模式下监听系统明暗变化实时切换。
 * 向课表模型提供明暗各一套卡片配色盘（cardBackgrounds / cardForegrounds）。
 */

#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace {
// 基础色盘：由实际明暗（m_dark）决定，变量名对应 theme.qss 占位符
struct BasePalette {
    const char *bg;         // 窗口背景 %BG%
    const char *fg;         // 正文前景 %FG%
    const char *border;     // 边框 %BORDER%
    const char *inputBg;    // 输入框 / 列表 / 表格背景 %INPUT_BG%
    const char *inputFg;    // 输入文字 %INPUT_FG%
    const char *altBg;      // 表格交替行 %ALT_BG%
    const char *headBg;     // 表头 / 菜单栏 / 状态栏 %HEAD_BG%
    const char *headFg;     // 表头文字 %HEAD_FG%
    const char *btnBg;      // 按钮 %BTN_BG%
    const char *btnHover;   // 按钮悬停 %BTN_HOVER%
    const char *btnPressed; // 按钮按下 %BTN_PRESSED%
    const char *btnFg;      // 按钮文字 %BTN_FG%
    const char *tooltipBg;  // 工具提示 %TOOLTIP_BG%
    const char *tooltipFg;  // 工具提示文字 %TOOLTIP_FG%
    const char *scrollBg;   // 滚动条滑道 %SCROLL_BG%
    const char *scrollHd;   // 滚动条滑块 %SCROLL_HANDLE%
    const char *disabledBg; // 禁用底色 %DISABLED_BG%
    const char *disabledFg; // 禁用文字 %DISABLED_FG%
    const char *surface;         // 面板/画布衬底 %SURFACE%（比窗口底更"浮"）
    const char *surfaceBorder;   // 画布/面板描边 %SURFACE_BORDER%
    const char *secondaryText;   // 次要说明文字 %TEXT_SECONDARY%
    const char *danger;          // 错误语义前景 %DANGER%（独立于品牌 accent）
    const char *dangerTint;      // 错误浅衬底 %DANGER_TINT%
};

// 明暗两套均为有表面分层的配色（不是简单"换灰"）——
// 亮：整体走"中性浅灰纸感"，窗底、表头、画布逐层下沉一档（画布非纯白、更暗更柔和，
//     左上角表头交角与表头同底）；暗：窗底加深、画布略亮 + 高光描边勾勒
const BasePalette kLight = {
    // bg        fg       border    inputBg   inputFg   altBg
    "#DEE1E5", "#24272C", "#C6CBD1", "#F6F7F9", "#24272C", "#E9ECEF",
    // headBg    headFg   btnBg    btnHover  btnPressed btnFg
    "#E3E6EA", "#3E454D", "#F6F7F9", "#EDEFF2", "#E0E3E7", "#2A2E34",
    // tooltipBg tooltipFg scrollBg scrollHd disabledBg disabledFg
    "#FBFCFD", "#24272C", "#E4E7EA", "#ABB2BA", "#E9EBEE", "#99A0A7",
    // surface   surfaceBorder secondaryText danger    dangerTint
    "#EBEDF0", "#D5D9DE", "#6B727A", "#BE3A32", "#F5E8E6"
};

const BasePalette kDark = {
    "#1E1F22", "#D7DAE0", "#3B3E43", "#2C2F34", "#E0E3E8", "#232529",
    "#26282D", "#B6BAC0", "#2C2F34", "#34383E", "#3C4147", "#E0E3E8",
    "#33363B", "#E0E3E8", "#202226", "#4A4F56", "#282A2E", "#6B7077",
    "#2A2A2E", "#3A3D42", "#9CA1A9", "#EE6B6B", "#382B2D"
};

// accent 色：light = 亮色模式用（较深，配白字）；dark = 暗色模式用（较亮，配深字）
struct AccentColor {
    const char *id;
    const char *light;
    const char *dark;
};

const AccentColor kAccents[] = {
    {"Blue",   "#1976D2", "#64B5F6"},
    {"Green",  "#2E7D32", "#81C784"},
    {"Purple", "#7B1FA2", "#CE93D8"},
    {"Orange", "#E65100", "#FFB74D"},
    {"Teal",   "#00796B", "#4DB6AC"}
};
const char *kAccentLabels[] = {"蓝", "绿", "紫", "橙", "青"};
const int kAccentCount = 5;

// 课表卡片配色盘：底色（明 / 暗各一套）+ 文字色（同课同色轮换取用）
const char *kCardBgLight[] = {
    "#FFE0B2", "#C8E6C9", "#B3E5FC", "#CE93D8", "#FFCCBC",
    "#F0F4C3", "#FFF9C4", "#FFAB91", "#E1BEE7", "#B2DFDB"
};
const char *kCardBgDark[] = {
    "#6D4C41", "#2E7D32", "#0277BD", "#7B1FA2", "#BF360C",
    "#33691E", "#8D6E63", "#00838F", "#4A148C", "#4E342E"
};
const char *kCardFgLight = "#1A1A1A";
const char *kCardFgDark = "#ECEFF1";
const int kCardCount = 10;

// 「更多 +N」折叠卡位的中性底色：亮 = 浅灰（区别于柔白网格与淡彩课程卡），
// 暗 = 中灰（区别于深网格与深色课程卡）；文字沿用卡片前景色保证可读
const char *kMoreBgLight = "#D7DADD";
const char *kMoreBgDark = "#606060";

// mix - 按 t∈[0,1] 向 b 混色（t=0 全取 a，t=1 全取 b）；用于生成 accent 浅衬底
QColor mix(const QColor &a, const QColor &b, double t)
{
    auto lerp = [t](int x, int y) {
        return int(x + (y - x) * t + 0.5);
    };
    return QColor(lerp(a.red(), b.red()), lerp(a.green(), b.green()),
                  lerp(a.blue(), b.blue()));
}

// accentTintOf - accent 的浅衬底：亮色混白 ≈15% accent，暗色向窗底混 45% accent
QColor accentTintOf(const QColor &accent, bool dark)
{
    return mix(accent, QColor(dark ? QStringLiteral("#1E1F22")
                                   : QStringLiteral("#FFFFFF")),
               dark ? 0.55 : 0.85);
}
}

/*
ThemeManager::ThemeManager - 构造函数：恢复持久化偏好并监听系统明暗变化

Remark:
    偏好键：theme/scheme（int）、theme/accent（QString），默认跟随系统 + 蓝。
*/
ThemeManager::ThemeManager()
{
    initAccents();

    QSettings s;
    m_scheme = static_cast<Scheme>(
        s.value(QStringLiteral("theme/scheme"), static_cast<int>(FollowSystem)).toInt());
    m_accentId = s.value(QStringLiteral("theme/accent"), QStringLiteral("Blue")).toString();

    // 跟随系统时，系统明暗变化即重应用：Qt 6.8+ 用 QStyleHints::colorSchemeChanged，
    // 更早版本退回 QApplication::paletteChanged
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) { onSystemPaletteChanged(); });
#else
    connect(qApp, &QGuiApplication::paletteChanged,
            this, &ThemeManager::onSystemPaletteChanged);
#endif
}

/*
ThemeManager::instance - 全局单例

Result:
    ThemeManager&: 唯一实例（首次调用即触发构造函数恢复偏好）
*/
ThemeManager &ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

/*
ThemeManager::scheme - 取当前选择的明暗方案

Result:
    Scheme: FollowSystem / Light / Dark
*/
ThemeManager::Scheme ThemeManager::scheme() const
{
    return m_scheme;
}

/*
ThemeManager::accentId - 取当前 accent 的 id

Result:
    QString: 如 "Blue" / "Green"
*/
QString ThemeManager::accentId() const
{
    return m_accentId;
}

/*
ThemeManager::effectiveDark - 实际生效是否为暗色

Result:
    bool: 手动态直接返回；跟随系统时取系统明暗（Qt 6.5+，Qt 5 恒为亮色）
*/
bool ThemeManager::effectiveDark() const
{
    if (m_scheme == Dark)
        return true;
    if (m_scheme == Light)
        return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

/*
ThemeManager::setScheme - 切换明暗方案并持久化、重应用、广播

Parameter：
    scheme: 新的明暗方案
*/
void ThemeManager::setScheme(Scheme scheme)
{
    if (m_scheme == scheme)
        return;
    m_scheme = scheme;
    QSettings().setValue(QStringLiteral("theme/scheme"), static_cast<int>(scheme));
    apply();
    emit themeChanged();
}

/*
ThemeManager::setAccent - 切换 accent 并持久化、重应用、广播

Parameter：
    accentId: 新的 accent id
*/
void ThemeManager::setAccent(const QString &accentId)
{
    if (m_accentId == accentId)
        return;
    m_accentId = accentId;
    QSettings().setValue(QStringLiteral("theme/accent"), accentId);
    apply();
    emit themeChanged();
}

/*
ThemeManager::apply - 按当前主题渲染 QSS 并应用到全应用

Remark:
    更新实际明暗缓存 m_dark；跟随系统模式切换由 onSystemPaletteChanged 触发。
*/
void ThemeManager::apply()
{
    m_dark = effectiveDark();
    qApp->setStyleSheet(renderQss());
}

/*
ThemeManager::accents - 内置 accent 列表

Result:
    QVector<QPair<QString,QString>>: (id, 显示名)，供「主题」菜单按顺序生成
*/
QVector<QPair<QString, QString>> ThemeManager::accents() const
{
    return m_accents;
}

/*
ThemeManager::cardBackgrounds - 课表卡片底色盘

Result:
    QVector<QColor>: 随当前实际明暗返回对应的 10 色盘
*/
QVector<QColor> ThemeManager::cardBackgrounds() const
{
    QVector<QColor> v;
    v.reserve(kCardCount);
    const char *const *src = m_dark ? kCardBgDark : kCardBgLight;
    for (int i = 0; i < kCardCount; ++i)
        v.append(QColor(QString::fromLatin1(src[i])));
    return v;
}

/*
ThemeManager::cardForegrounds - 课表卡片文字色盘

Result:
    QVector<QColor>: 对应底色盘的统一文字色（10 项）
*/
QVector<QColor> ThemeManager::cardForegrounds() const
{
    const QString fg = QString::fromLatin1(m_dark ? kCardFgDark : kCardFgLight);
    return QVector<QColor>(kCardCount, QColor(fg));
}

/*
ThemeManager::moreBackground - 「更多」折叠卡位的中性底色
（随当前明暗；亮浅灰 / 暗中灰）
*/
QColor ThemeManager::moreBackground() const
{
    return QColor(QString::fromLatin1(m_dark ? kMoreBgDark : kMoreBgLight));
}

/*
ThemeManager::moreForeground - 「更多」折叠卡位的文字色
（随当前明暗；沿用卡片前景色，保证与中性底色对比可读）
*/
QColor ThemeManager::moreForeground() const
{
    return QColor(QString::fromLatin1(m_dark ? kCardFgDark : kCardFgLight));
}

/*
ThemeManager::baseForeground - 基础前景色（随当前明暗）
（亮 #202020 / 暗 #E0E0E0，与 QSS %FG%/%BTN_FG% 同源，供图标等取正文同色）
*/
QColor ThemeManager::baseForeground() const
{
    const BasePalette &base = m_dark ? kDark : kLight;
    return QColor(QString::fromLatin1(base.fg));
}

/*
ThemeManager::currentAccent - 按当前 accentId 与实际明暗求 accent 实色
（renderQss 与各语义访问器的公共来源；未命中回退蓝）
*/
QColor ThemeManager::currentAccent() const
{
    for (const AccentColor &a : kAccents) {
        if (m_accentId == QLatin1String(a.id))
            return QColor(QString::fromLatin1(m_dark ? a.dark : a.light));
    }
    return QColor(QStringLiteral("#1976D2"));
}

/*
ThemeManager::accentColor - accent 实色（主操作 / 焦点 / 选中格描边）
*/
QColor ThemeManager::accentColor() const
{
    return currentAccent();
}

/*
ThemeManager::accentTint - accent 浅衬底（列表项 / 菜单项 / 生效态背景）
（与 renderQss 的 %ACCENT_TINT% 同源，亮混白、暗向窗底混）
*/
QColor ThemeManager::accentTint() const
{
    return accentTintOf(currentAccent(), m_dark);
}

/*
ThemeManager::dangerColor - 错误语义前景色（失败首词 / 错误状态，独立于品牌 accent）
*/
QColor ThemeManager::dangerColor() const
{
    const BasePalette &base = m_dark ? kDark : kLight;
    return QColor(QString::fromLatin1(base.danger));
}

/*
ThemeManager::dangerTint - 错误浅衬底
*/
QColor ThemeManager::dangerTint() const
{
    const BasePalette &base = m_dark ? kDark : kLight;
    return QColor(QString::fromLatin1(base.dangerTint));
}

/*
ThemeManager::secondaryText - 次要文字色（侧栏小节标题 / hint / 副行）
*/
QColor ThemeManager::secondaryText() const
{
    const BasePalette &base = m_dark ? kDark : kLight;
    return QColor(QString::fromLatin1(base.secondaryText));
}

/*
ThemeManager::disabledForeground - 禁用前景色（随当前明暗）
（亮 #99A0A7 / 暗 #6B7077，与 QSS 禁用文字 %DISABLED_FG% 同源，
供禁用态图标取色，保证按钮禁用时图标与文字同灰一致）
*/
QColor ThemeManager::disabledForeground() const
{
    const BasePalette &base = m_dark ? kDark : kLight;
    return QColor(QString::fromLatin1(base.disabledFg));
}

/*
ThemeManager::surfaceColor - 面板/画布衬底色（比窗口底更"浮"，随明暗）
*/
QColor ThemeManager::surfaceColor() const
{
    const BasePalette &base = m_dark ? kDark : kLight;
    return QColor(QString::fromLatin1(base.surface));
}

/*
ThemeManager::initAccents - 填充内置 accent 列表
*/
void ThemeManager::initAccents()
{
    m_accents.clear();
    for (int i = 0; i < kAccentCount; ++i)
        m_accents.append(qMakePair(QString::fromLatin1(kAccents[i].id),
                                   QString::fromUtf8(kAccentLabels[i])));
}

/*
ThemeManager::renderQss - 读取 theme.qss 模板并替换配色占位符

Result:
    QString: 最终 QSS；模板读取失败返回空串

Remark:
    强调色按当前明暗取 accent 对应值；主按钮 / 选中前景在暗色用深字、亮色用白字。
*/
QString ThemeManager::renderQss() const
{
    const BasePalette &base = m_dark ? kDark : kLight;

    const QColor accentColor = currentAccent();   // 当前 accent 实色（含未命中回退蓝）
    const QString accent = accentColor.name();
    // accent 实底上的文字：暗色配深字（亮 accent），亮色配白字（深 accent）
    const QString accentFg = m_dark ? QStringLiteral("#1A1A1A")
                                    : QStringLiteral("#FFFFFF");
    // 主按钮悬停（小幅提亮）；按下：亮色压暗、暗色提亮
    const QString primaryHover = accentColor.lighter(m_dark ? 115 : 108).name();
    const QString accentPressed =
        (m_dark ? accentColor.lighter(112) : accentColor.darker(112)).name();
    const QString accentTint = accentTintOf(accentColor, m_dark).name();

    QFile f(QStringLiteral(":/theme/theme.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QString qss = QString::fromUtf8(f.readAll());

    qss.replace("%BG%", base.bg);
    qss.replace("%FG%", base.fg);
    qss.replace("%BORDER%", base.border);
    qss.replace("%INPUT_BG%", base.inputBg);
    qss.replace("%INPUT_FG%", base.inputFg);
    qss.replace("%ALT_BG%", base.altBg);
    qss.replace("%HEAD_BG%", base.headBg);
    qss.replace("%HEAD_FG%", base.headFg);
    qss.replace("%BTN_BG%", base.btnBg);
    qss.replace("%BTN_HOVER%", base.btnHover);
    qss.replace("%BTN_PRESSED%", base.btnPressed);
    qss.replace("%BTN_FG%", base.btnFg);
    qss.replace("%TOOLTIP_BG%", base.tooltipBg);
    qss.replace("%TOOLTIP_FG%", base.tooltipFg);
    qss.replace("%SCROLL_BG%", base.scrollBg);
    qss.replace("%SCROLL_HANDLE%", base.scrollHd);
    qss.replace("%DISABLED_BG%", base.disabledBg);
    qss.replace("%DISABLED_FG%", base.disabledFg);
    qss.replace("%PRIMARY_BG%", accent);              // 主操作实底
    qss.replace("%PRIMARY_HOVER%", primaryHover);
    qss.replace("%PRIMARY_FG%", accentFg);
    qss.replace("%SELECT_BG%", accentTint);            // 列表/菜单/表格选中改浅衬
    qss.replace("%SELECT_FG%", base.fg);               // 浅衬上配正文色文字
    // accent 语义 token：自绘位与 QSS 同源，保证换 accent 时全应用一致
    qss.replace("%ACCENT%", accent);
    qss.replace("%ACCENT_TINT%", accentTint);
    qss.replace("%ACCENT_PRESSED%", accentPressed);
    qss.replace("%ACCENT_FG%", accentFg);
    qss.replace("%SURFACE%", base.surface);
    qss.replace("%SURFACE_BORDER%", base.surfaceBorder);
    qss.replace("%TEXT_SECONDARY%", base.secondaryText);
    qss.replace("%DANGER%", base.danger);
    qss.replace("%DANGER_TINT%", base.dangerTint);
    return qss;
}

/*
ThemeManager::onSystemPaletteChanged - 系统明暗变化时的实时跟随

Remark:
    仅跟随系统模式响应；实际明暗变化才重应用并广播。
*/
void ThemeManager::onSystemPaletteChanged()
{
    if (m_scheme != FollowSystem)
        return;
    const bool dark = effectiveDark();
    if (dark == m_dark)
        return;
    apply();
    emit themeChanged();
}
