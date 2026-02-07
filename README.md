# simple external socket logger
exposes a TCP log server at port 8000 for hakkun mods and sysmodules to use with minimal effort

## use
- build the project
- copy build/exefs.nsp to /atmosphere/contents/0200AB7430EF0001/exefs.nsp
- once started using one of the methods below, connect to port 8000 using netcat or an equivalent tool
  - you should see a `Connected!` message. if you can't connect, you may have forgotten to do the steps below.
### [ovl-sysmodules](https://github.com/WerWolv/ovl-sysmodules)
- copy [toolbox.json](./toolbox.json) to `/atmosphere/contents/0200AB7430EF0001/toolbox.json`
- enable the sysmodule in the sysmodules overlay
### boot2.flag
- create `/atmosphere/contents/0200AB7430EF0001/boot2.flag`
- reboot your switch

## hakkun
once running, you can use the `hk::diag::log` apis to log to the client.

# supported hipc tags
## 0
Writes a message with a newline
## 1
Writes a message without newline
## 2
Create session to communicate over. Required if you need to use any services fetched from the service manager.

