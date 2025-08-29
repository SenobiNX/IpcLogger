#include "hk/diag/diag.h"
#include "hk/services/sm.h"
#include "hk/services/socket/address.h"
#include "hk/services/socket/config.h"
#include "hk/services/socket/service.h"
#include "hk/sf/hipc.h"
#include "hk/svc/api.h"
#include "hk/svc/types.h"
#include "hk/types.h"
#include "hk/util/Stream.h"
#include "hk/util/Tuple.h"
#include <cstdio>

constexpr hk::socket::ServiceConfig cConfig;
alignas(hk::cPageSize) u8 buffer[cConfig.calcTransferMemorySize() + 0x20000];

extern "C" void hkMain() {
    hk::sm::ServiceManager::initialize()->registerClient();

    hk::Handle port;
    // make sure to remove if it was left open
    hk::svc::ManageNamedPort(&port, "hklog", 0);
    HK_ABORT_UNLESS_R(hk::svc::ManageNamedPort(&port, "hklog", 1));

    auto* socket = hk::socket::Socket::initialize(cConfig, buffer);

    auto [err, errno] = socket->socket(hk::socket::AddressFamily::Ipv4, hk::socket::Type::Stream, hk::socket::Protocol(0));

    s32 fd = err;
    HK_ABORT_UNLESS(fd >= 0, "fd: %d, errno: %d", fd, errno);

    tie(err, errno) = socket->bind(fd, hk::socket::SocketAddrIpv4::parse<"0.0.0.0">(8008));
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    tie(err, errno) = socket->listen(fd, 1);
    HK_ABORT_UNLESS(err >= 0, "err: %d, errno: %d", err, errno);

    s32 clientFd = -1;
    hk::socket::SocketAddrIpv4 client;
    tie(clientFd, errno, client) = socket->accept(fd);
    HK_ABORT_UNLESS(clientFd >= 0, "clientFd: %d, errno: %d", clientFd, errno);

    auto logen = [&](const char* message) {
        tie(err, errno) = socket->send(clientFd, std::span<const u8>(cast<const u8*>(message), strlen(message)), 0);
    };

    logen("connected!\n");

    std::array<u8, 28> data = {};
    struct Handles {
        hk::Handle port;
        hk::Handle client = 0;
    } handles = { .port = port };
    bool sendBack = false;
    while (true) {
        u32 handleIndex = 0;
        auto result = hk::svc::ReplyAndReceive(&handleIndex, &handles.port, handles.client ? 2 : 1, sendBack ? handles.client : 0, -1);
        sendBack = false;
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
                logen("invalid handle :(");
                continue;
            }

            while (true) {
                if (clientFd < 0) {
                    tie(clientFd, errno, client) = socket->accept(fd);
                    HK_ABORT_UNLESS(clientFd >= 0, "clientFd: %d, errno: %d", clientFd, errno);
                    logen("connected!\n");
                }
                tie(err, errno) = socket->send(clientFd, std::span<const u8>(cast<const u8*>(buffer.address()), buffer.size()), 0);
                logen("\n");
                if (err < 0) {
                    socket->close(clientFd);
                    hk::svc::Break(hk::svc::BreakReason_User, nullptr, errno);
                    clientFd = -1;
                    continue;
                }
                break;
            }
            stream.seek(0);
            stream.write(hk::sf::hipc::Header { .dataWords = 4 });
            sendBack = true;
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
            char message[512];
            std::sprintf(message, "new and unique result %x\n", result.getValue());
            logen(message);
        }
        }
    }
    HK_ABORT("bye %d %d", err, errno);
}
