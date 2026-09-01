#pragma once
#include <QPushButton>
#include "SmmTypes.h"

// Компактная кнопка-переключатель одной платформы: иконка + подсветка
// акцентным цветом площадки, когда выбрана. Отдельный класс, а не голый
// QPushButton с setCheckable(true), чтобы инкапсулировать стилизацию
// (см. applyStyle()) и не размазывать QSS по SmmAutoPublisherWidget.
class PlatformToggleButton : public QPushButton {
    Q_OBJECT
public:
    explicit PlatformToggleButton(SocialPlatform platform, QWidget* parent = nullptr);
    SocialPlatform platform() const { return m_platform; }

private slots:
    void applyStyle(bool checked);

private:
    SocialPlatform m_platform;
};
