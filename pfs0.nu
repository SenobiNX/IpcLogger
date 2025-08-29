cmake --build build --parallel 16
mkdir build/pfsdir
cp build/main.npdm build/pfsdir
cp build/rtld.nso build/pfsdir/rtld
cp build/IpcLogger.nso build/pfsdir/main
run-external $"($env.SWITCHTOOLS)/build_pfs0" build/pfsdir build/IpcLogger.nsp
curl -T build/IpcLogger.nsp ftp://192.168.1.136:5000/atmosphere/contents/0200AB7430EF0001/exefs.nsp
