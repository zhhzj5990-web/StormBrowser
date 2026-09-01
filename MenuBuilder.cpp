#include "MenuBuilder.h"
#include "MainWindow.h"
#include "CustomMenuPanel.h"
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QObject>

namespace {

    void populateMenu(CustomMenuPanel* root, MainWindow* mw) {

        // ==========================================
        // 1. БЫСТРЫЕ ДЕЙСТВИЯ С ВКЛАДКАМИ
        // ==========================================
        root->addAction(u8"➕ Новая вкладка", [mw]() {
            mw->addNewTab(QUrl("storm://newtab"));
            });
        root->addAction(u8"🕶 Новая вкладка инкогнито", [mw]() {
            mw->openIncognitoTab();
            });
        root->addAction(u8"❌ Закрыть вкладку", [mw]() {
            mw->closeTab(mw->getTabWidget()->currentIndex());
            });

        root->addSeparator();

        // ==========================================
        // 2. БИБЛИОТЕКА
        // ==========================================
        // "Закладки" открываются вкладкой — так же, как обычные сайты
        // (storm://bookmarks, см. MainWindow::addNewTab()), а не отдельным
        // окном и не боковым флайаут-подменю, как было раньше.
        root->addAction(u8"⭐ Закладки", [mw]() { mw->openBookmarksTab(); });

        CustomMenuPanel* historyMenu = root->addSubmenu(u8"🕒 История");
        historyMenu->addAction(u8"🕒 Показать историю", [mw]() { mw->showHistory(); });
        historyMenu->addAction(u8"🧹 Очистить историю", [mw]() { mw->clearHistory(); });

        root->addSeparator();
        root->addAction(u8"📥 Загрузки", [mw]() { mw->openDownloads(); });

        root->addSeparator();


        // "Боковая панель" — отдельная кнопка сразу под "Вид"
        // Важно: панель меню пересоздаётся с нуля при каждом клике по
        // гамбургер-кнопке (см. MenuBuilder::buildMenu), поэтому начальное
        // состояние галочки нужно брать из реальной видимости sidebar,
        // а не хардкодить — иначе галочка и панель расходятся местами.
        root->addCheckable(u8"📊 Боковая панель", mw->isSidebarVisible(), [mw](bool) {
            mw->toggleSidebar();
            }, false); // false = не закрывать меню при переключении

        root->addSeparator();

        // ==========================================
        // 4. НАСТРОЙКИ — открывает вкладку (storm://settings)
        // ==========================================
        root->addAction(u8"⚙️ Настройки", [mw]() {
            mw->openSettingsTab();
            });

        // ==========================================
        // 5. ВЫХОД — всегда последним
        // ==========================================
        root->addSeparator();
        root->addAction(u8"🚪 Выход", [mw]() { mw->close(); });
    }

} // namespace

void MenuBuilder::buildMenu(MainWindow* mw) {
    QPushButton* menuBtn = mw->findChild<QPushButton*>("hamburgerMenuBtn");
    if (!menuBtn) return;

    if (!mw->findChild<QMenu*>("bookmarksMenu")) {
        QMenu* bm = new QMenu(mw);
        bm->setObjectName("bookmarksMenu");
    }

    QObject::connect(menuBtn, &QPushButton::clicked, mw, [mw, menuBtn]() {
        CustomMenuPanel* root = new CustomMenuPanel();
        populateMenu(root, mw);
        root->popup(menuBtn);
        });
}