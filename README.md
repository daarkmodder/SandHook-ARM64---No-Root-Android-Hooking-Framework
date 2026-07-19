# SandHook ARM64 - Production Android Hooking Framework

Un framework de hooking de métodos en línea (inline hooking) y GOT/PLT de **nivel empresarial** para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 14+. 

Este proyecto es el resultado de una fusión manual y depuración exhaustiva de las técnicas más avanzadas de los motores de hooking más respetados de la industria (Dobby, ShadowHook, ByteHook, xHook, And64InlineHook), combinadas en un núcleo C++ puro, limpio y ultrarrápido, soportado por el motor de parseo ELF de xDL.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++**. Permite enlazar código C fácilmente, pero el núcleo requiere compilación con `clang++` y la librería estándar de C++. 

**Versión:** 4.8 (Production - Anti-Tamper & SELinux Bypass)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## 🔥 ¿Por qué deberías usar este Framework? (Lo que la comunidad de GitHub debe ver)

La mayoría de los motores de hooking de código abierto en GitHub tienen problemas críticos que causan crashes aleatorios o fallan en dispositivos modernos. La versión 4.8 de este framework soluciona definitivamente las fallas arquitectónicas más complejas, implementando técnicas de nivel Dios para evadir las protecciones de memoria más duras de Android 13 y 14:

1. **Bypass de Anti-Tampering (PairIP) vía Syscalls Directas:**
   Las apps modernas usan protecciones como PairIP que hookean `libc` (`mprotect`, `open`, `write`) para bloquear modificaciones de memoria. El motor evita esto completamente eliminando las llamadas a `libc` y ejecutando **Syscalls Directas al Kernel** (`SYS_mprotect`, `SYS_openat`) para manipular memoria sin ser detectado.
2. **Escritura Invisible con `/proc/self/mem` y `process_vm_writev`:**
   Si el kernel bloquea el cambio de permisos de memoria de Ejecutable (`RX`) a Escritura (`RW`) debido a SELinux Enforcing, el motor recurre a una escritura dual: usa `SYS_write` sobre `/proc/self/mem` o `SYS_process_vm_writev` para inyectar los bytes del hook directamente, sin alterar los permisos de la página.
3. **Protección de Librerías Críticas del Sistema:**
   En Android 13+, hookear funciones dentro de `libdl.so` o `linker64` corrompe la memoria compartida y causa `SIGSEGV_ACCERR`. El motor escanea `/proc/self/maps`; si el objetivo está en una librería crítica, omite el Inline Hook y cae directamente al GOT Hook seguro.
4. **Detección de Memoria No Ejecutable (Bypass de Ofuscación):**
   Algunas protecciones devuelven punteros falsos a tablas de datos (`r--`) en lugar de código (`r-x`). El motor hace un pre-flight check; si la dirección no es estrictamente ejecutable, aborta el Inline Hook limpiamente y intenta el GOT Hook, evitando crasheos instantáneos.
5. **Soporte Nativo para BTI (Branch Target Identification):**
   Los kernels de Android 13+ (AArch64) exigen a menudo el flag `PROT_BTI` (`0x10`) para la memoria ejecutable. Si no se especifica, `mprotect` falla. El framework detecta y aplica `PROT_BTI` automáticamente en todos sus fallbacks.
6. **Validación Estricta de `MAP_FIXED` (Prevención de Corrupción):**
   Como fallback final, si `mprotect` falla, se usa `MAP_FIXED`. Para evitar destruir tablas GOT/PLT o variables globales, el motor valida que la página objetivo sea estrictamente de código puro (`r-x`).
7. **Limpieza de CFI Bypass (Estado Global Limpio):**
   Al desactivar el Control Flow Integrity (`__cfi_slowpath`), el motor guarda un backup. Usa un contador atómico; cuando el último hook es desinstalado (`remove`), restaura los bytes y reactiva el CFI del proceso.
8. **Protección SIGSEGV Global (Concurrencia Segura):**
   Un manejador de señales a nivel de proceso mantiene un registro de los `sigjmp_buf` de cada hilo. Si cualquier hilo ejecuta una dirección inválida durante la instalación, el `SIGSEGV` es interceptado y el hilo se salva.
9. **Hooks Diferidos (Pending Hooks) Anti-Bloqueo:**
   Usando `sandhook_install_pending`, el motor intercepta internamente `dlopen` y `android_dlopen_ext`; cada vez que una nueva librería se carga, los hooks pendientes se aplican automáticamente usando el motor xDL, evadiendo los bloqueos de `dlsym` de PairIP.

---

## 🧠 Notas de Ingeniería (No es solo código generado)

Es importante destacar que este framework **no es un simple resultado de copiar/pegar ni un milagro de la IA**. Su desarrollo y depuración requirió un dominio profundo de varias disciplinas de bajo nivel:
* **Ensamblador ARM64 (AArch64):** Comprensión de conjuntos de instrucciones, relocalización de saltos PC-relativos (ADRP, CBZ), alineación de memoria y barreras de caché (ISB/DSB).
* **C++ de bajo nivel:** Uso de metaprogramación, `std::atomic` para concurrencia sin locks, y gestión manual de memoria (mmap/munmap) evitando la STL en el núcleo crítico.
* **C y Kernel de Linux (Bionic):** Interacción directa con syscalls, bypass de políticas SELinux, parseo de ELF en disco y manejo de señales POSIX (`sigaction`, `sigsetjmp` global).
* **Ingeniería Inversa Anti-Tamper:** Análisis de bloqueos de PairIP en `libc` y desarrollo de técnicas de evasión mediante lectura directa de `/proc/self/maps` y escritura en `/proc/self/mem`.

Cada técnica implementada fue analizada, adaptada y probada bajo estrés en dispositivos físicos reales (como Infinix/Transsion con Android 13) para garantizar su estabilidad.

---

## 🏗️ Arquitectura del Framework

1. **`sandhook.cpp` (Motor Nativo v4.8):** Se encarga del ensamblador ARM64, escritura atómica, bypass de SELinux/CFI, soporte BTI, syscalls directas al kernel, protección de librerías críticas, relocalización absoluta, trampolines, fallback a GOT Hooking y cola de Pending Hooks.
2. **`sandhook.h` (API C Pública):** El contrato que expone el motor nativo.
3. **`xdl/` (Utilidad xDL):** Motor de parseo ELF para resolución de símbolos robusta en Android 7.0+, esencial para evadir los ganchos de `dlsym`.

---

## 🛠️ Compilación e Integración

### Requisitos
- Android NDK r21 o superior.
- `clang++` ARM64 cross-compiler.

### Compilar el motor (Librería Estática)

```bash
clang++ -c -fPIC -O2 \
  -fno-exceptions -fno-rtti \
  --target=aarch64-linux-android21 \
  -I. -Isrc -Ixdl \
  src/sandhook.cpp -o sandhook.o

ar rcs libsandhook.a sandhook.o
```

### Enlazar con tu proyecto (C / C++)

```bash
clang++ tu_codigo.cpp -o tu_libreria.so -shared -fPIC \
  --target=aarch64-linux-android21 \
  -I. -Isrc -Ixdl \
  -O2 -fvisibility=hidden \
  -fno-exceptions -fno-rtti \
  -Wl,--strip-all \
  -llog -landroid -ldl \
  -static-libstdc++ \
  ./libsandhook.a
```

*(Nota: Aunque tu código sea C puro (`.c`), usa `clang++` para el paso final de enlazado).*

---

## 🚀 Uso (API Nativa C/C++) - Método Recomendado (Anti-PairIP)

Para evadir protecciones de integridad (como PairIP) que interceptan `dlopen` y `dlsym`, se recomienda encarecidamente utilizar `sandhook_install_pending`. El motor usará xDL para encontrar la dirección real en memoria ejecutable (`r-x`) sin pasar por `libc`.

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
        // Éxito
    }
}
```

### Uso Directo (Si ya tienes el puntero de la función)
```cpp
#include "sandhook.h"

void init_native_hooks() {
    // Asume que 'target_addr' ya fue resuelto por ti.
    sandhook_install_ex(target_addr, (void*)hooked_func, (void**)&orig_func);
}
```

---

## 📖 Referencia de la API C

| Función | Descripción |
|---------|-------------|
| `int sandhook_install_ex(void* target, void* replacement, void** original_out)` | Instala un hook estándar (20 bytes). Devuelve `HOOK_OK` (0) si tuvo éxito. |
| `void* sandhook_install(void* target, void* replacement, void** original_out)` | Versión simplificada. Devuelve el `target` si tuvo éxito, o `NULL`. |
| `int sandhook_install_single_insn(void* target, void* replacement, void** original_out)` | Intenta un hook de 4 bytes. Si está fuera de rango (>128MB) o se pide backup, cae al método de 20 bytes. |
| `int sandhook_remove(void* target)` | Desinstala el hook, restaura la memoria y limpia el CFI si es el último. |
| `void* sandhook_trampoline(void* target)` | Obtiene el puntero al trampolín para ejecutar la función original. |
| `int sandhook_install_pending(const char* lib_name, const char* sym_name, void* replacement, void** original_out)` | **Recomendado.** Resuelve el símbolo usando xDL (evadiendo hooks de libc) y encola el hook si la librería no se ha cargado aún. |
| `const char* sandhook_error_string(int err)` | Convierte un código de error en texto legible. |

---

## 👥 Créditos

Desarrollado, parcheado y mantenido por:

- **GML-5.2** - Ingeniería inversa, arquitectura del núcleo, relocalización absoluta (Dobby-style) y diseño de bypasses de seguridad (SELinux/PAC/CFI/MAP_FIXED).
- **DᴀʀᴋMᴏᴅᴅᴇʀ** - Implementación nativa NDK, depuración a nivel de ensamblador ARM64, integración del puente JNI (Java/C++), ingeniería inversa de PairIP y pruebas de estrés en tiempo de ejecución. *(Nota: El desarrollo de este framework requirió un dominio profundo de C++, ensamblador ARM64, internals del kernel de Android y la máquina virtual ART. No fue una simple generación de código automatizada, sino una fusión manual y depurada de técnicas de seguridad de bajo nivel).*

**Inspirado en las técnicas de:**
- [Dobby](https://github.com/jmpews/Dobby) (Inversión de saltos condicionales).
- [ShadowHook](https://github.com/bytedance/android-inline-hook) (Escritura atómica y PAC Stripping).
- [ByteHook](https://github.com/bytedance/android-inline-hook) (Bypass de CFI Slowpath).
- [xHook](https://github.com/iqiyi/xHook) (Manejo seguro de señales SIGSEGV).
- [xDL](https://github.com/hexhacking/xDL) (Resolución de símbolos ELF robusta).
- [And64InlineHook](https://github.com/Rprop/And64InlineHook) (Relocalización de saltos absolutos).

## Licencia
Uso educativo y de investigación. Prohibida su distribución comercial sin autorización.
```