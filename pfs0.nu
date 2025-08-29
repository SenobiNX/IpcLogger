cmake --build build --parallel 16
mkdir build/pfsdir
cp build/main.npdm build/pfsdir
cp build/rtld.nso build/pfsdir/rtld
cp build/LightIpcLogger.nso build/pfsdir/main
run-external $"($env.SWITCHTOOLS)/build_pfs0" build/pfsdir build/LightIpcLogger.nsp
curl -T build/LightIpcLogger.nsp ftp://192.168.1.136:5000/atmosphere/contents/0200AB7430EF0001/exefs.nsp
