#ifndef SIMPLECRYPT_H
#define SIMPLECRYPT_H

#include <QString>
#include <QByteArray>

class SimpleCrypt
{
public:
    explicit SimpleCrypt(quint64 key = 0x1234567890abcdefULL);
    QString encryptString(const QString& plaintext) const;
    QString decryptString(const QString& ciphertext) const;

private:
    quint64 m_key;
    QByteArray xorEncryptDecrypt(const QByteArray& data) const;
};

#endif // SIMPLECRYPT_H