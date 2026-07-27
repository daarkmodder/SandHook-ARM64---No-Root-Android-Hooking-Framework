# SandHook ARM64 - Military Grade Android Hooking Framework

Un framework de hooking de **nivel empresarial/militar** para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 14+. Soporta tanto **Hooking Nativo (C/C++)** como **Hooking de Java (ART/Dalvik)**.

Este proyecto es el resultado de una fusión manual y depuración exhaustiva de las técnicas más avanzadas de los motores de hooking más respetados de la industria (Dobby, ShadowHook, ByteHook, GlossHook, xHook, And64InlineHook, LSPosed, ARMPatch), combinadas en un núcleo C++ puro, limpio y ultrarrápido.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++ y Ensamblador (ARM64)**. Recompilación con `clang++` y la librería estándar de C++.

**Versión:** 6.0 (Military Grade: Hybrid Engine - Dobby + ShadowHook + GlossHook)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## 🔥 Características Principales (v6.0)

La versión 6.0 implementa técnicas de evasión de seguridad de nivel Dios, permitiendo hookear aplicaciones en Android 13/14 con SELinux Enforcing y CFI activos sin causar crasheos:

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
6. **Direct ART Swap (Hooking de Java sin ASM Bridge):**
   En lugar de usar un puente C++ gigante (como LSPosed), el motor utiliza "Direct Swap". Obtiene el `entry_point` nativo (JIT) de tu método de reemplazo y lo inyecta directamente en el `ArtMethod` original. Se incluye un `warmUpMethod` en Java para forzar la compilación JIT antes de hookear.
7. **Motor Protection-Aware (Triple Fallback):**
   Si el motor detecta que CFI está activo y no puede ser desactivado, **prohíbe el Inline Hooking** para evitar `SIGSEGV`. En su lugar, activa un sistema de respaldo de 3 niveles:
   - Nivel 1: Intenta **GOT Hooking Mejorado** (escaneo `.rela.plt` y `.rela.dyn`).
   - Nivel 2: Si GOT falla, aplica **RET Patch (Neutralización)**.
   - Nivel 3: Si nada funciona, falla limpiamente devolviendo `HOOK_PROTECTED`.
8. **Dobby Page Shadowing (Bypass de SELinux execmod):**
   Cuando SELinux prohíbe usar `mprotect` para devolverle el permiso de ejecución a una página de memoria, el motor usa la técnica de Dobby: crea una página anónima nueva con `mmap` y `MAP_FIXED` exactamente en la misma dirección física. Como es anónima, SELinux permite darle permisos de ejecución, salvando la app de crashear.
9. **ShadowHook Atomic Patching:**
   Para evitar race conditions y crasheos intermitentes en entornos multihilo, la escritura de los parches en memoria se realiza utilizando instrucciones atómicas de hardware (`__atomic_store_n`) inspiradas en ShadowHook, garantizando que ningún hilo lea el código a medias.
10. **Salto Absoluto Limpio de 16 bytes (LDR + BR):**
    Inspirado en Dobby, el motor inyecta un salto absoluto de 16 bytes (`LDR X16, #8` + `BR X16` + `Addr`). Esto redujo el tamaño mínimo del parche de 20 a 16 bytes, permitiendo hookear funciones mucho más cortas sin fallar por límites de tamaño.
11. **Pattern Scanner y Utilidades de Bajo Nivel (ARMPatch Style):**
    Se ha añadido `sandhook_find_pattern` para escanear patrones de bytes con comodines (`??`), un cache de handles xDL para evitar fugas de memoria, y macros de conveniencia (`DECL_HOOK`, `HOOK_ADDR`, `HOOK_SYM`).
12. **Hooks Diferidos (Pending Hooks) Anti-Bloqueo:**
    Usando `sandhook_install_pending`, el motor intercepta `dlopen` y `android_dlopen_ext`; cada vez que una nueva librería se carga, los hooks pendientes se aplican automáticamente usando el motor xDL.
13. **ShadowHook-Style Atomic Patching & SIGSEGV Protection:**
    Usa un manejador de señales global (`sigsetjmp`/`siglongjmp`) que salva la app de crasheos si se lee una dirección inválida, estrictamente aislado a los rangos de memoria objetivo.

---

## 🧠 Arquitectura del Framework (Modular)

El núcleo ha sido dividido en módulos independientes para garantizar bajo acoplamiento y fácil mantenimiento:

- **`src/core/`**: El orquestador (`hook_manager.cpp`). Decide si usar Inline, GOT o RET Patch. Incluye utilidades y cache de xDL.
- **`src/arm64/`**: Decodificador y relocalizador de instrucciones ARM64 (Namespace `Inst`).
- **`src/memory/`**: Syscalls directas, manejo de `mprotect`, bypass `MAP_FIXED` y el aislador de señales `SigGuard`.
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

// 1. Ejemplo de hook nativo con Macros (ARMPatch Style)
DECL_HOOK(int, kill_hook, int pid, int sig);
HOOK_SYM(kill_hook, "libc.so", "kill");

// 2. Ejemplo de bypass extremo: Neutralizar JNI_OnLoad
void bypass_jni_onload(void* jni_onload_addr) {
    // Hace que la función retorne 0x00010006 (JNI_VERSION_1_6) sin ejecutar su código
    sandhook_ret_patch(jni_onload_addr, 0x00010006, 1);
}

// 3. Ejemplo de Pattern Scanner y NOP patch
void patch_anti_cheat() {
    uintptr_t addr = sandhook_find_pattern("libtarget.so", "AA BB ?? CC 1F");
    if (addr != 0) sandhook_write_nop((void*)addr, 5);
}
```

---

## 📖 Referencia de la API

| Función | Descripción |
|---------|-------------|
| `int sandhook_install_ex(...)` | Instala un hook nativo. Si CFI bloquea el inline, intenta GOT y luego RET Patch. |
| `int sandhook_install_pending(...)` | **Recomendado.** Resuelve el símbolo usando xDL y encola el hook si la librería no se ha cargado. |
| `int sandhook_ret_patch(...)` | Neutraliza una función escribiendo `RET`. Útil para bypasear `JNI_OnLoad`. |
| `uintptr_t sandhook_find_pattern(...)` | Escanea una librería buscando un patrón de bytes con comodines (`??`). |
| `bool sandhook_write_nop(...)` | Escribe instrucciones `NOP` (4 bytes cada una) en una dirección. |
| `bool sandhook_write_ret(...)` | Escribe una instrucción `RET` en una dirección. |
| `int sandhook_remove(...)` | Desinstala el hook, restaura la memoria y limpia el CFI. |

---

## 👥 Créditos y Trabajo Duro

Este framework no es un simple "copiar, pegar y ya funciona". Detrás de estas líneas de código hay **innumerables horas de trabajo, estrés y depuración exhaustiva**. Se requirió ingeniería inversa profunda, pruebas de estrés en tiempo de ejecución y la resolución manual de errores del toolchain de Android (NDK/Termux) que ninguna IA puede resolver por sí sola en un solo intento. La adaptación a entornos hostiles con SELinux Enforcing y CFI activo fue un proceso iterativo donde cada fallo de segmentación (`SIGSEGV`) fue analizado y vencido a mano.

**Desarrollado, parcheado y mantenido por:**

- **GML-5.2** - Arquitectura del núcleo C++, diseño de lógica de relocalización absoluta (Dobby-style), implementación de bypasses de seguridad a bajo nivel (SELinux/PAC/CFI/MAP_FIXED) y estructura modular.
- **DᴀʀᴋMᴏᴅᴅᴇʀ** - Implementación nativa NDK, compilación cruzada en Termux, depuración a nivel de ensamblador ARM64, integración del puente JNI (Java/C++), inyección en APKs reales (MT Manager) y horas de pruebas de estrés en tiempo de ejecución resolviendo `UnsatisfiedLinkError` y bloqueos del Linker de Android.

### 📝 Mensaje y aclaraciones

Sí, este proyecto ha sido desarrollado con la asistencia de modelos de Inteligencia Artificial (Grok 4.5 para análisis profundo, Kimi k2 para la estructura modular y Haiku 4.5 para la resolución de errores específicos, así como la asistencia continua del modelo GML-5.2 para la implementación principal). 

Sin embargo, la arquitectura central, la depuración de corrupciones de memoria, el bypass de CFI y la relocalización ARM64 fueron guiados, probados en dispositivos reales Android 14 y refinados con ingenio humano.

El desarrollo de herramientas de hooking a bajo nivel requiere pruebas exhaustivas y una adaptación constante a las barreras de seguridad de cada versión de Android. Agradecemos profundamente el feedback constructivo y los reportes de errores objetivos y precisos que ayudan a mejorar la estabilidad del motor. Este framework se mantiene enfocado en ofrecer una solución híbrida (Inline -> GOT -> RET) robusta y funcional, priorizando la estabilidad en entornos hostiles por encima de debates estériles sobre el origen del código.

**Inspirado en las técnicas de:**
- [Dobby](https://github.com/jmpews/Dobby) (Page Shadowing MAP_FIXED, Absolute Jump LDR+BR).
- [ShadowHook](https://github.com/bytedance/android-inline-hook) (Escritura atómica multi-hilo).
- [ByteHook](https://github.com/bytedance/android-inline-hook) (Bypass de CFI Slowpath).
- [GlossHook](https://github.com/AoThenBiceps/GlossHook) (Límite de tamaño de símbolo en el relocator).
- [xHook](https://github.com/iqiyi/xHook) (Manejo seguro de señales SIGSEGV).
- [xDL](https://github.com/hexhacking/xDL) (Resolución de símbolos ELF robusta).
- [And64InlineHook](https://github.com/Rprop/And64InlineHook) (Relocalización de saltos absolutos).
- [ARMPatch](https://github.com/Skifary/ARMPatch) (Macros de conveniencia e ideas de Pattern Scanner).
- [Obfuscate](https://github.com/adamyaxley/Obfuscate) (Cifrado de strings en tiempo de compilación).

## Licencia
Uso educativo y de investigación. Prohibida su distribución comercial sin autorización.