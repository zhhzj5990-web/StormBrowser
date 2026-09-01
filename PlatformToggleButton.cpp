#include "PlatformToggleButton.h"

PlatformToggleButton::PlatformToggleButton(SocialPlatform platform, QWidget* parent)
    : QPushButton(parent), m_platform(platform) {
    const PlatformInfo& info = platformRegistry().value(platform);

    setText(info.icon);
    setToolTip(info.displayName);
    setCheckable(true);
    setFixedSize(36, 32);
    setCursor(Qt::PointingHandCursor);

    QFont f = font();
    f.setPointSize(14);
    setFont(f);

    connect(this, &QPushButton::toggled, this, &PlatformToggleButton::applyStyle);
    applyStyle(false);
}

void PlatformToggleButton::applyStyle(bool checked) {
    const PlatformInfo& info = platformRegistry().value(m_platform);

    if (checked) {
        setStyleSheet(QString(
            "QPushButton { background-color: %1; border: 1px solid %1; border-radius: 8px; color: white; }"
        ).arg(info.accentColor));
    } else {
        setStyleSheet(
            "QPushButton { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.12); border-radius: 8px; color: #cfcfcf; }"
            "QPushButton:hover { background: rgba(255,255,255,0.12); }"
        );
    }
}
