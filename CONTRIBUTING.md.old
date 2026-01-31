# Guía de Contribución para usbmuxd

¡Gracias por tu interés en contribuir al proyecto usbmuxd! Esta guía te ayudará a comenzar.

## Tabla de Contenidos

- [Código de Conducta](#código-de-conducta)
- [Configuración del Entorno de Desarrollo](#configuración-del-entorno-de-desarrollo)
- [Proceso de Desarrollo](#proceso-de-desarrollo)
- [Estándares de Código](#estándares-de-código)
- [Envío de Pull Requests](#envío-de-pull-requests)
- [Reporte de Issues](#reporte-de-issues)

## Código de Conducta

Por favor, respeta el código de conducta del proyecto. Sé respetuoso, inclusivo y constructivo en todas las interacciones.

## Configuración del Entorno de Desarrollo

### Requisitos Previos

- GCC o Clang
- Make
- Autoconf
- Automake
- Libtool
- pkg-config
- libusb-1.0-dev
- libplist-dev

### Instalación en Linux (Debian/Ubuntu)

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libusb-1.0-0-dev \
    libplist-dev
```

### Instalación en macOS

```bash
brew install autoconf automake libtool pkg-config libusb libplist
```

### Configuración del Proyecto

```bash
# Clonar el repositorio
git clone https://github.com/libimobiledevice/usbmuxd.git
cd usbmuxd

# Generar el script de configuración
./autogen.sh

# Configurar el proyecto
./configure

# Compilar
make

# Ejecutar pruebas
make check
```

### Configuración de VSCode

El proyecto incluye configuraciones para VSCode en el directorio `.vscode/`:

- `launch.json`: Configuración de depuración
- `tasks.json`: Tareas de compilación
- `settings.json`: Configuración del editor
- `c_cpp_properties.json`: Propiedades de C/C++
- `extensions.json`: Extensiones recomendadas

Instala las extensiones recomendadas para una mejor experiencia de desarrollo.

## Proceso de Desarrollo

### Flujo de Trabajo

1. **Crea una rama** para tu trabajo:
   ```bash
   git checkout -b feature/tu-feature
   ```

2. **Realiza tus cambios** siguiendo los estándares de código.

3. **Formatea tu código**:
   ```bash
   clang-format -i src/*.c src/*.h
   ```

4. **Compila y prueba**:
   ```bash
   make clean
   make
   make check
   ```

5. **Commitea tus cambios**:
   ```bash
   git add .
   git commit -m "Descripción clara de tus cambios"
   ```

6. **Push a tu fork**:
   ```bash
   git push origin feature/tu-feature
   ```

7. **Crea un Pull Request** en GitHub.

## Estándares de Código

### Estilo de Código

El proyecto sigue el estilo LLVM con ajustes específicos. Usa `.clang-format` para formatear automáticamente:

```bash
clang-format -i archivo.c
```

### Reglas Generales

- Usa 4 espacios para indentación (no tabs)
- Líneas máximas de 80 caracteres
- Usa nombres descriptivos para variables y funciones
- Comenta código complejo o no obvio
- Usa `const` para variables que no cambian
- Verifica siempre los valores de retorno de funciones

### Convenciones de Nombres

- **Funciones**: `snake_case` (ej: `usbmuxd_get_device`)
- **Variables**: `snake_case` (ej: `device_count`)
- **Constantes**: `UPPER_SNAKE_CASE` (ej: `MAX_DEVICES`)
- **Tipos**: `snake_case_t` (ej: `device_info_t`)
- **Macros**: `UPPER_SNAKE_CASE` (ej: `LOG_DEBUG`)

### Documentación

Usa comentarios Doxygen para documentar funciones públicas:

```c
/**
 * @brief Obtiene información de un dispositivo conectado
 * @param handle Manejo del dispositivo
 * @param info Puntero a estructura para almacenar información
 * @return 0 en éxito, código de error en fallo
 */
int usbmuxd_get_device(usbmuxd_device_handle_t handle, device_info_t *info);
```

## Envío de Pull Requests

### Antes de Enviar

- [ ] Tu código compila sin advertencias
- [ ] Las pruebas pasan (`make check`)
- [ ] El código está formateado con `clang-format`
- [ ] Has actualizado la documentación si es necesario
- [ ] Has agregado pruebas para nuevas funcionalidades
- [ ] Tu PR tiene un título y descripción claros

### Plantilla de PR

```markdown
## Descripción
Breve descripción de los cambios realizados.

## Tipo de Cambio
- [ ] Bug fix
- [ ] Nueva funcionalidad
- [ ] Cambio de ruptura
- [ ] Documentación
- [ ] Refactorización
- [ ] Mejora de rendimiento

## Testing
Describe cómo probaste tus cambios.

## Checklist
- [ ] Código formateado
- [ ] Pruebas pasando
- [ ] Documentación actualizada
```

## Reporte de Issues

Al reportar un issue, incluye:

1. **Descripción clara** del problema
2. **Pasos para reproducir**
3. **Comportamiento esperado** vs **comportamiento actual**
4. **Información del sistema**:
   - Sistema operativo
   - Versión de usbmuxd
   - Versión de libusb
5. **Logs relevantes** si aplica

## Preguntas

¿Tienes preguntas? No dudes en abrir un issue o contactarnos en los canales de comunicación del proyecto.

---

¡Gracias por contribuir a usbmuxd!
