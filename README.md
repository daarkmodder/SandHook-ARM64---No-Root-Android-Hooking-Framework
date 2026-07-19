# SandHook ARM64 - Military Grade Android Hooking Framework

Un framework de hooking de **nivel empresarial/militar** para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 14+. Soporta tanto **Hooking Nativo (C/C++)** como **Hooking de Java (ART/Dalvik)**.

Este proyecto es el resultado de una fusión manual y depuración exhaustiva de las técnicas más avanzadas de los motores de hooking más respetados de la industria (Dobby, ShadowHook, ByteHook, xHook, And64InlineHook, LSPosed), combinadas en un núcleo C++ puro, limpio y ultrarrápido.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++ y Ensamblador (ARM64)**. Re compilación con `clang++` y la librería estándar de C++.

**Versión:** 5.6 (Military Grade: Obfuscated Strings + Dobby ASM Bridge + SELinux Bypass)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## 🔥 Características Principales (v5.6)

La versión 5.6 implementa técnicas de evasión de seguridad de nivel Dios, permitiendo hookear aplicaciones en Android 13/14 con SELinux Enforcing y CFI activos sin causar crasheos:

1. **Ofuscación de Strings en Tiempo de Compilación:**
   Integración de `obfuscate.h` (AY_OBFUSCATE). Todas las cadenas de texto sensibles (`/proc/self/mem`, `libart.so`, `__cfi_slowpath`) se cifran con XOR en tiempo de compilación. Los decompiladores (Ghidra/IDA Pro) solo verán basura ilegible.
2. **Bypass de Anti-Tampering (PairIP/SELinux) vía Syscalls Directas:**
   Las protecciones modernas hookean `libc` (`mprotect`, `open`). Este motor elimina las llamadas a `libc` y ejecuta **Syscalls Directas al Kernel** (`SYS_openat`, `SYS_write`) para escribir en `/proc/self/mem` y `process_vm_writev`, evadiendo PairIP por completo.
3. **Trampolín Ensamblador para Java (ART):**
   Un archivo `.S` (Dobby-style) actúa como intermediario. Cuando ART llama a un método hookeado, salta a nuestro ensamblador, el cual salva **TODOS** los registros de la CPU (x0-x30, q0-q7), llama a C++ de forma segura, restaura todo y salta a la función original. Cero crasheos por corrupción de registros.
4. **Cálculo Dinámico de Offsets (ArtMethod):**
   Olvídate de los offsets hardcodeados que se rompen en cada versión de Android. El motor carga `libart.so`, busca `art_quick_to_interpreter_bridge` y escanea la memoria en tiempo de ejecución para encontrar el offset exacto del `entry_point` para Android 9, 10, 11, 12, 13 y 14.
5. **Motor Protection-Aware (CFI/SELinux Safe Fallback):**
   Si el motor detecta que CFI (Control Flow Integrity) está activo y no puede ser desactivado, **prohíbe el Inline Hooking** para evitar `SIGSEGV` (Signal 11). En su lugar, cae automáticamente al **GOT Hooking** (que es seguro contra CFI) o falla limpiamente devolviendo `HOOK_PROTECTED`.
6. **Bypass de CFI Slowpath:**
   Parchea `__cfi_slowpath` buscando en `libc.so`, `libdl.so` y `linker64` usando el motor xDL, escribiendo la instrucción `RET` directamente mediante `/proc/self/mem` para evadir el bloqueo `execmod` de SELinux.
7. **Bypass de PAC (Pointer Authentication Codes):**
   Limpia las firmas criptográficas de los punteros en ARM64 v8.3+ (Android 13+) usando máscaras de 48 bits para evitar crasheos silenciosos al leer tablas de métodos.
8. **Hooks Diferidos (Pending Hooks) Anti-Bloqueo:**
   Usando `sandhook_install_pending`, el motor intercepta `dlopen` y `android_dlopen_ext`; cada vez que una nueva librería se carga, los hooks pendientes se aplican automáticamente usando el motor xDL.
9. **ShadowHook-Style Atomic Patching & SIGSEGV Protection:**
   Usa instrucciones atómicas de hardware (`__atomic_store_n`) para evitar race conditions y un manejador de señales global (`sigsetjmp`/`siglongjmp`) que salva la app de crasheos si se lee una dirección inválida durante la relocalización.

---

## 🧠 Arquitectura del Framework

1. **`sandhook.cpp` (Motor Nativo v5.6):** Se encarga del ensamblador ARM64, escritura atómica, bypass de SELinux/CFI, ofuscación de strings, protección de señales SIGSEGV, relocalización absoluta, trampolines, fallback a GOT Hooking y cola de Pending Hooks.
2. **`art_hook.cpp` (Capa ART Dinámica):** Resuelve los offsets internos de `ArtMethod` dinámicamente escaneando la memoria de `libart.so`.
3. **`art_quick_stub.S` (Trampolín Ensamblador):** Salva el contexto completo de la CPU, llama al handler de C++ y salta a la función original sin romper la convención de llamadas de ART.
4. **`sandhook_jni.cpp` (Puente JNI):** Recibe llamadas desde Java, mapea los métodos a hookear, gestiona el trampolín y el *StopTheWorld* (SuspendVM).
5. **`obfuscate.h` (Anti-Reversing):** Cifrado de strings en tiempo de compilación.
6. **`xdl/` (Utilidad xDL):** Motor de parseo ELF para resolución de símbolos robusta en Android 7.0+, evadiendo los ganchos de `dlsym`.

---

## 🛠️ Compilación

### Requisitos
- Android NDK r21 o superior.
- `clang++` y `clang` ARM64 cross-compiler.

### Script de Compilación Manual

```bash
# 1. Compilar SandHook Core (C++)
clang++ -c -fPIC -O2 \
  -fno-exceptions -fno-rtti \
  --target=aarch64-linux-android21 \
  -I. -Isrc -Ixdl \
  src/sandhook.cpp -o sandhook.o

# 2. Compilar ART Hook (C++)
clang++ -c -fPIC -O2 \
  -fno-exceptions -fno-rtti \
  --target=aarch64-linux-android21 \
  -I. -Isrc \
  src/art_hook.cpp -o art_hook.o

# 3. Compilar el Trampolín Ensamblador (.S)
clang++ -c -fPIC -O2 \
  -fno-exceptions -fno-rtti \
  --target=aarch64-linux-android21 \
  -I. -Isrc \
  src/art_quick_stub.S -o art_quick_stub.o

# 4. Compilar Puente JNI (C++)
clang++ -c -fPIC -O2 \
  -fno-exceptions -fno-rtti \
  --target=aarch64-linux-android21 \
  -I. -Isrc \
  src/sandhook_jni.cpp -o sandhook_jni.o

# 5. Compilar xDL (C)
clang -c -fPIC -O2 \
  --target=aarch64-linux-android21 \
  -I./src/xdl \
  ./src/xdl/xdl.c -o xdl.o

clang -c -fPIC -O2 \
  --target=aarch64-linux-android21 \
  -I./src/xdl \
  ./src/xdl/xdl_iterate.c -o xdl_iterate.o

clang -c -fPIC -O2 \
  --target=aarch64-linux-android21 \
  -I./src/xdl \
  ./src/xdl/xdl_linker.c -o xdl_linker.o

clang -c -fPIC -O2 \
  --target=aarch64-linux-android21 \
  -I./src/xdl \
  ./src/xdl/xdl_lzma.c -o xdl_lzma.o

clang -c -fPIC -O2 \
  --target=aarch64-linux-android21 \
  -I./src/xdl \
  ./src/xdl/xdl_util.c -o xdl_util.o

# 6. Empaquetar todo en libsandhook.a
ar rcs libsandhook.a \
  sandhook.o \
  art_hook.o \
  art_quick_stub.o \
  sandhook_jni.o \
  xdl.o \
  xdl_iterate.o \
  xdl_linker.o \
  xdl_lzma.o \
  xdl_util.o

echo "Compilación exitosa. libsandhook.a creado."
```

---

## 🚀 Uso (API Java / Kotlin)

```java
import com.swift.sandhook.SandHook;
import java.lang.reflect.Method;

public class MyInjector {
    public static void applyHack() {
        SandHook.init();
        try {
            Method origin = TargetClass.class.getDeclaredMethod("getGold");
            Method hook = MyInjector.class.getDeclaredMethod("hooked_getGold");
            Method backup = MyInjector.class.getDeclaredMethod("backup_getGold");
            SandHook.hook(origin, hook, backup);
        } catch (Exception e) { e.printStackTrace(); }
    }
    public static int hooked_getGold() { return 9999; }
    public static int backup_getGold() { return 0; }
}
```

---

## ⚙️ Uso (API Nativa C/C++ - Anti-PairIP)

Para evadir protecciones de integridad (como PairIP) que interceptan `dlopen` y `dlsym`, se recomienda encarecidamente utilizar `sandhook_install_pending`.

```cpp
#include "sandhook.h"

typedef int (*TargetFunc)(int);
static TargetFunc orig_func = nullptr;

int hooked_func(int arg) {
    // Tu lógica aquí
    return orig_func(arg);
}

void init_hooks() {
    // Si la librería no está cargada, se encolará.
    // Si ya lo está, se hookeará al instante leyendo los encabezados ELF nativos.
    int err = sandhook_install_pending(
        "libtarget.so", 
        "Java_com_example_target", 
        (void*)hooked_func, 
        (void**)&orig_func
    );
    
    if (err == HOOK_OK) {
        // Éxito inmediato
    } else if (err == HOOK_PENDING) {
        // En cola esperando a que la app cargue libtarget.so
    }
}
```

---

## 📖 Referencia de la API C

| Función | Descripción |
|---------|-------------|
| `int sandhook_install_ex(void* target, void* replacement, void** original_out)` | Instala un hook nativo estándar (20 bytes). Devuelve `HOOK_OK` (0) si tuvo éxito. |
| `int sandhook_install_pending(const char* lib_name, const char* sym_name, void* replacement, void** original_out)` | **Recomendado.** Resuelve el símbolo usando xDL (evadiendo hooks de libc) y encola el hook si la librería no se ha cargado aún. |
| `int sandhook_install_single_insn(void* target, void* replacement, void** original_out)` | Intenta un hook de 4 bytes. Si está fuera de rango (>128MB) o se pide backup, cae al método de 20 bytes. |
| `int sandhook_remove(void* target)` | Desinstala el hook, restaura la memoria y limpia el CFI si es el último. |
| `const char* sandhook_error_string(int err)` | Convierte un código de error en texto legible. |

### Códigos de Error
* `HOOK_OK (0)`: Éxito.
* `HOOK_PENDING (11)`: La librería no se ha cargado, el hook está en cola.
* `HOOK_PROTECTED (12)`: La función está protegida por CFI/SELinux e impide el Inline Hook.
* `HOOK_MPROTECT_FAILED (4)`: Fallo de permisos de memoria (SELinux bloqueando).

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