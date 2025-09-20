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
#include "hk/util/FixedVec.h"
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
    auto* socket = HK_UNWRAP(hk::socket::Socket::initialize(cConfig, buffer));

    auto [err, errno] = HK_UNWRAP(socket->socket(hk::socket::AddressFamily::Ipv4, hk::socket::Type::Stream, hk::socket::Protocol(0)));

    s32 fd = err;
    HK_ABORT_UNLESS(fd >= 0, "fd: %d, errno: %d", fd, errno);

    tie(err, errno) = socket->bind(fd, hk::socket::SocketAddrIpv4::parse<"0.0.0.0">(8000));
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    tie(err, errno) = socket->listen(fd, 1);
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    while (true) {
        s32 clientFd = -1;
        hk::socket::SocketAddrIpv4 client;
        tie(clientFd, errno, client) = HK_UNWRAP(socket->accept(fd));
        HK_ABORT_UNLESS(clientFd >= 0, "clientFd: %d, errno: %d", clientFd, errno);

        const auto log = [&](const char* message) {
            tie(err, errno) = HK_UNWRAP(socket->send(clientFd, std::span<const u8>(cast<const u8*>(message), strlen(message)), 0));
            if (err < 0)
                clientFd = 0;
        };

        log("hi\n");

        while (clientFd) {
            auto [msg, shouldNewline] = queue->take();

            tie(err, errno) = HK_UNWRAP(socket->send(clientFd, msg, 0));
            if (shouldNewline)
                log("\n");

            delete[] msg.data();
        }
    }
}

constexpr u32 cMaxSessions = 16;

extern "C" void hkMain() {
    HK_UNWRAP(hk::sm::ServiceManager::initialize())->registerClient();

    hk::Handle port;
    // make sure to remove if it was left open
    hk::svc::ManageNamedPort(&port, "hklog", 0);

    HK_ABORT_UNLESS_R(hk::svc::ManageNamedPort(&port, "hklog", 1));

    LogQueue queue;
    hk::os::Thread thread(socketThread, &queue, 0, 8_KB);
    thread.setName("SocketThread");
    thread.start();

    std::array<u8, 28> data = {};
    hk::util::FixedVec<hk::Handle, 1 + cMaxSessions> handles;
    handles.add(port);
    hk::Handle replyHandle = 0;
    while (true) {
        u32 handleIndex = 0;
        auto result = hk::svc::ReplyAndReceive(&handleIndex, handles.begin(), handles.size(), replyHandle, -1);
        replyHandle = 0;
        hk::Handle currentHandle = handles[handleIndex];
        switch (result.getValue()) {
        case 0: {
            if (handleIndex == 0) {
                handles.add(HK_UNWRAP(hk::svc::AcceptSession(port)));
                continue;
            }
            const auto closeHandle = [&] {
                hk::svc::CloseHandle(currentHandle);
                handles.remove(handleIndex);
            };
            hk::util::Stream stream(hk::svc::getTLS()->ipcMessageBuffer, hk::sf::cTlsBufferSize);
            auto header = stream.read<hk::sf::hipc::Header>();

            switch (header.tag) {
            case 0:
            case 1: {
                if (header.sendBufferCount != 1) {
                    closeHandle();
                    continue;
                }

                auto buffer = stream.read<hk::sf::hipc::Buffer>();
                stream.seek(0);

                u8* msg = new u8[buffer.size()];
                while (msg == nullptr) {
                    while (!queue.empty())
                        delete[] queue.take().a.data();
                    msg = new u8[buffer.size()];
                }

                memcpy(msg, cast<const void*>(buffer.address()), buffer.size());
                queue.add({ { msg, buffer.size() }, header.tag == 0 });

                stream.write(hk::sf::hipc::Header { .tag = 0, .dataWords = 4 });
                break;
            }
            case 2: {
                auto session = HK_UNWRAP(hk::svc::CreateSession(false, 0x40040880));
                handles.add(session.server);
                stream.seek(0);
                stream.write(hk::sf::hipc::Header { .tag = 0, .dataWords = 8, .hasSpecialHeader = true });
                stream.write(hk::sf::hipc::SpecialHeader {
                    .moveHandleCount = 1,
                });
                stream.write(session.client);
                break;
            }
            case 3: {
                stream.seek(0);
                stream.write(hk::sf::hipc::Header { .tag = 0, .dataWords = 4 });
                break;
            }
            default: {
                stream.seek(0);
                stream.write(hk::sf::hipc::Header { .tag = 1, .dataWords = 4 });
                break;
            }
            }
            replyHandle = currentHandle;
            continue;
        }
        case 0xf601: {
            if (handleIndex == 0)
                HK_ABORT("port erroneously closed by kernel", 0);

            hk::svc::CloseHandle(currentHandle);
            handles.remove(handleIndex);
            continue;
        }
        default:
            HK_ABORT("unhandled result %x", result.getValue());
        }
    }
}
