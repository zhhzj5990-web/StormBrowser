#include "SingleInstanceGuard.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QDebug>

SingleInstanceGuard::SingleInstanceGuard(const QString& key, QObject* parent)
    : QObject(parent), sharedMemKey(key), sharedMemory(key) {

    // На Linux/macOS сегмент QSharedMemory может "залипнуть" после падения
    // предыдущего процесса (аварийное завершение не всегда штатно освобождает
    // сегмент) — тогда create() ошибочно решит, что кто-то ещё жив, хотя на
    // самом деле нет. Стандартный обходной манёвр: attach()+detach() ДО
    // create() — если сегмент был "мёртвым" (без единого живого подключения),
    // это принудительно освобождает его, и следующий create() честно
    // отработает. На Windows (основная платформа для этого браузера) такой
    // проблемы нет — там сегмент и так живёт ровно пока жив процесс-владелец,
    // но лишний attach()+detach() безвреден и для кросс-платформенности его
    // оставляем.
    if (sharedMemory.attach()) {
        sharedMemory.detach();
    }

    if (sharedMemory.create(1)) {
        // Мы — первый и единственный процесс.
        isPrimary = true;

        // На Unix-сокет QLocalServer может остаться на диске после не
        // штатного завершения предыдущего запуска — removeServer() убирает
        // такой "мусор" перед listen(), иначе listen() тихо провалится и
        // следующий второй экземпляр решит, что первого не существует.
        QLocalServer::removeServer(sharedMemKey);
        localServer = new QLocalServer(this);
        connect(localServer, &QLocalServer::newConnection, this, &SingleInstanceGuard::handleIncomingConnection);
        if (!localServer->listen(sharedMemKey)) {
            // Не критично: худший случай — второй запуск не сможет "разбудить"
            // окно первого и просто тихо закроется сам, защита от двух
            // одновременно работающих процессов при этом всё равно работает,
            // потому что она держится на QSharedMemory, а не на QLocalServer.
            qWarning() << "[SingleInstance] Не удалось поднять QLocalServer:" << localServer->errorString();
        }
    }
    else {
        // Другой процесс уже держит сегмент — "будим" его коротким сигналом
        // и сами дальше не живём (isPrimaryInstance() вернёт false, main()
        // должен сразу завершиться).
        isPrimary = false;
        QLocalSocket socket;
        socket.connectToServer(sharedMemKey);
        if (socket.waitForConnected(200)) {
            socket.write("activate");
            socket.waitForBytesWritten(200);
            socket.disconnectFromServer();
        }
    }
}

SingleInstanceGuard::~SingleInstanceGuard() {
    if (localServer) {
        localServer->close();
    }
}

void SingleInstanceGuard::handleIncomingConnection() {
    QLocalSocket* socket = localServer->nextPendingConnection();
    if (!socket) return;
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    // Само содержимое сообщения не важно — сам факт входящего подключения
    // уже означает "кто-то попытался запустить второй экземпляр".
    emit anotherInstanceStarted();
}
