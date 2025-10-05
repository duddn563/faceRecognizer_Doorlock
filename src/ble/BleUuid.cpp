#include <QUuid>
#include <QtBluetooth/QBluetoothUuid>

extern const QBluetoothUuid SERVICE_UUID   { QUuid(QStringLiteral("0000ffff-0000-1000-8000-00805f9b34fb")) };
extern const QBluetoothUuid CHAR_CMD_UUID  { QUuid(QStringLiteral("0000ff01-0000-1000-8000-00805f9b34fb")) }; // WRITE
extern const QBluetoothUuid CHAR_STATE_UUID{ QUuid(QStringLiteral("0000ff02-0000-1000-8000-00805f9b34fb")) }; // READ/NOTIFY
extern const QBluetoothUuid CHAR_NOTIFY_UUID{ QUuid(QStringLiteral("0000ff03-0000-1000-8000-00805f9b34fb")) }; // NOTIFY 전용(충돌 방지)

