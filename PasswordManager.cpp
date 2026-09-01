#include "PasswordManager.h"
#include "FramelessDialog.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QTextStream>
#include <QUrl>
#include <QUuid>
#include <QDateTime>
#include <functional>

PasswordManager::PasswordManager(QObject* parent) : QObject(parent) {
    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(appDataDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    vaultPath = dir.filePath("vault.json");
    loadMasterHash();
}

QString PasswordManager::hashPassword(const QString& password) {
    return QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
}

// Простейшее обратимое шифрование (XOR + Base64) для хранения в JSON
QString PasswordManager::encryptData(const QString& data, const QString& key) {
    QByteArray dataBytes = data.toUtf8();
    QByteArray keyBytes = key.toUtf8();
    if (keyBytes.isEmpty()) return data;
    QByteArray result;
    for (int i = 0; i < dataBytes.size(); ++i) {
        result.append(dataBytes[i] ^ keyBytes[i % keyBytes.size()]);
    }
    return QString(result.toBase64());
}

QString PasswordManager::decryptData(const QString& data, const QString& key) {
    QByteArray dataBytes = QByteArray::fromBase64(data.toUtf8());
    QByteArray keyBytes = key.toUtf8();
    QByteArray result;
    for (int i = 0; i < dataBytes.size(); ++i) {
        result.append(dataBytes[i] ^ keyBytes[i % keyBytes.size()]);
    }
    return QString(result);
}

void PasswordManager::loadMasterHash() {
    QFile file(vaultPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        masterHash = doc.object().value("master_hash").toString();
        file.close();
    }
}

QJsonObject PasswordManager::loadVault() {
    QFile file(vaultPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject vault = doc.object();

    // МИГРАЦИЯ: старый формат хранил passwords как ОБЪЕКТ с доменом в качестве
    // ключа — из-за этого второй сохранённый пароль для того же домена молча
    // затирал первый (два аккаунта на одном сайте = потеря одного из них).
    // Конвертируем в массив записей с уникальным id и сразу пересохраняем,
    // чтобы миграция выполнилась один раз, а не при каждой загрузке.
    if (vault.contains("passwords") && vault.value("passwords").isObject()) {
        QJsonObject oldPasswords = vault.value("passwords").toObject();
        QJsonArray migrated;
        for (auto it = oldPasswords.begin(); it != oldPasswords.end(); ++it) {
            QJsonObject oldEntry = it.value().toObject();
            QJsonObject newEntry;
            newEntry["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            newEntry["domain"] = it.key();
            newEntry["login"] = oldEntry.value("login").toString();
            newEntry["password"] = oldEntry.value("password").toString();
            migrated.append(newEntry);
        }
        vault["passwords"] = migrated;

        QFile outFile(vaultPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(QJsonDocument(vault).toJson());
            outFile.close();
        }
    }

    return vault;
}

void PasswordManager::saveVault(const QJsonObject& vault) {
    QFile file(vaultPath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(vault);
        file.write(doc.toJson());
        file.close();
    }
}

bool PasswordManager::promptNewMasterPassword(QWidget* parentWidget) {
    bool ok;
    QString pwd = QInputDialog::getText(parentWidget, u8"Создание мастер-пароля",
        u8"Придумайте сложный мастер-пароль для защиты базы:\n(Вы не сможете восстановить его, если забудете!)",
        QLineEdit::Password, "", &ok);

    if (ok && !pwd.isEmpty()) {
        masterHash = hashPassword(pwd);
        QJsonObject vault = loadVault();
        vault["master_hash"] = masterHash;
        if (!vault.contains("passwords")) {
            vault["passwords"] = QJsonArray();
        }
        saveVault(vault);
        return true;
    }
    return false;
}

bool PasswordManager::checkMasterPassword(QWidget* parentWidget) {
    if (masterHash.isEmpty()) {
        return promptNewMasterPassword(parentWidget);
    }

    bool ok;
    QString pwd = QInputDialog::getText(parentWidget, u8"Мастер-пароль",
        u8"Введите мастер-пароль для доступа к хранилищу:",
        QLineEdit::Password, "", &ok);

    if (ok && hashPassword(pwd) == masterHash) {
        return true;
    }

    if (ok) QMessageBox::warning(parentWidget, u8"Ошибка", u8"Неверный мастер-пароль!");
    return false;
}

QString PasswordManager::generatePassword() {
    const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*");
    QString randomString;
    for (int i = 0; i < 16; ++i) {
        int index = QRandomGenerator::global()->generate() % possibleCharacters.length();
        randomString.append(possibleCharacters.at(index));
    }
    return randomString;
}

void PasswordManager::savePassword(const QString& domain, const QString& login, const QString& password) {
    qWarning() << "[PasswordManager] savePassword() вызван, домен:" << domain
        << ", masterHash пуст:" << masterHash.isEmpty();

    if (masterHash.isEmpty()) {
        qWarning() << "[PasswordManager] Сохранение ОТКЛОНЕНО: хранилище ещё не создано "
            "(откройте Менеджер паролей и задайте мастер-пароль хотя бы один раз)";
        return;
    }

    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();

    QString encLogin = encryptData(login, masterHash);

    // Ищем запись с ТЕМ ЖЕ доменом И ТЕМ ЖЕ логином — обновляем пароль в ней.
    // Если это другой логин на том же домене — создаём отдельную новую запись.
    qint64 now = QDateTime::currentSecsSinceEpoch();

    bool updated = false;
    for (int i = 0; i < passwords.size(); ++i) {
        QJsonObject entry = passwords[i].toObject();
        if (entry.value("domain").toString() == domain && entry.value("login").toString() == encLogin) {
            entry["password"] = encryptData(password, masterHash);
            entry["last_used"] = now;
            passwords[i] = entry;
            updated = true;
            break;
        }
    }

    if (!updated) {
        QJsonObject newEntry;
        newEntry["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        newEntry["domain"] = domain;
        newEntry["login"] = encLogin;
        newEntry["password"] = encryptData(password, masterHash);
        newEntry["last_used"] = now;
        passwords.append(newEntry);
    }

    vault["passwords"] = passwords;
    saveVault(vault);
    qWarning() << "[PasswordManager] Пароль успешно сохранён, домен:" << domain << ", всего записей:" << passwords.size();
}

bool PasswordManager::hasExactMatch(const QString& domain, const QString& login, const QString& password) {
    if (masterHash.isEmpty()) return false;

    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();

    QString encLogin = encryptData(login, masterHash);
    QString encPwd = encryptData(password, masterHash);

    for (const auto& v : passwords) {
        QJsonObject entry = v.toObject();
        if (entry.value("domain").toString() == domain &&
            entry.value("login").toString() == encLogin &&
            entry.value("password").toString() == encPwd) {
            return true;
        }
    }
    return false;
}

void PasswordManager::touchLastUsed(const QString& domain, const QString& login) {
    if (masterHash.isEmpty()) return;

    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();
    QString encLogin = encryptData(login, masterHash);

    bool changed = false;
    for (int i = 0; i < passwords.size(); ++i) {
        QJsonObject entry = passwords[i].toObject();
        if (entry.value("domain").toString() == domain && entry.value("login").toString() == encLogin) {
            entry["last_used"] = QDateTime::currentSecsSinceEpoch();
            passwords[i] = entry;
            changed = true;
            break;
        }
    }

    if (changed) {
        vault["passwords"] = passwords;
        saveVault(vault);
    }
}

QJsonObject PasswordManager::getBestMatchForAutofill(const QString& domain) {
    if (masterHash.isEmpty()) return QJsonObject();

    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();

    QJsonObject bestEncrypted;
    qint64 bestLastUsed = -1;

    for (const auto& v : passwords) {
        QJsonObject entry = v.toObject();
        QString siteUrl = entry.value("domain").toString();

        bool sameDomain = (domain == siteUrl)
            || domain.endsWith("." + siteUrl)
            || siteUrl.endsWith("." + domain);
        if (!sameDomain) continue;

        qint64 lastUsed = entry.value("last_used").toVariant().toLongLong();
        if (bestEncrypted.isEmpty() || lastUsed > bestLastUsed) {
            bestEncrypted = entry;
            bestLastUsed = lastUsed;
        }
    }

    if (bestEncrypted.isEmpty()) return QJsonObject();

    QJsonObject result;
    result["login"] = decryptData(bestEncrypted.value("login").toString(), masterHash);
    result["password"] = decryptData(bestEncrypted.value("password").toString(), masterHash);
    return result;
}

void PasswordManager::removePassword(const QString& id) {
    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();

    QJsonArray filtered;
    for (const auto& v : passwords) {
        if (v.toObject().value("id").toString() != id) {
            filtered.append(v);
        }
    }

    vault["passwords"] = filtered;
    saveVault(vault);
}

void PasswordManager::showManagerDialog(QWidget* parentWidget) {
    if (!checkMasterPassword(parentWidget)) return;

    FramelessDialog* dialog = new FramelessDialog(u8"🔑 Менеджер паролей Storm Vault", parentWidget);
    dialog->resize(650, 480);

    QVBoxLayout* layout = dialog->contentLayout();

    // --- Таблица существующих записей ---
    QTableWidget* table = new QTableWidget(0, 4, dialog);
    table->setHorizontalHeaderLabels({ u8"Сайт", u8"Логин", u8"Копировать", u8"Удалить" });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table->setColumnWidth(2, 110);
    table->setColumnWidth(3, 80);
    layout->addWidget(table, 1);

    // reloadTable — std::function, захватывается ПО ССЫЛКЕ внутри кнопок.
    // Безопасно: dialog->exec() ниже блокирует выполнение, пока диалог открыт,
    // так что reloadTable гарантированно жива всё время, пока на кнопки можно нажать.
    std::function<void()> reloadTable;
    reloadTable = [this, table, dialog, &reloadTable]() {
        table->setRowCount(0);
        QJsonObject vault = loadVault();
        QJsonArray passwords = vault.value("passwords").toArray();

        for (const auto& v : passwords) {
            QJsonObject entry = v.toObject();
            int row = table->rowCount();
            table->insertRow(row);

            QString id = entry.value("id").toString();
            QString domain = entry.value("domain").toString();
            QString login = decryptData(entry.value("login").toString(), masterHash);
            QString pwd = decryptData(entry.value("password").toString(), masterHash);

            table->setItem(row, 0, new QTableWidgetItem(domain));
            table->setItem(row, 1, new QTableWidgetItem(login));

            QPushButton* copyBtn = new QPushButton(u8"Копировать", table);
            connect(copyBtn, &QPushButton::clicked, [pwd, dialog]() {
                QGuiApplication::clipboard()->setText(pwd);
                QMessageBox::information(dialog, u8"Скопировано", u8"Пароль скопирован в буфер обмена!");
                });
            table->setCellWidget(row, 2, copyBtn);

            QPushButton* delBtn = new QPushButton(u8"🗑", table);
            connect(delBtn, &QPushButton::clicked, this, [this, id, domain, dialog, &reloadTable]() {
                auto answer = QMessageBox::question(dialog, u8"Удалить пароль",
                    QString(u8"Удалить сохранённый пароль для «%1»?").arg(domain));
                if (answer == QMessageBox::Yes) {
                    removePassword(id);
                    reloadTable();
                }
                });
            table->setCellWidget(row, 3, delBtn);
        }
        };
    reloadTable();

    // --- Форма добавления новой записи вручную ---
    QWidget* addForm = new QWidget(dialog);
    QHBoxLayout* addLayout = new QHBoxLayout(addForm);
    addLayout->setContentsMargins(0, 10, 0, 0);

    QLineEdit* domainInput = new QLineEdit(addForm);
    domainInput->setPlaceholderText(u8"Сайт (например, google.com)");

    QLineEdit* loginInput = new QLineEdit(addForm);
    loginInput->setPlaceholderText(u8"Логин");

    QLineEdit* passwordInput = new QLineEdit(addForm);
    passwordInput->setPlaceholderText(u8"Пароль");
    passwordInput->setEchoMode(QLineEdit::Password);

    QPushButton* genBtn = new QPushButton(u8"🎲", addForm);
    genBtn->setToolTip(u8"Сгенерировать надёжный пароль");
    genBtn->setFixedWidth(36);
    connect(genBtn, &QPushButton::clicked, [this, passwordInput]() {
        passwordInput->setEchoMode(QLineEdit::Normal);
        passwordInput->setText(generatePassword());
        });

    QPushButton* addBtn = new QPushButton(u8"➕ Сохранить", addForm);
    connect(addBtn, &QPushButton::clicked, [this, domainInput, loginInput, passwordInput, dialog, &reloadTable]() {
        QString domain = domainInput->text().trimmed();
        QString login = loginInput->text().trimmed();
        QString pwd = passwordInput->text();

        if (domain.isEmpty() || pwd.isEmpty()) {
            QMessageBox::warning(dialog, u8"Ошибка", u8"Укажите хотя бы сайт и пароль.");
            return;
        }

        savePassword(domain, login, pwd);

        domainInput->clear();
        loginInput->clear();
        passwordInput->clear();
        passwordInput->setEchoMode(QLineEdit::Password);
        reloadTable();
        });

    addLayout->addWidget(domainInput, 2);
    addLayout->addWidget(loginInput, 2);
    addLayout->addWidget(passwordInput, 2);
    addLayout->addWidget(genBtn);
    addLayout->addWidget(addBtn);

    layout->addWidget(addForm);

    dialog->exec();
}

// --- СМЕНА МАСТЕР ПАРОЛЯ ---
void PasswordManager::changeMasterPassword(QWidget* parentWidget) {
    if (masterHash.isEmpty()) {
        QMessageBox::information(parentWidget, u8"Внимание", u8"Хранилище еще не создано. Сначала сохраните хотя бы один пароль.");
        return;
    }

    bool ok;
    QString currentPwd = QInputDialog::getText(parentWidget, u8"Смена пароля",
        u8"Введите ТЕКУЩИЙ мастер-пароль:", QLineEdit::Password, "", &ok);

    if (!ok || hashPassword(currentPwd) != masterHash) {
        if (ok) QMessageBox::warning(parentWidget, u8"Ошибка", u8"Неверный текущий пароль!");
        return;
    }

    QString newPwd = QInputDialog::getText(parentWidget, u8"Смена пароля",
        u8"Придумайте НОВЫЙ мастер-пароль:", QLineEdit::Password, "", &ok);

    if (!ok || newPwd.isEmpty()) return;

    QString newHash = hashPassword(newPwd);

    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();
    QJsonArray newPasswords;

    for (const auto& v : passwords) {
        QJsonObject entry = v.toObject();
        QString login = decryptData(entry.value("login").toString(), masterHash);
        QString pwd = decryptData(entry.value("password").toString(), masterHash);

        QJsonObject newEntry;
        newEntry["id"] = entry.value("id").toString();
        newEntry["domain"] = entry.value("domain").toString();
        newEntry["login"] = encryptData(login, newHash);
        newEntry["password"] = encryptData(pwd, newHash);
        newPasswords.append(newEntry);
    }

    vault["master_hash"] = newHash;
    vault["passwords"] = newPasswords;
    masterHash = newHash;
    saveVault(vault);

    QMessageBox::information(parentWidget, u8"Успех", u8"Мастер-пароль успешно изменен!");
}

// --- СБРОС ХРАНИЛИЩА ---
void PasswordManager::resetVault(QWidget* parentWidget) {
    auto btn = QMessageBox::warning(parentWidget, u8"Сброс хранилища",
        u8"ВНИМАНИЕ! Вы уверены?\n\nЭто действие безвозвратно удалит ВСЕ сохраненные пароли и мастер-пароль!",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn == QMessageBox::Yes) {
        QFile file(vaultPath);
        if (file.exists()) {
            file.remove();
        }
        masterHash.clear();
        QMessageBox::information(parentWidget, u8"Успех", u8"Хранилище полностью очищено.");
    }
}

// --- ИМПОРТ ИЗ CHROME / EDGE (CSV) ---
void PasswordManager::importPasswordsCsv(QWidget* parentWidget) {
    if (!checkMasterPassword(parentWidget)) return;

    QString fileName = QFileDialog::getOpenFileName(parentWidget, u8"Выберите CSV файл", "", u8"CSV Файлы (*.csv)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(parentWidget, u8"Ошибка", u8"Не удалось открыть файл.");
        return;
    }

    QTextStream in(&file);
    QString header = in.readLine();
    int count = 0;

    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();

    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList parts;
        QString current = "";
        bool inQuotes = false;

        for (int i = 0; i < line.length(); ++i) {
            QChar c = line[i];
            if (c == '\"') {
                inQuotes = !inQuotes;
            }
            else if (c == ',' && !inQuotes) {
                parts.append(current);
                current = "";
            }
            else {
                current.append(c);
            }
        }
        parts.append(current);

        if (parts.size() >= 4) {
            QString url = parts[1];
            QString username = parts[2];
            QString pwd = parts[3];

            if (!url.isEmpty() && !pwd.isEmpty()) {
                QUrl qurl(url);
                QString domain = qurl.host().isEmpty() ? url : qurl.host();

                QJsonObject entry;
                entry["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                entry["domain"] = domain;
                entry["login"] = encryptData(username, masterHash);
                entry["password"] = encryptData(pwd, masterHash);
                passwords.append(entry);
                count++;
            }
        }
    }

    vault["passwords"] = passwords;
    saveVault(vault);
    file.close();

    QMessageBox::information(parentWidget, u8"Успех", QString(u8"Успешно импортировано паролей: %1").arg(count));
}

QJsonArray PasswordManager::getAllDecrypted() {
    QJsonArray result;
    if (masterHash.isEmpty()) return result;

    QJsonObject vault = loadVault();
    QJsonArray passwords = vault.value("passwords").toArray();

    for (const auto& v : passwords) {
        QJsonObject entry = v.toObject();
        QJsonObject item;
        item["site_url"] = entry.value("domain").toString();
        item["login"] = decryptData(entry.value("login").toString(), masterHash);
        item["password"] = decryptData(entry.value("password").toString(), masterHash);
        result.append(item);
    }
    return result;
}

int PasswordManager::getPasswordCount() {
    QJsonObject vault = loadVault();
    return vault.value("passwords").toArray().size();
}