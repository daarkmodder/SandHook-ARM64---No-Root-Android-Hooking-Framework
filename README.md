# SandHook ARM64 - Production Android Hooking Framework

Un framework de hooking de métodos en línea (inline hooking) de nivel producción para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 13+. 

A diferencia de los motores de hooking básicos, SandHook no solo parchea código nativo (C/C++), sino que incluye una capa de abstracción ART que permite **hookear métodos Java/Kotlin** directamente, y una API de compatibilidad estilo Xposed.

**Versión:** 3.0 (Production)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## ✨ Características Principales

- ✅ **Hook Nativo y Java/Kotlin:** Intercepta funciones C/C++ y métodos Java mediante la manipulación directa de `ArtMethod`.
- ✅ **Single Instruction Hook (Fast Hook):** Soporte para parchear atómicamente solo 4 bytes (1 instrucción) usando saltos relativos si el destino está en un rango de 128MB.
- ✅ **Cumplimiento W^X (Android 10+):** Asignación de memoria en dos pasos (RW -> RX) para evitar violaciones de SELinux en Android modernos.
- ✅ **Relocalización ARM64 Completa:** Analiza y reescribe instrucciones PC-relativas (ADRP, ADR, LDR_LIT, CBZ, TBZ) para garantizar trampolines seguros.
- ✅ **Thread Safety (StopTheWorld):** Sincronización basada en `std::recursive_mutex` para evitar deadlocks al hookear funciones anidadas.
- ✅ **Capa de Compatibilidad Xposed:** API en Java que imita a Xposed para fácil migración de módulos.

---

## 🏗️ Arquitectura del Framework

El proyecto se divide en 5 capas modularizadas para mantener la separación de responsabilidades:

1. **`sandhook.cpp` (Motor Nativo):** Se encarga del ensamblador ARM64, gestión de memoria (`mmap`/`mprotect`), relocalización de instrucciones y trampolines.
2. **`sandhook.h` (API C Pública):** El contrato que expone el motor nativo al resto del sistema.
3. **`art_hook.cpp` (Capa ART):** Lee los offsets internos de Android para extraer direcciones de memoria nativas (`entry_point_from_quick_compiled_code`) de métodos Java.
4. **`sandhook_jni.cpp` (Puente JNI):** Recibe llamadas desde Java, convierte objetos `Method` a direcciones `void*` y se las pasa al motor nativo.
5. **`SandHook.java` (API Java):** Carga la librería y expone métodos estáticos fáciles de usar para desarrolladores Android.

---

## 🛠️ Compilación e Integración

### Requisitos
- Android NDK r21 o superior.
- Android Studio con soporte CMake.

### Integración en Android Studio

1. Coloca los archivos `CMakeLists.txt` y la carpeta `src/` (con los `.cpp` y `.h`) en tu módulo de app.
2. Coloca los archivos `.java` en `app/src/main/java/com/swift/sandhook/`.
3. Configura tu `build.gradle` (app-level):

```gradle
android {
    defaultConfig {
        ndk {
            abiFilters 'arm64-v8a' // SandHook solo soporta ARM64
        }
        externalNativeBuild {
            cmake {
                cppFlags "-std=c++14 -fno-exceptions -fno-rtti"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path "src/main/cpp/CMakeLists.txt" // Ruta a tu CMakeLists
        }
    }
}
```

---

## 🚀 Uso (API Java / Kotlin)

Para hookear un método Java o Kotlin desde tu aplicación:

```java
import com.swift.sandhook.SandHook;
import java.lang.reflect.Method;

public class MyInjector {
    
    public static void applyHack() {
        // 1. Inicializar el motor (calcula offsets de ART)
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

```c
#include "sandhook.h"
#include <android/log.h>

typedef size_t (*strlen_fn)(const char*);

// Tu reemplazo
size_t hooked_strlen(const char* str) {
    strlen_fn original = (strlen_fn)sandhook_trampoline((void*)strlen);
    size_t len = original(str);
    __android_log_print(ANDROID_LOG_DEBUG, "Hook", "strlen llamado!");
    return len;
}

void init_native_hooks() {
    // Instalar hook de 20 bytes (con trampolín)
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
| `int sandhook_remove(void* target)` | Desinstala el hook y restaura la memoria original. |
| `void* sandhook_trampoline(void* target)` | Obtiene el puntero al trampolín para ejecutar la función original. |
| `const char* sandhook_error_string(int err)` | Convierte un código de error en texto legible. |

### Códigos de Error
- `0`: HOOK_OK (Éxito)
- `1`: HOOK_NULL_ARGS (Argumentos nulos)
- `2`: HOOK_ALREADY_HOOKED (Ya hookeado)
- `3`: HOOK_RELOCATION_FAILED (Falló la relocalización ARM64)
- `4`: HOOK_MPROTECT_FAILED (Falló el cambio de permisos de memoria)
- `5`: HOOK_ALLOC_FAILED (Falló la asignación de memoria)
- `9`: HOOK_OUT_OF_RANGE (Destino fuera de rango para Single Insn Hook)

---

## 🔒 Seguridad y Compatibilidad

- **SELinux:** El motor respeta las políticas W^X. No utiliza `PROT_READ | PROT_WRITE | PROT_EXEC` simultáneamente en Android 10+, evitando cierres forzosos por SELinux.
- **Concurrencia:** El uso de `std::recursive_mutex` garantiza que no haya condiciones de carrera (race conditions) al instalar o remover hooks en funciones concurrentes.
- **Limitaciones:** No se pueden hookear funciones cuya primera instrucción sea un salto incondicional (`B` o `BL`), ya que no se puede calcular un destino seguro para el trampolín.

## Licencia
Uso educativo y de investigación. Prohibida su distribución comercial sin autorización.
```