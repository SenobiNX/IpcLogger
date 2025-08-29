# simple external socket logger
exposes a TCP log server at port 8000 for hakkun mods and sysmodules to use with minimal effort

# use
- build the project
- copy build/exefs.nsp to /atmosphere/contents/0200AB7430EF0001/toolbox.json
### [ovl-sysmodules](https://github.com/WerWolv/ovl-sysmodules)
- copy [toolbox.json](./toolbox.json) to /atmosphere/contents/0200AB7430EF0001/toolbox.json
- enable the sysmodule in the sysmodules overlay
### boot2.flag
- create /atmosphere/contents/0200AB7430EF0001/boot2.flag 
- reboot your switch

## hakkun
add this to your project
```cpp
#include "hk/sf/sf.h"
#include "hk/svc/api.h"
#include "hk/util/Stream.h"

void hk::diag::hkLogSink(const char* msg, size len) {
    static hk::Handle lightHandle = 0;
    if (!lightHandle) {
        auto res = svc::ConnectToNamedPort(&lightHandle, "hklog");
        if (res.failed())
            svc::Break(svc::BreakReason_User, nullptr, res.getValue());
    };

    hk::util::Stream stream(hk::svc::getTLS()->ipcMessageBuffer, hk::sf::cTlsBufferSize);
    stream.write(hk::sf::hipc::Header { .tag = 0, .sendBufferCount = 1, .dataWords = 8 });
    stream.write(hk::sf::hipc::Buffer(sf::hipc::BufferMode::Normal, u64(msg), len));
    auto res = svc::SendSyncRequest(lightHandle);
    if (res.failed())
        svc::Break(svc::BreakReason_User, nullptr, res.getValue());
}
```

# supported hipc tags
- 0 - Log with newline
- 1 - Log without newline
