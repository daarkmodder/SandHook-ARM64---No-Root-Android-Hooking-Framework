# SandHook ARM64 - Military Grade Android Hooking Framework

Un framework de hooking de **nivel empresarial/militar** para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 14+. Soporta tanto **Hooking Nativo (C/C++)** como **Hooking de Java (ART/Dalvik)**.

Este proyecto es el resultado de una fusión manual y depuración exhaustiva de las técnicas más avanzadas de los motores de hooking más respetados de la industria (Dobby, ShadowHook, ByteHook, xHook, And64InlineHook, LSPosed), combinadas en un núcleo C++ puro, limpio y ultrarrápido.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++ y Ensamblador (ARM64)**. Re compilación con `clang++` y la librería estándar de C++.

**Versión:** 5.8 (Military Grade: xDL .symtab Bypass + PAC Safe Fallback)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## 🔥 Características Principales (v5.8)

La versión 5.8 implementa técnicas de evasión de seguridad de nivel Dios, permitiendo hookear aplicaciones en Android 13/14 con SELinux Enforcing y CFI activos sin causar crasheos:

1. **Ofuscación de Strings en Tiempo de Compilación:**
   Integración de `obfuscate.h` (AY_OBFUSCATE). Todas las cadenas de texto sensibles (`/proc/self/mem`, `libart.so`, `__cfi_slowpath`) se cifran con XOR en tiempo de compilación. Los decompiladores (Ghidra/IDA Pro) solo verán basura ilegible.
2. **Bypass de Anti-Tampering (PairIP/SELinux) vía Syscalls Directas:**
   Las protecciones modernas hookean `libc` (`mprotect`, `open`). Este motor elimina las llamadas a `libc` y ejecuta **Syscalls Directas al Kernel** (`SYS_openat`, `SYS_write`) para escribir en `/proc/self/mem` y `process_vm_writev`, evadiendo PairIP por completo.
3. **Bypass de Linker Namespace Isolation (Android 8+):**
   Las apps normales tienen prohibido hacer `dlopen("libart.so")`. El motor usa xDL para parsear los encabezados ELF directamente en memoria, evadiendo el bloqueo del Linker de Android.
4. **Bypass de Símbolos Ocultos (Android 10+ .symtab):**
   Google ocultó miles de símbolos internos de la tabla dinámica (`.dynsym`). Si xDL no encuentra `art_quick_to_interpreter_bridge` en memoria, automáticamente lee el archivo del disco y busca en la tabla estática (`.symtab`) usando `xdl_dsym`.
5. **PAC Safe Fallback (Android 13/14):**
   En ARM64 v8.3+, los punteros de `ArtMethod` están firmados criptográficamente (PAC). Si el motor detecta que la firma PAC impide calcular el offset dinámico del `entry_point`, cae automáticamente a un offset seguro (`24`) garantizando que los hooks de Java funcionen sin crashear la VM.
6. **Trampolín Ensamblador para Java (ART):**
   Un archivo `.S` (Dobby-style) actúa como intermediario. Cuando ART llama a un método hookeado, salta a nuestro ensamblador, el cual salva **TODOS** los registros de la CPU (x0-x30, q0-q7), llama a C++ de forma segura, restaura todo y salta a la función original. Cero crasheos por corrupción de registros.
7. **Motor Protection-Aware (Triple Fallback):**
   Si el motor detecta que CFI está activo y no puede ser desactivado, **prohíbe el Inline Hooking** para evitar `SIGSEGV`. En su lugar, activa un sistema de respaldo de 3 niveles:
   - Nivel 1: Intenta **GOT Hooking Mejorado** (escaneo `.rela.plt` y `.rela.dyn`).
   - Nivel 2: Si GOT falla, aplica **RET Patch (Neutralización)**.
   - Nivel 3: Si nada funciona, falla limpiamente devolviendo `HOOK_PROTECTED`.
8. **Neutralización por RET Patch (Direct Prologue Patching):**
   Como última línea de defensa, el motor puede escribir una instrucción `RET` al inicio de la función objetivo. Se puede inyectar un valor falso en `x0` (ej. hacer que `JNI_OnLoad` retorne `JNI_VERSION_1_6` sin ejecutar el anti-cheat).
9. **Hooks Diferidos (Pending Hooks) Anti-Bloqueo:**
   Usando `sandhook_install_pending`, el motor intercepta `dlopen` y `android_dlopen_ext`; cada vez que una nueva librería se carga, los hooks pendientes se aplican automáticamente.
10. **ShadowHook-Style Atomic Patching & SIGSEGV Protection:**
    Usa instrucciones atómicas de hardware (`__atomic_store_n`) para evitar race conditions y un manejador de señales global (`sigsetjmp`/`siglongjmp`) que salva la app de crasheos si se lee una dirección inválida.

---

## 🧠 Arquitectura del Framework

1. **`sandhook.cpp` (Motor Nativo v5.8):** Ensamblador ARM64, escritura atómica, bypass de SELinux/CFI, ofuscación de strings, protección SIGSEGV, relocalización absoluta, GOT Hooking Mejorado, RET Patch y Pending Hooks.
2. **`art_hook.cpp` (Capa ART Dinámica):** Usa xDL para cargar `libart.so`, bypasea la tabla `.symtab` oculta, y calcula el offset del `entry_point` de forma dinámica con fallback PAC Safe.
3. **`art_quick_stub.S` (Trampolín Ensamblador):** Salva el contexto completo de la CPU, llama al handler de C++ y salta a la función original.
4. **`sandhook_jni.cpp` (Puente JNI):** Recibe llamadas desde Java, mapea métodos, gestiona el trampolín y el *StopTheWorld*.
5. **`obfuscate.h` (Anti-Reversing):** Cifrado de strings en tiempo de compilación.
6. **`xdl/` (Utilidad xDL):** Motor de parseo ELF para resolución de símbolos robusta, evadiendo Linker Namespaces y `.dynsym` vacías.

---

## 🛠️ Compilación

### Requisitos
- Android NDK r21 o superior.
- `clang++` y `clang` ARM64 cross-compiler.
- JDK instalado (`javac`) y `d8` para el lado Java.

### Script de Compilación Nativa (C++ / ASM)

```bash
# 1. Compilar SandHook Core (C++)
clang++ -c -fPIC -O2 -fno-exceptions -fno-rtti --target=aarch64-linux-android21 -I. -Isrc -Ixdl src/sandhook.cpp -o sandhook.o

# 2. Compilar ART Hook (C++)
clang++ -c -fPIC -O2 -fno-exceptions -fno-rtti --target=aarch64-linux-android21 -I. -Isrc src/art_hook.cpp -o art_hook.o

# 3. Compilar el Trampolín Ensamblador (.S)
clang++ -c -fPIC -O2 -fno-exceptions -fno-rtti --target=aarch64-linux-android21 -I. -Isrc src/art_quick_stub.S -o art_quick_stub.o

# 4. Compilar Puente JNI (C++)
clang++ -c -fPIC -O2 -fno-exceptions -fno-rtti --target=aarch64-linux-android21 -I. -Isrc src/sandhook_jni.cpp -o sandhook_jni.o

# 5. Compilar xDL (C)
clang -c -fPIC -O2 --target=aarch64-linux-android21 -I./src/xdl ./src/xdl/xdl.c -o xdl.o
clang -c -fPIC -O2 --target=aarch64-linux-android21 -I./src/xdl ./src/xdl/xdl_iterate.c -o xdl_iterate.o
clang -c -fPIC -O2 --target=aarch64-linux-android21 -I./src/xdl ./src/xdl/xdl_linker.c -o xdl_linker.o
clang -c -fPIC -O2 --target=aarch64-linux-android21 -I./src/xdl ./src/xdl/xdl_lzma.c -o xdl_lzma.o
clang -c -fPIC -O2 --target=aarch64-linux-android21 -I./src/xdl ./src/xdl/xdl_util.c -o xdl_util.o

# 6. Linkear TODOS los .o en libsandhook.so
clang++ -shared -fPIC -O2 \
  -fno-exceptions -fno-rtti \
  --target=aarch64-linux-android21 \
  sandhook.o art_hook.o sandhook_jni.o art_quick_stub.o \
  xdl.o xdl_iterate.o xdl_linker.o xdl_lzma.o xdl_util.o \
  -static-libstdc++ -static-libgcc \
  -llog -lm -pthread \
  -Wl,--strip-all -Wl,--exclude-libs,ALL \
  -o libsandhook.so

rm -f *.o
```

### Script de Compilación Java (API Smali/Dex)

```bash
# 1. Descargar android.jar si no lo tienes (en Termux/Kali)
wget -q https://github.com/Reginer/aosp-android-jar/raw/main/android-34/android.jar -O android.jar

# 2. Compilar .java a .class
mkdir -p out/classes
javac -source 1.8 -target 1.8 -classpath android.jar -d out/classes $(find java -name "*.java")

# 3. Convertir a classes.dex con d8
mkdir -p out/dex
d8 --classpath android.jar --output out/dex $(find out/classes -name "*.class")

# 4. Empaquetar en .jar
cd out/dex
zip -r ../sandhook-java.jar classes.dex
cd ../..
rm -rf out/classes out/dex
```

---

## 🚀 Uso e Inyección (MT Manager)

1. **Inyectar `.so`:** Abre el APK objetivo en MT Manager, ve a `lib/arm64-v8a/` y pega tu `libsandhook.so`.
2. **Inyectar `.dex`:** Extrae el `classes.dex` de `sandhook-java.jar`, renómbralo a `classes2.dex` y pégalo en la raíz del APK.
3. **Punto de entrada:** Decompila el `AndroidManifest.xml`, busca la clase `Application` y ábrela en Smali. En `onCreate()`, añade:
   ```smali
   const-string v0, "sandhook"
   invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
   invoke-static {}, Lcom/swift/sandhook/SandHook;->init()V
   ```
4. **Firma y prueba:** Guarda, firma el APK e instálalo. Si miras el Logcat verás:
   ```
   I SandHook-ART: Successfully found art_quick_to_interpreter_bridge via xDL!
   W SandHook-ART: Dynamic offset scan failed. Using fallback offset 24.
   ```

---

## ⚙️ API Nativa C/C++ (Anti-PairIP)

```cpp
#include "sandhook.h"

// Ejemplo de hook nativo con cola de espera (Pending)
void init_hooks() {
    int err = sandhook_install_pending(
        "libtarget.so", 
        "Java_com_example_target", 
        (void*)hooked_func, 
        (void**)&orig_func
    );
}

// Ejemplo de bypass extremo: Neutralizar JNI_OnLoad
void bypass_jni_onload(void* jni_onload_addr) {
    // Hace que la función retorne 0x00010006 (JNI_VERSION_1_6) sin ejecutar su código
    sandhook_ret_patch(jni_onload_addr, 0x00010006, 1);
}
```

---

## 📖 Referencia de la API

| Función | Descripción |
|---------|-------------|
| `int sandhook_install_ex(...)` | Instala un hook nativo. Si CFI bloquea el inline, intenta GOT y luego RET Patch. |
| `int sandhook_install_pending(...)` | **Recomendado.** Resuelve el símbolo usando xDL y encola el hook si la librería no se ha cargado. |
| `int sandhook_ret_patch(...)` | Neutraliza una función escribiendo `RET`. Útil para bypasear `JNI_OnLoad`. |
| `int sandhook_remove(...)` | Desinstala el hook, restaura la memoria y limpia el CFI. |

---

## 👥 Créditos

Desarrollado, parcheado y mantenido por:

- **GML-5.2** - Ingeniería inversa, arquitectura del núcleo, relocalización absoluta (Dobby-style) y diseño de bypasses de seguridad (SELinux/PAC/CFI/MAP_FIXED).
- **DᴀʀᴋMᴏᴅᴅᴇʀ** - Implementación nativa NDK, depuración a nivel de ensamblador ARM64, integración del puente JNI (Java/C++), ingeniería inversa de PairIP y pruebas de estrés en tiempo de ejecución. 

**Inspirado en las técnicas de:**
- [Dobby](https://github.com/jmpews/Dobby) (Closure Bridge ASM, Inversión de saltos condicionales).
- [ShadowHook](https://github.com/bytedance/android-inline-hook) (Escritura atómica y PAC Stripping).
- [ByteHook](https://github.com/bytedance/android-inline-hook) (Bypass de CFI Slowpath).
- [xHook](https://github.com/iqiyi/xHook) (Manejo seguro de señales SIGSEGV).
- [xDL](https://github.com/hexhacking/xDL) (Resolución de símbolos ELF robusta).
- [And64InlineHook](https://github.com/Rprop/And64InlineHook) (Relocalización de saltos absolutos).
- [Obfuscate](https://github.com/adamyaxley/Obfuscate) (Cifrado de strings en tiempo de compilación).

## Licencia
Uso educativo y de investigación. Prohibida su distribución comercial sin autorización.
```