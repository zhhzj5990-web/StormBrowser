#pragma once
#pragma once
#include <QByteArray>
#include <QString>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>

class CryptoManager {
public:
    CryptoManager();
    ~CryptoManager();

    // RSA: Генерация ключей и получение публичного ключа для отправки на сервер
    bool generateRsaKeys();
    QByteArray getPublicKeyPem() const;

    // RSA: Расшифровка ключа комнаты (от сервера)
    QByteArray decryptRsa(const QByteArray& encryptedData) const;

    // Fernet: Шифрование и дешифровка сообщений
    void setRoomKey(const QByteArray& base64UrlKey);
    QString encryptFernet(const QString& plainText) const;
    QString decryptFernet(const QByteArray& fernetToken) const;

    // Fernet: генерация НОВОГО ключа комнаты локально — используется, когда
    // отвечать на наш request_room_key некому (мы первый/единственный участник
    // комнаты, см. VoiceChatWidget::onRoomKeyTimeout()). getRoomKeyBase64Url()
    // возвращает ключ в том же base64url-виде, что принимает setRoomKey(), —
    // этим же значением (уже RSA-зашифрованным чужим ключом) мы отвечаем на
    // ЧУЖОЙ request_room_key (см. encryptRsaWithPublicKey()).
    void generateRoomKey();
    bool hasRoomKey() const;
    QByteArray getRoomKeyBase64Url() const;
    void clearRoomKey();

    // RSA: шифрование данных чужим публичным ключом (PEM) — нужно, когда МЫ
    // отвечаем на request_room_key другого участника (в отличие от
    // decryptRsa(), который расшифровывает СВОИМ приватным ключом ответ на
    // НАШ собственный запрос).
    QByteArray encryptRsaWithPublicKey(const QByteArray& publicKeyPem, const QByteArray& data) const;

private:
    EVP_PKEY* rsaKeyPair = nullptr;
    QByteArray roomKey; // Декодированный из Base64Url 32-байтный ключ
};