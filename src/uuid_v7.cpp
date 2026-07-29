#include "uuid_v7.h"

#include <QByteArray>
#include <QDateTime>
#include <QRandomGenerator>
#include <QtGlobal>

QUuid createUuidV7()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return QUuid::createUuidV7();
#else
    QByteArray bytes(16, '\0');

    const qint64 current_timestamp_ms = QDateTime::currentMSecsSinceEpoch();
    const quint64 timestamp_ms = current_timestamp_ms < 0
                                     ? 0
                                     : static_cast<quint64>(current_timestamp_ms);

    bytes[0] = static_cast<char>((timestamp_ms >> 40) & 0xffU);
    bytes[1] = static_cast<char>((timestamp_ms >> 32) & 0xffU);
    bytes[2] = static_cast<char>((timestamp_ms >> 24) & 0xffU);
    bytes[3] = static_cast<char>((timestamp_ms >> 16) & 0xffU);
    bytes[4] = static_cast<char>((timestamp_ms >> 8) & 0xffU);
    bytes[5] = static_cast<char>(timestamp_ms & 0xffU);

    QRandomGenerator *random_generator = QRandomGenerator::system();
    const quint64 random_high = random_generator->generate64();
    const quint32 random_low = random_generator->generate();

    for (int i = 0; i < 8; i++)
    {
        const int shift = (7 - i) * 8;
        bytes[6 + i] = static_cast<char>((random_high >> shift) & 0xffU);
    }

    bytes[14] = static_cast<char>((random_low >> 8) & 0xffU);
    bytes[15] = static_cast<char>(random_low & 0xffU);

    bytes[6] = static_cast<char>((static_cast<quint8>(bytes.at(6)) & 0x0fU) | 0x70U);
    bytes[8] = static_cast<char>((static_cast<quint8>(bytes.at(8)) & 0x3fU) | 0x80U);

    return QUuid::fromRfc4122(bytes);
#endif
}
