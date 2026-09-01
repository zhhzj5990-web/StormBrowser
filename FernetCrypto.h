#pragma once
#include <QString>
#include <QByteArray>

// Совместимая с Python `cryptography.fernet.Fernet` реализация:
// AES-128-CBC (шифр) + HMAC-SHA256 (подпись), формат токена:
// base64url( 0x80 + timestamp(8 байт BE) + IV(16 байт) + ciphertext + HMAC-SHA256(32 байта) )
//
// Ключ (32 байта) получается через PBKDF2-HMAC-SHA256 — используем Qt'шный
// QPasswordDigestor (доступен без OpenSSL с Qt 5.12+). Само шифрование AES
// требует OpenSSL (EVP), т.к. в публичном API Qt его нет.
class FernetCrypto {
public:
    // secret — обычно пароль пользователя (как в Python-версии), salt — соль для PBKDF2.
    // iterations=180000 — совпадает с сервером/старой Python-версией, менять нельзя.
    static QByteArray deriveKey(const QByteArray& secret, const QByteArray& salt, int iterations = 180000);

    // plaintext -> fernet-токен (готовая строка для отправки на сервер)
    static QString encrypt(const QString& plaintext, const QByteArray& key32);

    // fernet-токен -> plaintext. ok=false при ошибке (неверный ключ/повреждённые данные)
    static QString decrypt(const QString& token, const QByteArray& key32, bool& ok);

    // Обычный HMAC-SHA256 в hex (для подписи payload, отдельно от Fernet-шифрования)
    static QString hmacSha256Hex(const QByteArray& secret, const QByteArray& data);
};