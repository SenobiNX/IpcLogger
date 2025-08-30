#include "hk/diag/diag.h"
#include "hk/os/Thread.h"
#include "hk/services/sm.h"
#include "hk/services/socket/address.h"
#include "hk/services/socket/config.h"
#include "hk/services/socket/service.h"
#include "hk/sf/hipc.h"
#include "hk/svc/api.h"
#include "hk/svc/types.h"
#include "hk/types.h"
#include "hk/util/Queue.h"
#include "hk/util/Storage.h"
#include "hk/util/Stream.h"
#include "hk/util/Tuple.h"
#include "hk/util/Vec.h"
#include <cstdio>

constexpr hk::socket::ServiceConfig cConfig;
alignas(hk::cPageSize) u8 buffer[cConfig.calcTransferMemorySize() + 0x20000];

using LogQueue = hk::util::Queue<hk::Tuple<std::span<const u8>, bool>>;

static void socketThread(LogQueue* queue) {
    auto* socket = hk::socket::Socket::initialize(cConfig, buffer);

    auto [err, errno] = socket->socket(hk::socket::AddressFamily::Ipv4, hk::socket::Type::Stream, hk::socket::Protocol(0));

    s32 fd = err;
    HK_ABORT_UNLESS(fd >= 0, "fd: %d, errno: %d", fd, errno);

    tie(err, errno) = socket->bind(fd, hk::socket::SocketAddrIpv4::parse<"0.0.0.0">(8000));
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    tie(err, errno) = socket->listen(fd, 1);
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    s32 clientFd = -1;
    hk::socket::SocketAddrIpv4 client;
    tie(clientFd, errno, client) = socket->accept(fd);
    HK_ABORT_UNLESS(clientFd >= 0, "clientFd: %d, errno: %d", clientFd, errno);

    const auto log = [&](const char* message) {
        tie(err, errno) = socket->send(clientFd, std::span<const u8>(cast<const u8*>(message), strlen(message)), 0);
    };

    log("hi\n");

    while (true) {
        auto [msg, shouldNewline] = queue->take();

        tie(err, errno) = socket->send(clientFd, msg, 0);
        if (shouldNewline)
            log("\n");

        delete[] msg.data();
    }
}

extern "C" void hkMain() {
    hk::sm::ServiceManager::initialize()->registerClient();

    hk::Handle port;
    // make sure to remove if it was left open
    hk::svc::ManageNamedPort(&port, "hklog", 0);
    HK_ABORT_UNLESS_R(hk::svc::ManageNamedPort(&port, "hklog", 1));

    LogQueue queue;
    hk::os::Thread thread(socketThread, &queue);
    thread.setName("SocketThread");
    thread.start();

    std::array<u8, 28> data = {};
    struct Handles {
        hk::Handle port;
        hk::Handle client = 0;
    } handles = { .port = port };
    bool reply = false;
    while (true) {
        u32 handleIndex = 0;
        auto result = hk::svc::ReplyAndReceive(&handleIndex, &handles.port, handles.client ? 2 : 1, reply ? handles.client : 0, -1);
        reply = false;
        switch (result.getValue()) {
        case 0: {
            if (handleIndex == 0) {
                HK_ABORT_UNLESS_R(hk::svc::AcceptSession(&handles.client, handles.port));
                continue;
            }
            hk::util::Stream stream(hk::svc::getTLS()->ipcMessageBuffer, hk::sf::cTlsBufferSize);
            auto header = stream.read<hk::sf::hipc::Header>();
            auto buffer = stream.read<hk::sf::hipc::Buffer>();
            if (header.sendBufferCount != 1) {
                hk::svc::CloseHandle(handles.client);
                HK_ABORT("invalid handle :(", 0);
                continue;
            }

            u8* msg = new u8[buffer.size()];
            if (msg == nullptr)
                while (!queue.empty())
                    delete[] queue.take().a.data();
            else {
                memcpy(msg, cast<const void*>(buffer.address()), buffer.size());
                queue.add({ { msg, buffer.size() }, header.tag == 0 });
            }

            stream.seek(0);
            stream.write(hk::sf::hipc::Header { .dataWords = 4 });
            reply = true;
            continue;
        }
        case 0xf601: {
            if (handleIndex == 0)
                HK_ABORT("port erroneously closed by kernel", 0);

            hk::svc::CloseHandle(handles.client);
            handles.client = 0;
            continue;
        }
        default: {
            HK_ABORT("new and unique result %x", result.getValue());
        }
        }
    }
}
