# Pruebas de usbmuxd

Este directorio contiene las pruebas unitarias para el proyecto usbmuxd.

## Estructura del Directorio

```
tests/
├── Makefile              # Makefile para compilar las pruebas
├── run_tests.sh          # Script para ejecutar todas las pruebas
├── test_utils.c          # Pruebas unitarias para el módulo utils
├── test_conf.c           # Pruebas unitarias para el módulo conf
└── README.md             # Este archivo
```

## Requisitos

Para compilar y ejecutar las pruebas, necesitas tener instaladas las siguientes dependencias:

- **gcc**: Compilador de C
- **libplist-2.0**: Biblioteca para manipular archivos plist
- **libusb-1.0**: Biblioteca para comunicación USB
- **make**: Herramienta de construcción

### Instalación de dependencias en Debian/Ubuntu

```bash
sudo apt-get install \
    build-essential \
    pkg-config \
    libplist-dev \
    libusb-1.0-0-dev
```

## Compilación

### Compilar todas las pruebas

```bash
cd tests
make all
```

### Compilar solo pruebas específicas

```bash
cd tests
make test-utils    # Solo pruebas de utils
make test-conf     # Solo pruebas de conf
```

## Ejecución de Pruebas

### Ejecutar todas las pruebas

```bash
cd tests
./run_tests.sh
```

O usando make:

```bash
cd tests
make test
```

### Ejecutar pruebas específicas

```bash
cd tests
./test_utils    # Solo pruebas de utils
./test_conf     # Solo pruebas de conf
```

## Pruebas Disponibles

### test_utils

Pruebas unitarias para el módulo `utils.c`:

- `fdlist_create`: Verifica la creación correcta de una lista de descriptores de archivo
- `fdlist_add`: Verifica la adición de elementos a la lista
- `fdlist_reset`: Verifica el restablecimiento de la lista
- `mstime64`: Verifica la función de obtención de tiempo en milisegundos
- `get_tick_count`: Verifica la función de obtención de tiempo

### test_conf

Pruebas unitarias para el módulo `conf.c`:

- `config_get_config_dir`: Verifica la obtención del directorio de configuración
- `config_set_config_dir`: Verifica el establecimiento del directorio de configuración
- `config_has_device_record`: Verifica la detección de registros de dispositivos
- `config_set_device_record` y `config_get_device_record`: Verifica el almacenamiento y recuperación de registros
- `config_remove_device_record`: Verifica la eliminación de registros
- `config_device_record_get_host_id`: Verifica la obtención del HostID
- `config_get_system_buid`: Verifica la generación y obtención del SystemBUID

**Nota**: Las pruebas de `conf` requieren una versión completa de libplist con todas las funciones disponibles. Si la versión instalada no tiene las funciones necesarias, estas pruebas no se compilarán.

## Limpieza

### Eliminar archivos compilados

```bash
cd tests
make clean
```

### Eliminar todos los archivos generados

```bash
cd tests
make distclean
```

## Resultados de las Pruebas

Las pruebas muestran un resumen al finalizar:

```
========================================
Resumen de pruebas:
  Total:   5
  Pasadas: 5
  Falladas: 0
========================================
```

- **Total**: Número total de pruebas ejecutadas
- **Pasadas**: Número de pruebas que pasaron exitosamente
- **Falladas**: Número de pruebas que fallaron

## Agregar Nuevas Pruebas

Para agregar nuevas pruebas:

1. Crea un nuevo archivo `test_<modulo>.c` en el directorio `tests/`
2. Agrega las pruebas usando el marco de pruebas existente
3. Actualiza el `Makefile` para incluir el nuevo archivo de prueba
4. Actualiza el script `run_tests.sh` para ejecutar las nuevas pruebas

### Ejemplo de estructura de prueba

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

#define TEST_START(name) \
    printf("  Probando: %s... ", name); \
    tests_run++;

#define TEST_PASS() \
    tests_passed++; \
    printf("OK\n");

#define TEST_FAIL(msg) \
    tests_failed++; \
    printf("FALLÓ: %s\n", msg);

#define ASSERT(condition, msg) \
    if (!(condition)) { \
        TEST_FAIL(msg); \
        return; \
    }

void test_mi_funcion(void) {
    TEST_START("mi_funcion");
    
    ASSERT(1 == 1, "La condición debe ser verdadera");
    
    TEST_PASS();
}

int main(void) {
    printf("========================================\n");
    printf("Pruebas unitarias para mi_modulo\n");
    printf("========================================\n\n");
    
    test_mi_funcion();
    
    printf("\n========================================\n");
    printf("Resumen de pruebas:\n");
    printf("  Total:   %d\n", tests_run);
    printf("  Pasadas: %d\n", tests_passed);
    printf("  Falladas: %d\n", tests_failed);
    printf("========================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
```

## Solución de Problemas

### Error: libplist-2.0 no está instalado

Instala la biblioteca libplist:

```bash
sudo apt-get install libplist-dev
```

### Error: libusb-1.0 no está instalado

Instala la biblioteca libusb:

```bash
sudo apt-get install libusb-1.0-0-dev
```

### Error: gcc no está instalado

Instala el compilador gcc:

```bash
sudo apt-get install build-essential
```

### Las pruebas de conf no se compilan

Esto puede ocurrir si la versión de libplist instalada no tiene todas las funciones necesarias. Las pruebas de utils deberían funcionar independientemente de esto.

## Contribución

Si deseas agregar más pruebas o mejorar las existentes, por favor:

1. Sigue el estilo de código existente
2. Agrega pruebas para cubrir casos edge
3. Documenta las pruebas que agregas
4. Asegúrate de que todas las pruebas pasen antes de enviar tu contribución

## Licencia

Las pruebas están bajo la misma licencia que el proyecto usbmuxd (GNU General Public License v3.0).
