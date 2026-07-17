```markdown
# SandHook ARM64 - Production Android Hooking Framework

Un framework de hooking de métodos en línea (inline hooking) de nivel producción para Android ARM64. Funciona sin root y es compatible con Android 5.0 hasta Android 13+. 

Este framework utiliza un motor nativo en C++ altamente optimizado, capaz de bypassear las restricciones de seguridad más estrictas de Android moderno (SELinux `execmod`, W^X) y relocalizar cualquier tipo de prólogo de función, igualando e incluso superando a motores como Dobby o And64InlineHook.

> ⚠️ **Aviso de Compatibilidad:** 
> Este framework está escrito y optimizado estrictamente para **C++**. Permite enlazar código C fácilmente, pero el núcleo requiere compilación con `clang++` y la librería estándar de C++. Existe la posibilidad de que en el futuro se porte a C puro, pero por ahora no está garantizado.

**Versión:** 3.1 (Production)  
**Arquitectura:** Android ARM64 (aarch64) únicamente  
**Requisitos:** Android 5.0+ (API 21+), No se requiere Root.

---

## ✨ Características Principales

- ✅ **Hook Nativo:** Intercepta funciones C/C++ mediante parcheo de memoria directo y trampolines dinámicos.
- ✅ **Relocalización Absoluta (Nivel Dobby):** Soporte completo para funciones que comienzan con saltos incondicionales (`B` / `BL`). El motor calcula la dirección absoluta y genera un salto seguro (`LDR X16` + `BR X16`), permitiendo hookear funciones que otros motores rechazan.
- ✅ **Single Instruction Hook (Fast Hook):** Soporte para parchear atómicamente solo 4 bytes (1 instrucción) usando saltos relativos si el destino está en un rango de 128MB. Si se solicita un backup, cae inteligentemente al método de 20 bytes.
- ✅ **Bypass Android 10/13 (`execmod`):** Implementa un fallback con `mmap` anónimo (`MAP_FIXED`) para bypassar las restricciones de SELinux que impiden ejecutar código modificado de librerías mapeadas en memoria.
- ✅ **Cumplimiento W^X:** Asignación de memoria en dos pasos (RW para escritura, RX para ejecución) para evitar violaciones de SELinux en Android 10+.
- ✅ **Relocalización ARM64 Completa:** Analiza y reescribe instrucciones PC-relativas (`ADRP`, `ADR`, `LDR_LIT`, `CBZ`, `CBNZ`, `TBZ`, `TBNZ`) garantizando trampolines seguros.
- ✅ **Thread Safety (StopTheWorld):** Sincronización basada en `std::recursive_mutex` para evitar deadlocks al hookear funciones anidadas o desde múltiples hilos.

---

## 🛠️ Compilación

### Requisitos
- Android NDK r21 o superior.
- `clang++` ARM64 cross-compiler.

### Compilar el motor (Librería Estática)

Para compilar el motor `sandhook.cpp` en una librería estática (`libsandhook.a`):

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

Si tienes un proyecto en C o C++ y quieres usar SandHook, enlázalo así:

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

## 🚀 Uso (API Nativa C/C++)

Para hookear una función nativa de una librería `.so` (ej. `libc.so`):

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
| `int sandhook_remove(void* target)` | Desinstala el hook y restaura la memoria original. |
| `void* sandhook_trampoline(void* target)` | Obtiene el puntero al trampolín para ejecutar la función original. |
| `const char* sandhook_error_string(int err)` | Convierte un código de error en texto legible. |

---

## 🔒 Seguridad y Compatibilidad

- **SELinux Bypass (`execmod`):** El motor detecta automáticamente cuando el sistema bloquea la ejecución de código modificado (`errno=13 EACCES`) y utiliza `mmap` con `MAP_FIXED` para reemplazar la página por una anónima, engañando al kernel y permitiendo la ejecución en Android 10, 11, 12 y 13.
- **Relocalización de Saltos Largos:** A diferencia de motores básicos, SandHook no falla (`RELOCATION_FAILED`) si la función objetivo empieza con un salto incondicional (`B`/`BL`). Convierte el salto relativo en un salto absoluto en el trampolín, garantizando la ejecución del código original.
- **Concurrencia:** El uso de `std::recursive_mutex` garantiza que no haya condiciones de carrera (race conditions) al instalar o remover hooks en funciones concurrentes.

---

## 👥 Créditos

Desarrollado, parcheado y mantenido por:

- **GML-5.2** - Ingeniería inversa, arquitectura del núcleo, relocalización absoluta y bypasses de seguridad.
- **DᴀʀᴋMᴏᴅᴅᴇʀ** - Implementación nativa, integración NDK, relocalización ARM64 y pruebas de estrés en tiempo de ejecución.

Basado en los conceptos originales del ecosistema SandHook, And64InlineHook (Rprop) y Dobby.

## Licencia
Uso educativo y de investigación. Prohibida su distribución comercial sin autorización.
```