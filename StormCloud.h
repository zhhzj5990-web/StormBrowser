#pragma once
#pragma once

#include <QDialog>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QPropertyAnimation>
#include <QSettings>

class StormCloud : public QDialog {
    Q_OBJECT

public:
    explicit StormCloud(QWidget* parent = nullptr);
    ~StormCloud();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void toggleMode();
    void handleAuth();
    void onAuthReply(QNetworkReply* reply);
    void checkServerOnline();
    void onPingReply(QNetworkReply* reply);
    void requestBetaAccess();
    void logout();
    void animateTransition(QWidget* nextWidget);
    // Запрос данных профиля
    void fetchBillingInfo();
    void onBillingInfoReply(QNetworkReply* reply);

    // Диалоги и окна
    void showBetaInfo();
    void showWalletDialog();
    void showStoreDialog();
    void handleVpnClick();
    void processPurchase(const QString& packageId, QDialog* parentDlg);

private:
    void setupUi();
    QWidget* createLoginScreen();
    QWidget* createManageScreen();
    void updateProfileUi();

    // Сеть
    QNetworkAccessManager* m_networkManager;
    QString m_serverUrl;

    // UI элементы
    QFrame* m_container;
    QStackedWidget* m_stack;
    QWidget* m_loginWidget;
    QWidget* m_manageWidget;

    // Логин
    QLineEdit* m_usernameInput;
    QLineEdit* m_passwordInput;
    QLineEdit* m_inviteInput;
    QPushButton* m_btnAuth;
    QPushButton* m_modeToggle;

    // ЛК: Шапка
    QLabel* m_userNameLabel;
    QLabel* m_statusDot;
    QLabel* m_statusTextLabel;
    QLabel* m_lastSyncLabel;
    QPushButton* m_avatarBtn;
    QPushButton* m_settingsBtn;

    // ЛК: Быстрая статистика (Балансы)
    QLabel* m_lblHeaderSc;
    QLabel* m_lblHeaderAi;
    QLabel* m_lblHeaderPrem;

    // ЛК: Левая колонка
    QFrame* m_histCard;
    QFrame* m_passCard;
    QPushButton* m_btnOpenStore;
    QPushButton* m_btnWalletInfo;
    QLabel* m_refHeader;
    QLabel* m_refCodeLbl;

    // ЛК: VPN
    QLabel* m_vpnStatusLbl;
    QLabel* m_vpnTrafficLbl;
    QProgressBar* m_vpnProgress;
    QPushButton* m_btnBuyVpn;
    QLabel* m_vpnFraudNote;

    // ЛК: Правая колонка
    QPushButton* m_btnVk;
    QPushButton* m_betaBtn;
    QPushButton* m_infoBtn;
    QPushButton* m_autoSyncBtn;
    QProgressBar* m_syncProgress;
    QPushButton* m_btnSync;

    // Вспомогательный метод для карточек статистики
    QFrame* createStatCard(const QString& title, const QString& value);

    // Состояние
    QSettings m_settings;
    QString m_currentUser;
    QString m_currentPassword;
    QPoint m_oldPos;
    QTimer* m_pingTimer;
    bool m_isPremium;
};