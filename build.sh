#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

TARGET="aarch64-linux-android21"
COMMON_FLAGS="-fPIC -O2 -fno-exceptions -fno-rtti"
INCLUDES="-Isrc/internal -Isrc/public -Isrc/xdl -Isrc -Isrc/art"

echo -e "${CYAN}🛠 SandHook v5.8 Modular Build Script${NC}"
echo "1. Compilar Todo (libsandhook.a + libsandhook.so)"
echo "2. Limpiar archivos temporales (.o)"
read -p "Selecciona una opción (1/2): " option

if [ "$option" == "1" ]; then
    echo -e "${CYAN}🚀 Compilando SandHook v5.8...${NC}"

    echo "Compilando xDL..."
    clang -c -fPIC -O2 --target=$TARGET -Isrc/xdl src/xdl/xdl.c -o xdl.o
    clang -c -fPIC -O2 --target=$TARGET -Isrc/xdl src/xdl/xdl_iterate.c -o xdl_iterate.o
    clang -c -fPIC -O2 --target=$TARGET -Isrc/xdl src/xdl/xdl_linker.c -o xdl_linker.o
    clang -c -fPIC -O2 --target=$TARGET -Isrc/xdl src/xdl/xdl_lzma.c -o xdl_lzma.o
    clang -c -fPIC -O2 --target=$TARGET -Isrc/xdl src/xdl/xdl_util.c -o xdl_util.o

    echo "Compilando SandHook Core..."
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/memory/mem.cpp -o mem.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/memory/sig_guard.cpp -o sig_guard.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/arm64/relocator.cpp -o relocator.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/got/got_hook.cpp -o got_hook.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/protections/cfi_bypass.cpp -o cfi_bypass.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/protections/ret_patch.cpp -o ret_patch.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/core/hook_manager.cpp -o hook_manager.o

    echo "Empaquetando libsandhook.a..."
    ar rcs libsandhook.a \
      mem.o sig_guard.o relocator.o got_hook.o cfi_bypass.o ret_patch.o hook_manager.o \
      xdl.o xdl_iterate.o xdl_linker.o xdl_lzma.o xdl_util.o

    echo "Compilando Módulos ART y JNI..."
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/art/art_hook.cpp -o art_hook.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/art/art_quick_stub.S -o art_quick_stub.o
    clang++ -c $COMMON_FLAGS --target=$TARGET $INCLUDES src/jni/sandhook_jni.cpp -o sandhook_jni.o

    echo "Linkeando libsandhook.so..."
    clang++ -shared -fPIC -O2 \
      -fno-exceptions -fno-rtti \
      --target=$TARGET \
      mem.o sig_guard.o relocator.o got_hook.o cfi_bypass.o ret_patch.o hook_manager.o \
      art_hook.o sandhook_jni.o art_quick_stub.o \
      xdl.o xdl_iterate.o xdl_linker.o xdl_lzma.o xdl_util.o \
      -static-libstdc++ \
      -static-libgcc \
      -llog -lm -pthread \
      -Wl,--strip-all \
      -Wl,--exclude-libs,ALL \
      -o libsandhook.so

    rm -f *.o
    echo -e "${GREEN}✅ ¡Compilación exitosa!${NC}"
    echo "   - libsandhook.a (Motor estático)"
    echo "   - libsandhook.so (Motor dinámico para Java)"

elif [ "$option" == "2" ]; then
    echo -e "${RED}🧹 Limpiando...${NC}"
    rm -f *.o
    rm -f libsandhook.a
    rm -f libsandhook.so
    echo -e "${GREEN}✅ Limpieza completada.${NC}"

else
    echo -e "${RED}❌ Opción no válida.${NC}"
fi