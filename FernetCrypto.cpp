#include "FernetCrypto.h"
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QDateTime>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace {

QByteArray aesCbcEncrypt(const QByteArray& key16, const QByteArray& iv16, const QByteArray& plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QByteArray();

    QByteArray out(plaintext.size() + EVP_MAX_BLOCK_LENGTH, 0);
    int outLen1 = 0, outLen2 = 0;
    bool okInit = EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
        reinterpret_cast<const unsigned char*>(key16.constData()),
        reinterpret_cast<const unsigned char*>(iv16.constData())) == 1;

    bool okUpdate = okInit && EVP_EncryptUpdate(ctx,
        reinterpret_cast<unsigned char*>(out.data()), &outLen1,
        reinterpret_cast<const unsigned char*>(plaintext.constData()), plaintext.size()) == 1;

    bool okFinal = okUpdate && EVP_EncryptFinal_ex(ctx,
        reinterpret_cast<unsigned char*>(out.data()) + outLen1, &outLen2) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if (!okFinal) return QByteArray();

    out.resize(outLen1 + outLen2);
    return out;
}

QByteArray aesCbcDecrypt(const QByteArray& key16, const QByteArray& iv16, const QByteArray& ciphertext, bool& ok) {
    ok = false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QByteArray();

    QByteArray out(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, 0);
    int outLen1 = 0, outLen2 = 0;
    bool okInit = EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
        reinterpret_cast<const unsigned char*>(key16.constData()),
        reinterpret_cast<const unsigned char*>(iv16.constData())) == 1;

    bool okUpdate = okInit && EVP_DecryptUpdate(ctx,
        reinterpret_cast<unsigned char*>(out.data()), &outLen1,
        reinterpret_cast<const unsigned char*>(ciphertext.constData()), ciphertext.size()) == 1;

    // EVP_DecryptFinal_ex возвращает 0/ошибку если padding битый (неверный ключ/повреждённые данные)
    bool okFinal = okUpdate && EVP_DecryptFinal_ex(ctx,
        reinterpret_cast<unsigned char*>(out.data()) + outLen1, &outLen2) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if (!okFinal) return QByteArray();

    out.resize(outLen1 + outLen2);
    ok = true;
    return out;
}

} // namespace

QByteArray FernetCrypto::deriveKey(const QByteArray& secret, const QByteArray& salt, int iterations) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, secret, salt, iterations, 32);
}

QString FernetCrypto::hmacSha256Hex(const QByteArray& secret, const QByteArray& data) {
    QByteArray digest = QMessageAuthenticationCode::hash(data, secret, QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

QString FernetCrypto::encrypt(const QString& plaintext, const QByteArray& key32) {
    if (key32.size() != 32) return QString();

    QByteArray signingKey = key32.left(16);
    QByteArray encKey     = key32.mid(16, 16);

    QByteArray iv(16, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), 16) != 1) return QString();

    QByteArray cipherBytes = aesCbcEncrypt(encKey, iv, plaintext.toUtf8());
    if (cipherBytes.isEmpty()) return QString();

    // 8-байтный big-endian timestamp — так же, как Python Fernet
    quint64 t = static_cast<quint64>(QDateTime::currentSecsSinceEpoch());
    QByteArray tsBytes(8, 0);
    for (int i = 7; i >= 0; --i) {
        tsBytes[i] = static_cast<char>(t & 0xFF);
        t >>= 8;
    }

    QByteArray basicParts;
    basicParts.append(char(0x80));
    basicParts.append(tsBytes);
    basicParts.append(iv);
    basicParts.append(cipherBytes);

    QByteArray hmacDigest = QMessageAuthenticationCode::hash(basicParts, signingKey, QCryptographicHash::Sha256);
    QByteArray tokenBytes = basicParts + hmacDigest;

    return QString::fromLatin1(tokenBytes.toBase64(QByteArray::Base64UrlEncoding));
}

QString FernetCrypto::decrypt(const QString& token, const QByteArray& key32, bool& ok) {
    ok = false;
    if (key32.size() != 32) return QString();

    QByteArray signingKey = key32.left(16);
    QByteArray encKey     = key32.mid(16, 16);

    QByteArray raw = QByteArray::fromBase64(token.toUtf8(), QByteArray::Base64UrlEncoding);
    if (raw.size() < 1 + 8 + 16 + 32) return QString(); // слишком короткий токен
    if (static_cast<unsigned char>(raw.at(0)) != 0x80) return QString(); // неизвестная версия

    QByteArray iv            = raw.mid(9, 16);
    QByteArray hmacReceived  = raw.right(32);
    QByteArray ciphertext    = raw.mid(25, raw.size() - 25 - 32);
    QByteArray basicParts    = raw.left(raw.size() - 32);

    QByteArray hmacComputed = QMessageAuthenticationCode::hash(basicParts, signingKey, QCryptographicHash::Sha256);
    if (hmacComputed != hmacReceived) return QString(); // подпись не совпала — неверный ключ или подмена данных

    bool decOk = false;
    QByteArray plainBytes = aesCbcDecrypt(encKey, iv, ciphertext, decOk);
    if (!decOk) return QString();

    ok = true;
    return QString::fromUtf8(plainBytes);
}
