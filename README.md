# Simple external socket logger

```cpp
void hk::diag::hkLogSink(const char* msg, size len) {
    static hk::Handle lightHandle = 0;
    if (!lightHandle) {
        auto res = svc::ConnectToNamedPort(&lightHandle, "hklog");
        if (res.failed())
            svc::Break(svc::BreakReason_User, nullptr, res.getValue());
    };

    hk::util::Stream stream(hk::svc::getTLS()->ipcMessageBuffer, hk::sf::cTlsBufferSize);
    stream.write(hk::sf::hipc::Header { .tag = 15, .sendBufferCount = 1, .dataWords = 8 });
    stream.write(hk::sf::hipc::Buffer(sf::hipc::BufferMode::Normal, u64(msg), len));
    auto res = svc::SendSyncRequest(lightHandle);
    if (res.failed())
        svc::Break(svc::BreakReason_User, nullptr, res.getValue());
}
```
