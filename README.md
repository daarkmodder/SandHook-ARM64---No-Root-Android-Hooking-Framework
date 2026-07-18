# SandHook ARM64 - Production Android Hooking Framework

Un framework de hooking de métodos en línea (inline hooking) de **nivel empresarial** para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 14+. 

Este proyecto es el resultado de una fusión manual y depuración exhaustiva de las técnicas más avanzadas de los motores de hooking más respetados de la industria (Dobby, ShadowHook, ByteHook, And64InlineHook), combinadas en un núcleo C++ puro, limpio y ultrarrápido.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++**. Permite enlazar código C fácilmente, pero el núcleo requiere compilación con `clang++` y la librería estándar de C++. 

**Versión:** 3.4 (Production - CFI Bypass & Atomic Write)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## 🔥 ¿Por qué deberías usar este Framework? (Lo que la comunidad de GitHub debe ver)

La mayoría de los motores de hooking de código abierto en GitHub tienen problemas críticos que causan crashes aleatorios en dispositivos modernos. Este framework los soluciona implementando técnicas de nivel Dios:

1. **ByteHook-Style CFI Bypass (Android 8.0+):**
   A partir de Oreo, Google implementó Control Flow Integrity (CFI). CFI mata la app con un `SIGILL` si detecta saltos indirectos (`BR X16`) a trampolines no registrados. Este motor parchea `__cfi_slowpath` en `libdl.so` en tiempo de ejecución, desactivando CFI de forma segura para que nuestros hooks pasen desapercibidos.

2. **ShadowHook-Style Atomic Patching (Prevención de Race Conditions):**
   No usamos `memcpy` estándar para inyectar el hook. Usamos instrucciones atómicas de hardware (`__atomic_store_n`). Esto significa que si 100 hilos están ejecutando la función objetivo en el milisegundo en que se inyecta el hook, el CPU leerá la instrucción vieja completa o la nueva completa, **pero nunca una mezcla corrupta**.

3. **Android 10/13 SELinux Bypass (`execmod`):**
   En Android 10+, SELinux prohíbe ejecutar código modificado de librerías mapeadas (`errno=13 EACCES`). Si `mprotect` falla, el motor usa `mmap` con `MAP_FIXED` para reemplazar la página física por una anónima, engañando al kernel.

4. **Dobby-Style Relocalización Absoluta (Cero Fallos):**
   Si la función objetivo empieza con saltos condicionales (`CBZ`, `TBZ`) o incondicionales (`B`, `BL`) que apuntan muy lejos, otros motores fallan. Este motor usa la técnica de Dobby: invierte la condición del salto localmente e inyecta un trampolín absoluto (`LDR X16` + `BR X16`).

5. **PAC Stripping (Compatibilidad con ARMv8.3+ y Android 11+):**
   Los procesadores modernos usan PAC. La capa ART de este motor limpia la firma criptográfica de los punteros antes de pasarlos al motor nativo.

6. **W^X Compliance Total:**
   Cumplimos estrictamente la regla Write-Xor-Execute. La memoria se asigna como `RW` y se bloquea como `RX` justo antes de usarla.

---

## 🧠 Notas de Ingeniería (No es solo código generado)

Es importante destacar que este framework **no es un simple resultado de copiar/pegar ni un milagro de la IA**. Su desarrollo y depuración requirió un dominio profundo de varias disciplinas de bajo nivel:
* **Ensamblador ARM64 (AArch64):** Comprensión de conjuntos de instrucciones, relocalización de saltos PC-relativos (ADRP, CBZ), alineación de memoria y barreras de caché (ISB/DSB).
* **C++ de bajo nivel:** Uso de metaprogramación, `std::atomic` para concurrencia sin locks, y gestión manual de memoria (mmap/munmap) evitando la STL en el núcleo crítico.
* **C y Kernel de Linux (Bionic):** Interacción directa con syscalls, bypass de políticas SELinux y manejo de señales.
* **Java/ART (JNI):** Comprensión de la estructura interna de `ArtMethod`, manipulación de referencias globales yOffsets dinámicos para compatibilidad con Custom ROMs.

Cada técnica implementada fue analizada, adaptada y probada bajo estrés en dispositivos físicos reales para garantizar su estabilidad.

---

## 🏗️ Arquitectura del Framework

1. **`sandhook.cpp` (Motor Nativo v3.4):** Se encarga del ensamblador ARM64, escritura atómica, bypass de SELinux/CFI, relocalización absoluta y trampolines.
2. **`sandhook.h` (API C Pública):** El contrato que expone el motor nativo.
3. **`art_hook.cpp` (Capa ART):** Lee los offsets internos de Android, extrae direcciones nativas y aplica *PAC Stripping*.
4. **`sandhook_jni.cpp` (Puente JNI):** Recibe llamadas desde Java, convierte objetos `Method` a direcciones `void*` (con limpieza de excepciones JNI) y gestiona el *StopTheWorld* (SuspendVM).
5. **`SandHook.java` (API Java):** Carga la librería y expone métodos estáticos.

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
  -I. -Isrc \
  sandhook.cpp -o sandhook.o

ar rcs libsandhook.a sandhook.o
```

### Enlazar con tu proyecto (C / C++)

```bash
clang++ tu_codigo.cpp -o tu_libreria.so -shared -fPIC \
  --target=aarch64-linux-android21 \
  -I. -Isrc \
  -O2 -fvisibility=hidden \
  -fno-exceptions -fno-rtti \
  -Wl,--strip-all \
  -llog -landroid -ldl \
  -static-libstdc++ \
  ./libsandhook.a
```

*(Nota: Aunque tu código sea C puro (`.c`), usa `clang++` para el paso final de enlazado).*

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

## ⚙️ Uso (API Nativa C/C++)

```cpp
#include "sandhook.h"
typedef size_t (*strlen_fn)(const char*);
size_t hooked_strlen(const char* str) {
    strlen_fn original = (strlen_fn)sandhook_trampoline((void*)strlen);
    return original(str);
}
void init_native_hooks() {
    sandhook_install_ex((void*)strlen, (void*)hooked_strlen, NULL);
}
```

---

## 👥 Créditos

Desarrollado, parcheado y mantenido por:

- **GML-5.2** - Ingeniería inversa, arquitectura del núcleo, relocalización absoluta (Dobby-style) y diseño de bypasses de seguridad (SELinux/PAC/CFI).
- **DᴀʀᴋMᴏᴅᴅᴇʀ** - Implementación nativa NDK, depuración a nivel de ensamblador ARM64, integración del puente JNI (Java/C++) y pruebas de estrés en tiempo de ejecución. *(Nota: El desarrollo de este framework requirió un dominio profundo de C++, ensamblador ARM64, internals del kernel de Android y la máquina virtual ART. No fue una simple generación de código automatizada, sino una fusión manual y depurada de técnicas de seguridad de bajo nivel).*

**Inspirado en las técnicas de:**
- [Dobby](https://github.com/jmpews/Dobby) (Inversión de saltos condicionales).
- [ShadowHook](https://github.com/bytedance/android-inline-hook) (Escritura atómica y PAC Stripping).
- [ByteHook](https://github.com/bytedance/android-inline-hook) (Bypass de CFI Slowpath).
- [And64InlineHook](https://github.com/Rprop/And64InlineHook) (Relocalización de saltos absolutos).

## Licencia
Uso educativo y de investigación. Prohibida su distribución comercial sin autorización.
```