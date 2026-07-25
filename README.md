# SandHook ARM64 - Military Grade Android Hooking Framework

Un framework de hooking de **nivel empresarial/militar** para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 14+. Soporta tanto **Hooking Nativo (C/C++)** como **Hooking de Java (ART/Dalvik)**.

Este proyecto es el resultado de una fusión manual y depuración exhaustiva de las técnicas más avanzadas de los motores de hooking más respetados de la industria (Dobby, ShadowHook, ByteHook, xHook, And64InlineHook, LSPosed), combinadas en un núcleo C++ puro, limpio y ultrarrápido.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++ y Ensamblador (ARM64)**. Re compilación con `clang++` y la librería estándar de C++.

**Versión:** 5.8 (Military Grade: Modular Architecture + xDL .symtab Bypass + PAC Safe Fallback)  
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
    Usa un manejador de señales global (`sigsetjmp`/`siglongjmp`) que salva la app de crasheos si se lee una dirección inválida, estrictamente aislado a los rangos de memoria objetivo.

---

## 🧠 Arquitectura del Framework (Modular)

El núcleo ha sido dividido en módulos independientes para garantizar bajo acoplamiento y fácil mantenimiento:

- **`src/core/`**: El orquestador (`hook_manager.cpp`). Decide si usar Inline, GOT o RET Patch.
- **`src/arm64/`**: Decodificador y relocalizador de instrucciones ARM64.
- **`src/memory/`**: Syscalls directas, manejo de `mprotect` y el aislador de señales `SigGuard`.
- **`src/got/`**: Escaneo de tablas ELF para Hooking por GOT/PLT.
- **`src/protections/`**: Bypass de CFI y parcheo de neutralización (RET Patch).
- **`src/art/`**: Puentes y trampolines para la máquina virtual de Java (ART).
- **`src/jni/`**: Conexión directa entre la API de Java y el motor nativo.
- **`src/public/`** y **`src/internal/`**: Cabeceras de API pública y declaraciones cruzadas internas.

---

## 🛠️ Compilación

### Requisitos
- Android NDK r21 o superior (o entorno Termux con `clang++`).
- JDK instalado (`javac`) y `d8` para el lado Java.

### Script de Compilación Nativa (`build.sh`)

El proyecto incluye un script unificado que compila tanto la librería estática (para integrar en mods nativos) como la dinámica (para inyectar en Java).

```bash
# Ejecutar en la raíz del proyecto
chmod +x build.sh
./build.sh
```
Esto generará:
- `libsandhook.a`: Para linkear estáticamente en tu propio mod (ej. `libdaarkness.so`).
- `libsandhook.so`: Para inyectar en apps via MT Manager y usar con la API de Java.

### Compilación del Lado Java

```bash
# Compilar .java a .class y luego a .dex con d8
javac -source 1.8 -target 1.8 -classpath android.jar -d out/classes $(find java -name "*.java")
d8 --classpath android.jar --output out/dex $(find out/classes -name "*.class")
cd out/dex && zip -r ../sandhook-java.jar classes.dex && cd ../..
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
4. **Firma y prueba:** Guarda, firma el APK e instálalo. Si miras el Logcat verás el motor inicializándose.

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

## 👥 Créditos y Trabajo Duro

Este framework no es un simple "copiar, pegar y ya funciona". Detrás de estas líneas de código hay **innumerables horas de trabajo, estrés y depuración exhaustiva**. Se requirió ingeniería inversa profunda, pruebas de estrés en tiempo de ejecución y la resolución manual de errores del toolchain de Android (NDK/Termux) que ninguna IA puede resolver por sí sola en un solo intento. La adaptación a entornos hostiles con SELinux Enforcing y CFI activo fue un proceso iterativo donde cada fallo de segmentación (`SIGSEGV`) fue analizado y vencido a mano.

**Desarrollado, parcheado y mantenido por:**

- **GML-5.2** - Arquitectura del núcleo C++, diseño de lógica de relocalización absoluta (Dobby-style), implementación de bypasses de seguridad a bajo nivel (SELinux/PAC/CFI/MAP_FIXED) y estructura modular.
- **DᴀʀᴋMᴏᴅᴅᴇʀ** - Implementación nativa NDK, compilación cruzada en Termux, depuración a nivel de ensamblador ARM64, integración del puente JNI (Java/C++), inyección en APKs reales (MT Manager) y horas de pruebas de estrés en tiempo de ejecución resolviendo `UnsatisfiedLinkError` y bloqueos del Linker de Android.

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