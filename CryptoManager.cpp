#include "CryptoManager.h"
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <QDateTime>
#include <QDebug>

CryptoManager::CryptoManager() {}

CryptoManager::~CryptoManager() {
    if (rsaKeyPair) EVP_PKEY_free(rsaKeyPair);
}

bool CryptoManager::generateRsaKeys() {
    // Раньше при повторном вызове (например, при каждом переподключении к
    // чату — см. "joined" в VoiceChatWidget::handlePacket()) старый rsaKeyPair
    // просто затирался новым указателем без EVP_PKEY_free() — утечка на
    // каждый реконнект. Заодно освобождаем ctx на всех путях выхода, а не
    // только при успехе.
    if (rsaKeyPair) { EVP_PKEY_free(rsaKeyPair); rsaKeyPair = nullptr; }

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) return false;
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return false; }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) { EVP_PKEY_CTX_free(ctx); return false; }
    if (EVP_PKEY_keygen(ctx, &rsaKeyPair) <= 0) { EVP_PKEY_CTX_free(ctx); return false; }
    EVP_PKEY_CTX_free(ctx);
    return true;
}

QByteArray CryptoManager::getPublicKeyPem() const {
    if (!rsaKeyPair) return QByteArray();
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, rsaKeyPair);
    char* pemData;
    long pemLen = BIO_get_mem_data(bio, &pemData);
    QByteArray pubKey(pemData, pemLen);
    BIO_free(bio);
    return pubKey;
}

QByteArray CryptoManager::decryptRsa(const QByteArray& encryptedData) const {
    if (!rsaKeyPair) return QByteArray();
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(rsaKeyPair, NULL);
    EVP_PKEY_decrypt_init(ctx);
    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING); // Совместимость с Python rsa

    size_t outLen;
    EVP_PKEY_decrypt(ctx, NULL, &outLen, (const unsigned char*)encryptedData.data(), encryptedData.size());
    QByteArray decrypted(outLen, 0);
    EVP_PKEY_decrypt(ctx, (unsigned char*)decrypted.data(), &outLen, (const unsigned char*)encryptedData.data(), encryptedData.size());
    decrypted.resize(outLen);
    EVP_PKEY_CTX_free(ctx);
    return decrypted;
}

QByteArray CryptoManager::encryptRsaWithPublicKey(const QByteArray& publicKeyPem, const QByteArray& data) const {
    // Зеркало decryptRsa(), только чужим публичным ключом (из чужого
    // request_room_key) и на шифрование — используется, когда МЫ отвечаем на
    // запрос ключа комнаты от другого участника (см. VoiceChatWidget::handlePacket(),
    // ветка "request_room_key").
    BIO* bio = BIO_new_mem_buf(publicKeyPem.constData(), publicKeyPem.size());
    if (!bio) return QByteArray();
    EVP_PKEY* pubKey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pubKey) return QByteArray();

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pubKey, NULL);
    if (!ctx || EVP_PKEY_encrypt_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) { // тот же паддинг, что и в decryptRsa()
        if (ctx) EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pubKey);
        return QByteArray();
    }

    size_t outLen = 0;
    if (EVP_PKEY_encrypt(ctx, NULL, &outLen, (const unsigned char*)data.constData(), data.size()) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pubKey);
        return QByteArray();
    }
    QByteArray encrypted(static_cast<int>(outLen), 0);
    if (EVP_PKEY_encrypt(ctx, (unsigned char*)encrypted.data(), &outLen,
        (const unsigned char*)data.constData(), data.size()) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pubKey);
        return QByteArray();
    }
    encrypted.resize(static_cast<int>(outLen));

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pubKey);
    return encrypted;
}

void CryptoManager::generateRoomKey() {
    // Локальная генерация ключа комнаты — когда мы первый/единственный
    // участник и ответить на request_room_key некому (см.
    // VoiceChatWidget::onRoomKeyTimeout()). 32 байта — как ожидает
    // decryptFernet()/encryptFernet() (roomKey.size() != 32 -> нет ключа).
    QByteArray key(32, 0);
    RAND_bytes((unsigned char*)key.data(), 32);
    roomKey = key;
}

bool CryptoManager::hasRoomKey() const {
    return roomKey.size() == 32;
}

QByteArray CryptoManager::getRoomKeyBase64Url() const {
    // Тот же base64url-формат, что принимает setRoomKey() — так что
    // результат этого метода можно RSA-зашифровать чужим ключом и отправить
    // как есть в поле "encrypted_key" пакета "room_key_response" (получатель
    // расшифрует RSA и передаст результат прямо в setRoomKey()).
    return roomKey.toBase64(QByteArray::Base64UrlEncoding);
}

void CryptoManager::clearRoomKey() {
    // Вызывается при старте нового подключения (см. VoiceChatWidget::startChat()) —
    // без этого ключ от ПРЕДЫДУЩЕЙ комнаты/сессии оставался бы висеть в
    // roomKey и молча использовался бы для новой комнаты (roomKey.size()==32
    // прошёл бы проверку "есть ключ", хотя ключ на самом деле не тот).
    roomKey.clear();
}

void CryptoManager::setRoomKey(const QByteArray& base64UrlKey) {
    // Fernet ключ в Python передается в Base64 (url-safe)
    roomKey = QByteArray::fromBase64(base64UrlKey, QByteArray::Base64UrlEncoding);
}

QString CryptoManager::decryptFernet(const QByteArray& fernetTokenUrl64) const {
    if (roomKey.size() != 32) return "[Ошибка: Нет ключа комнаты]";

    QByteArray token = QByteArray::fromBase64(fernetTokenUrl64, QByteArray::Base64UrlEncoding);
    if (token.isEmpty() || token[0] != (char)0x80) return "[Ошибка: Неверный формат E2EE]";

    // Структура Fernet: 
    // [0] = 0x80 (Version)
    // [1..8] = Timestamp
    // [9..24] = IV (16 байт)
    // [25..end-32] = Ciphertext
    // [end-32..end] = HMAC SHA256

    QByteArray iv = token.mid(9, 16);
    QByteArray ciphertext = token.mid(25, token.size() - 25 - 32);

    // Ключ делится на две части: первые 16 байт для HMAC, вторые 16 для AES
    QByteArray encKey = roomKey.mid(16, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, (const unsigned char*)encKey.data(), (const unsigned char*)iv.data());

    QByteArray plaintext;
    plaintext.resize(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen1 = 0, outLen2 = 0;

    EVP_DecryptUpdate(ctx, (unsigned char*)plaintext.data(), &outLen1, (const unsigned char*)ciphertext.data(), ciphertext.size());
    EVP_DecryptFinal_ex(ctx, (unsigned char*)plaintext.data() + outLen1, &outLen2);
    EVP_CIPHER_CTX_free(ctx);

    plaintext.resize(outLen1 + outLen2);
    return QString::fromUtf8(plaintext);
}

QString CryptoManager::encryptFernet(const QString& plainText) const {
    if (roomKey.size() != 32) return plainText; // Fallback если ключа еще нет

    QByteArray encKey = roomKey.mid(16, 16);
    QByteArray signKey = roomKey.left(16);

    // 1. Генерируем IV (16 байт)
    QByteArray iv(16, 0);
    RAND_bytes((unsigned char*)iv.data(), 16);

    // 2. Шифруем AES-128-CBC
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, (const unsigned char*)encKey.data(), (const unsigned char*)iv.data());

    QByteArray plainBytes = plainText.toUtf8();
    QByteArray ciphertext;
    ciphertext.resize(plainBytes.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen1 = 0, outLen2 = 0;

    EVP_EncryptUpdate(ctx, (unsigned char*)ciphertext.data(), &outLen1, (const unsigned char*)plainBytes.data(), plainBytes.size());
    EVP_EncryptFinal_ex(ctx, (unsigned char*)ciphertext.data() + outLen1, &outLen2);
    ciphertext.resize(outLen1 + outLen2);
    EVP_CIPHER_CTX_free(ctx);

    // 3. Собираем токен до HMAC (Version + Timestamp + IV + Ciphertext)
    QByteArray payload;
    payload.append((char)0x80);
    quint64 timestamp = QDateTime::currentSecsSinceEpoch();
    for (int i = 7; i >= 0; --i) payload.append((char)((timestamp >> (i * 8)) & 0xFF)); // Big-endian
    payload.append(iv);
    payload.append(ciphertext);

    // 4. Подпись HMAC SHA256
    unsigned char macResult[32];
    unsigned int macLen = 0;
    HMAC(EVP_sha256(), signKey.data(), signKey.size(), (const unsigned char*)payload.data(), payload.size(), macResult, &macLen);

    payload.append((const char*)macResult, macLen);

    return payload.toBase64(QByteArray::Base64UrlEncoding);
}