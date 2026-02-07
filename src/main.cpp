#include "hk/os/Thread.h"
#include "hk/services/sm.h"
#include "hk/services/socket/service.h"
#include <atomic>

constexpr hk::socket::ServiceConfig cConfig = hk::socket::ServiceConfig {
    .udpTxBufSize = 0,
    .udpRxBufSize = 0,
    .sbEfficiency = 4,
};
alignas(hk::cPageSize) u8 buffer[cConfig.calcTransferMemorySize()];
static std::atomic<s32> clientFd = -1;

static void socketThread(hk::socket::Socket* socket) {
    auto [err, errno] = HK_UNWRAP(socket->socket(hk::socket::AddressFamily::Ipv4, hk::socket::Type::Stream, hk::socket::Protocol(6)));

    s32 fd = err;
    HK_ABORT_UNLESS(fd >= 0, "fd: %d, errno: %d", fd, errno);

    tie(err, errno) = socket->bind(fd, hk::socket::SocketAddrIpv4::parse<"0.0.0.0">(8000));
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    tie(err, errno) = socket->listen(fd, 1);
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    s32 cfd = -1;
    while (true) {
        hk::socket::SocketAddrIpv4 client;

        tie(cfd, errno, client) = HK_UNWRAP(socket->accept(fd));
        HK_ABORT_UNLESS(cfd >= 0, "cfd: %d, errno: %d", cfd, errno);

        int old = clientFd.exchange(cfd, std::memory_order_relaxed);

        tie(err, errno) = HK_UNWRAP(socket->send<const char>(cfd, "Connected!\n\n", 0));
        HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

        do {
            // send a nul byte until the client disconnects.
            // why aren't we polling for hang-up or recving until done?
            // that's because with the current code, the client socket is treated
            // as though its read half of the stream has ended. i'm not sure why
            // this happens, but i don't care to find out anymore.
            std::array<u8, 1> buffer = {};
            tie(err, errno) = HK_UNWRAP(socket->send<u8>(cfd, buffer, 0));
            hk::svc::SleepThread(500_ms);
        } while (err >= 0);

        HK_UNWRAP(socket->close(cfd));
    }
}

constexpr u32 cMaxSessions = 16;

extern "C" void hkMain() {
    HK_UNWRAP(hk::sm::ServiceManager::initialize())->registerClient();

    hk::Handle port;
    // make sure to remove if it was left open
    hk::svc::ManageNamedPort(&port, "hklog", 0);

    HK_ABORT_UNLESS_R(hk::svc::ManageNamedPort(&port, "hklog", cMaxSessions));

    auto* socket = HK_UNWRAP(hk::socket::Socket::initialize<"bsd:s">(cConfig, buffer));
    hk::os::Thread thread(socketThread, socket, 0, 4_KB);
    thread.setName("SocketServer");
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

                s32 fd = clientFd.load(std::memory_order_relaxed);
                HK_UNWRAP(socket->send(fd, buffer.span<const u8>(), 0));

                bool shouldNewline = header.tag == 0;
                if (shouldNewline) {
                    std::array<const char, 1> newline = { '\n' };
                    HK_UNWRAP(socket->send<char>(fd, newline, 0));
                }

                stream.seek(0);
                stream.write(hk::sf::hipc::Header { .tag = 0, .dataWords = 4 });
                break;
            }
            case 2: {
                s32 fd = clientFd.load(std::memory_order_relaxed);
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
