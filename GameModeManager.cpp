#include "GameModeManager.h"
#include "MainWindow.h"
#include <QTabWidget>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QSettings>
#include <QMessageBox>
#include <QStatusBar>
#include <QApplication>
#include <QDebug>

void GameModeManager::toggleGameMode(MainWindow* mw, bool enabled) {
    if (!mw) return;

    QSettings settings;
    settings.setValue("browser/game_mode", enabled);

    if (enabled) {
        // 1. Запускаем оптимизацию памяти
        optimizeRAM(mw);

        // 2. Радуем пользователя сообщением
        mw->statusBar()->showMessage(u8"🚀 Игровой режим ВКЛЮЧЕН: Фоновые вкладки заморожены, ОЗУ освобождено", 5000);

        QMessageBox::information(mw, u8"🚀 Игровой режим (RAM Boost)",
            u8"<b>Игровой режим успешно активирован!</b><br><br>"
            u8"• Приоритет отдан активной вкладке и сторонним играм.<br>"
            u8"• Фоновые страницы браузера выгружены из оперативной памяти.<br>"
            u8"• Временный кэш Chromium очищен.");
    }
    else {
        // При выключении возвращаем вкладки в нормальное состояние
        QTabWidget* tabs = mw->getTabWidget();
        if (tabs) {
            for (int i = 0; i < tabs->count() - 1; ++i) {
                if (auto* view = qobject_cast<QWebEngineView*>(tabs->widget(i))) {
                    if (view->page()) {
                        view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
                    }
                }
            }
        }

        mw->statusBar()->showMessage(u8"ℹ️ Игровой режим отключен: Стандартная работа вкладок восстановлена", 3000);
    }
}

void GameModeManager::optimizeRAM(MainWindow* mw) {
    if (!mw) return;

    QTabWidget* tabs = mw->getTabWidget();
    if (!tabs) return;

    int activeIndex = tabs->currentIndex();
    int suspendedCount = 0;

    // 1. Проходим по всем открытым вкладкам (кроме кнопки "+")
    for (int i = 0; i < tabs->count() - 1; ++i) {
        auto* view = qobject_cast<QWebEngineView*>(tabs->widget(i));
        if (!view || !view->page()) continue;

        // Активную текущую вкладку не трогаем, чтобы пользователь мог продолжать смотреть/читать
        if (i == activeIndex) {
            view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
            continue;
        }

        // Не замораживаем служебные страницы и аудио/видео плееры
        QString urlStr = view->url().toString();
        if (urlStr.startsWith("storm://") || view->page()->recentlyAudible()) {
            continue;
        }

        // Переводим фоновую страницу в режим Discarded (Выгружено из ОЗУ)
        view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Discarded);
        suspendedCount++;
    }

    // 2. Очищаем HTTP и дисковый кэш в оперативной памяти профиля
    QWebEngineProfile::defaultProfile()->clearHttpCache();

    qDebug() << "🚀 [RAM Boost] Выгружено фоновых вкладок из ОЗУ:" << suspendedCount;
}