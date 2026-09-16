#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

// 主题系统单例：明 / 暗两套基础色 × 若干 accent 强调色自由组合。
// 持有 scheme（跟随系统 / 手动亮 / 手动暗）与 accentId；渲染 theme.qss 模板应用到全应用。
// 偏好持久化于 QSettings（应用级），启动时恢复；跟随系统时监听系统明暗变化实时切换。
// 向课表模型提供明暗各一套卡片配色盘（cardBackgrounds / cardForegrounds）。
// 设计规格见 docs/architecture.md 第 12 节。
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    // 明暗方案：跟随系统（实际明暗由系统决定）/ 手动亮 / 手动暗
    enum Scheme { FollowSystem, Light, Dark };
    Q_ENUM(Scheme)

    static ThemeManager &instance();   // 全局单例（首次调用即恢复持久化偏好）

    Scheme scheme() const;             // 当前选择的明暗方案
    QString accentId() const;          // 当前 accent 的 id

    void setScheme(Scheme scheme);     // 切换明暗方案并持久化、重应用
    void setAccent(const QString &accentId);  // 切换 accent 并持久化、重应用

    bool effectiveDark() const;        // 实际生效是否暗色（跟随态取系统明暗）
    void apply();                      // 按当前主题渲染 QSS 并应用到全应用

    QVector<QColor> cardBackgrounds() const;  // 课表卡片底色盘（随当前明暗）
    QVector<QColor> cardForegrounds() const;  // 课表卡片文字色盘（随当前明暗）

    // 「更多 +N」折叠卡位的中性底色 / 文字色（随当前明暗；亮浅灰/暗中灰配对应前景）
    QColor moreBackground() const;
    QColor moreForeground() const;

    // 基础前景色（亮 #202020 / 暗 #E0E0E0），与 QSS 正文/按钮文字 %FG%/%BTN_FG% 同源，
    // 供需与正文同色的图标 / 装饰取色（如 themeicon::fromFeather 的描边色）
    QColor baseForeground() const;

    // accent 语义色（随当前 accent 与明暗），供 delegate / 徽标 / 状态栏等 QSS 管不到的
    // 自绘位显式取用（不要再依赖 QPalette 角色：自绘位不受 QSS 影响）
    QColor accentColor() const;   // accent 实色：主操作 / 焦点 / 选中格描边
    QColor accentTint() const;    // accent 浅衬底：列表项 / 菜单项 / 生效态背景
    QColor dangerColor() const;   // 错误语义色：失败首词 / 错误状态文字（独立于品牌 accent）
    QColor dangerTint() const;    // 错误浅衬底
    QColor secondaryText() const; // 次要文字：侧栏小节标题 / hint / 副行
    QColor disabledForeground() const; // 禁用前景色（禁用文字 %DISABLED_FG% / 禁用态图标）
    QColor surfaceColor() const;  // 面板 / 画布衬底色（明暗分层，随明暗）

    // (accentId, 显示名) 列表，供「主题」菜单按顺序生成 accent 选项
    QVector<QPair<QString, QString>> accents() const;

signals:
    void themeChanged();               // 主题变化（明暗或 accent 任一），供 UI 刷新

private:
    ThemeManager();
    void initAccents();                // 填充内置 accent 列表
    QString renderQss() const;         // 读 theme.qss 模板并替换配色占位符
    QColor currentAccent() const;      // 按 m_accentId 与 m_dark 求 accent 实色（未命中回退蓝）

    Scheme m_scheme = FollowSystem;    // 用户选择的明暗方案
    QString m_accentId = QStringLiteral("Blue");   // 用户选择的 accent id
    bool m_dark = false;               // 当前生效的实际明暗
    QVector<QPair<QString, QString>> m_accents;    // (accentId, 显示名)

private slots:
    void onSystemPaletteChanged();     // 系统明暗变化 → 跟随系统时重应用
};

#endif // THEME_H
