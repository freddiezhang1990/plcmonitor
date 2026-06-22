#include "simplecrypt.h"

SimpleCrypt::SimpleCrypt(quint64 key) : m_key(key) {}

QByteArray SimpleCrypt::xorEncryptDecrypt(const QByteArray& data) const
{
    QByteArray result = data;
    quint64 key = m_key;
    int keySize = sizeof(key);
    for (int i = 0; i < result.size(); ++i) {
        result[i] = result[i] ^ ((key >> ((i % keySize) * 8)) & 0xFF);
    }
    return result;
}

QString SimpleCrypt::encryptString(const QString& plaintext) const
{
    QByteArray plainData = plaintext.toUtf8();
    QByteArray encrypted = xorEncryptDecrypt(plainData);
    return encrypted.toBase64();
}

QString SimpleCrypt::decryptString(const QString& ciphertext) const
{
    QByteArray encrypted = QByteArray::fromBase64(ciphertext.toLatin1());
    QByteArray decrypted = xorEncryptDecrypt(encrypted);
    return QString::fromUtf8(decrypted);
}