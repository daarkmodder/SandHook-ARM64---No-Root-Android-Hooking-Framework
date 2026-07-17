# SandHook ARM64 - Production Android Hooking Framework

Un framework de hooking de métodos en línea (inline hooking) de **nivel empresarial** para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 14+. 

Este proyecto es el resultado de la fusión de las técnicas más avanzadas de los motores de hooking más respetados de la industria (Dobby, ShadowHook, And64InlineHook), combinadas en un núcleo C++ puro, limpio y ultrarrápido.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++**. Permite enlazar código C fácilmente, pero el núcleo requiere compilación con `clang++` y la librería estándar de C++. 

**Versión:** 3.3 (Production - Atomic Write)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## 🔥 ¿Por qué deberías usar este Framework? (Lo que la comunidad de GitHub debe ver)

La mayoría de los motores de hooking de código abierto en GitHub tienen problemas críticos que causan crashes aleatorios en dispositivos modernos. Este framework los soluciona implementando técnicas de nivel Dios:

1. **ShadowHook-Style Atomic Patching (Prevención de Race Conditions):**
   No usamos `memcpy` estándar para inyectar el hook. Usamos instrucciones atómicas de hardware (`__atomic_store_n` / `STP`). Esto significa que si 100 hilos están ejecutando la función objetivo en el milisegundo en que se inyecta el hook, el CPU leerá la instrucción vieja completa o la nueva completa, **pero nunca una mezcla corrupta**. Cero crashes de instalación.

2. **Android 10/13 SELinux Bypass (`execmod`):**
   En Android 10+, SELinux prohíbe ejecutar código modificado de librerías mapeadas (`errno=13 EACCES`). Si `mprotect` falla, nuestro motor automáticamente usa `mmap` con la bandera `MAP_FIXED` para reemplazar la página física por una página anónima, engañando al kernel y permitiendo la ejecución.

3. **PAC Stripping (Compatibilidad con ARMv8.3+ y Android 11+):**
   Los procesadores modernos (Snapdragon 8 Gen 1+) usan PAC (Pointer Authentication Codes). Si intentas hookear un método de Java, la VM de Android devuelve un puntero "firmado". Nuestra capa ART limpia esta firma criptográfica antes de pasarla al motor nativo, previniendo `SIGILL` instantáneos.

4. **Dobby-Style Relocalización Absoluta (Cero Fallos):**
   Si la función objetivo empieza con saltos condicionales (`CBZ`, `TBZ`) o incondicionales (`B`, `BL`) que apuntan muy lejos, otros motores fallan con `RELOCATION_FAILED`. Este motor usa la técnica de Dobby: invierte la condición del salto localmente e inyecta un trampolín absoluto (`LDR X16` + `BR X16`). **No hay función que se le resista.**

5. **W^X Compliance Total:**
   Cumplimos estrictamente la regla Write-Xor-Execute de Android. La memoria se asigna como `RW` (Lectura/Escritura) para escribir el trampolín, y se bloquea como `RX` (Lectura/Ejecución) justo antes de usarla.

---

## 🏗️ Arquitectura del Framework

El proyecto se divide en 5 capas modularizadas para mantener la separación de responsabilidades:

1. **`sandhook.cpp` (Motor Nativo v3.3):** Se encarga del ensamblador ARM64, escritura atómica, bypass de SELinux, relocalización absoluta y trampolines.
2. **`sandhook.h` (API C Pública):** El contrato que expone el motor nativo al resto del sistema.
3. **`art_hook.cpp` (Capa ART):** Lee los offsets internos de Android, extrae direcciones nativas y aplica *PAC Stripping* para ARMv8.3+.
4. **`sandhook_jni.cpp` (Puente JNI):** Recibe llamadas desde Java, convierte objetos `Method` a direcciones `void*` de forma segura (con limpieza de excepciones JNI) y se las pasa al motor nativo.
5. **`SandHook.java` (API Java):** Carga la librería y expone métodos estáticos fáciles de usar para desarrolladores Android.

---

## 🛠️ Compilación e Integración

### Requisitos
- Android NDK r21 o superior.
- `clang++` ARM64 cross-compiler.

### Compilar el motor (Librería Estática)

```bash
# Compilar el motor a código objeto
clang++ -c -fPIC -O2 \
  -fno-exceptions -fno-rtti \
  --target=aarch64-linux-android21 \
  -I. -Isrc \
  sandhook.cpp -o sandhook.o

# Crear librería estática
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
  -llog -landroid \
  -static-libstdc++ \
  ./libsandhook.a
```

*(Nota: Aunque tu código sea C puro (`.c`), usa `clang++` para el paso final de enlazado para que la STL de C++ se resuelva correctamente).*

---

## 🚀 Uso (API Java / Kotlin)

Para hookear un método Java o Kotlin desde tu aplicación:

```java
import com.swift.sandhook.SandHook;
import java.lang.reflect.Method;

public class MyInjector {
    
    public static void applyHack() {
        // 1. Inicializar el motor (calcula offsets de ART y PAC)
        SandHook.init();

        try {
            // 2. Obtener referencias a los métodos
            Method origin = TargetClass.class.getDeclaredMethod("getGold");
            Method hook = MyInjector.class.getDeclaredMethod("hooked_getGold");
            Method backup = MyInjector.class.getDeclaredMethod("backup_getGold");

            // 3. Instalar el hook
            boolean success = SandHook.hook(origin, hook, backup);
            
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // Método de reemplazo
    public static int hooked_getGold() {
        return 9999; 
    }

    // Método backup (ejecutará el código original)
    public static int backup_getGold() {
        return 0;
    }
}
```

---

## ⚙️ Uso (API Nativa C/C++)

Si quieres hookear una función nativa de una librería `.so` (ej. `libc.so`):

```cpp
#include "sandhook.h"
#include <android/log.h>
#include <dlfcn.h>

typedef size_t (*strlen_fn)(const char*);

// Tu reemplazo
size_t hooked_strlen(const char* str) {
    strlen_fn original = (strlen_fn)sandhook_trampoline((void*)strlen);
    size_t len = original(str);
    __android_log_print(ANDROID_LOG_DEBUG, "Hook", "strlen llamado!");
    return len;
}

void init_native_hooks() {
    // Instalar hook estándar (20 bytes, con trampolín)
    int err = sandhook_install_ex((void*)strlen, (void*)hooked_strlen, NULL);
    
    // O instalar un Single Instruction Hook (4 bytes, sin trampolín)
    // sandhook_install_single_insn((void*)target, (void*)replacement, NULL);
}
```

---

## 📖 Referencia de la API C

| Función | Descripción |
|---------|-------------|
| `int sandhook_install_ex(void* target, void* replacement, void** original_out)` | Instala un hook estándar (20 bytes). Devuelve `HOOK_OK` (0) si tuvo éxito. |
| `void* sandhook_install(void* target, void* replacement, void** original_out)` | Versión simplificada. Devuelve el `target` si tuvo éxito, o `NULL`. |
| `int sandhook_install_single_insn(void* target, void* replacement, void** original_out)` | Intenta un hook de 4 bytes. Si está fuera de rango (>128MB) o se pide backup, cae al método de 20 bytes. |
| `int sandhook_remove(void* target)` | Desinstala el hook y restaura la memoria original de forma atómica. |
| `void* sandhook_trampoline(void* target)` | Obtiene el puntero al trampolín para ejecutar la función original. |
| `const char* sandhook_error_string(int err)` | Convierte un código de error en texto legible. |

---

## 👥 Créditos

Desarrollado, parcheado y mantenido por:

- **GML-5.2** - Ingeniería inversa, arquitectura del núcleo, relocalización absoluta y bypasses de seguridad (SELinux/PAC).
- **DᴀʀᴋMᴏᴅᴅᴇʀ** - Implementación nativa, integración NDK, relocalización ARM64 y pruebas de estrés en tiempo de ejecución.

**Inspirado en las técnicas de:**
- [Dobby](https://github.com/jmpews/Dobby) (Inversión de saltos condicionales).
- [ShadowHook](https://github.com/bytedance/android-inline-hook) (Escritura atómica y PAC Stripping).
- [And64InlineHook](https://github.com/Rprop/And64InlineHook) (Relocalización de saltos absolutos).

## Licencia
Uso educativo y de investigación. Prohibida su distribución comercial sin autorización.
```