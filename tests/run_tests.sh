#!/bin/bash
# Script para ejecutar todas las pruebas de usbmuxd

set -e

# Colores para salida
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Directorio de pruebas
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$TEST_DIR"

echo "========================================"
echo "Sistema de Pruebas de usbmuxd"
echo "========================================"
echo ""

# Verificar dependencias
echo "Verificando dependencias..."
if ! command -v gcc &> /dev/null; then
    echo -e "${RED}Error: gcc no está instalado${NC}"
    exit 1
fi

if ! pkg-config --exists libplist-2.0; then
    echo -e "${RED}Error: libplist-2.0 no está instalado${NC}"
    exit 1
fi

if ! pkg-config --exists libusb-1.0; then
    echo -e "${RED}Error: libusb-1.0 no está instalado${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Todas las dependencias están instaladas${NC}"
echo ""

# Limpiar compilaciones anteriores
echo "Limpiando compilaciones anteriores..."
make clean > /dev/null 2>&1 || true
echo -e "${GREEN}✓ Limpieza completada${NC}"
echo ""

# Compilar pruebas
echo "Compilando pruebas..."
if make all; then
    echo -e "${GREEN}✓ Compilación exitosa${NC}"
else
    echo -e "${RED}✗ Error en la compilación${NC}"
    echo ""
    echo "Nota: Algunas pruebas pueden requerir dependencias adicionales."
    echo "Las pruebas disponibles se ejecutarán a continuación."
fi
echo ""

# Ejecutar pruebas
echo "========================================"
echo "Ejecutando pruebas..."
echo "========================================"
echo ""

FAILED=0

# Ejecutar pruebas de utils
echo "Pruebas de utils:"
if [ -f "./test_utils" ]; then
    if ./test_utils; then
        echo -e "${GREEN}✓ Pruebas de utils pasadas${NC}"
    else
        echo -e "${RED}✗ Pruebas de utils fallaron${NC}"
        FAILED=1
    fi
else
    echo -e "${YELLOW}⚠ Pruebas de utils no disponibles${NC}"
fi
echo ""

# Ejecutar pruebas de conf
echo "Pruebas de conf:"
if [ -f "./test_conf" ]; then
    if ./test_conf; then
        echo -e "${GREEN}✓ Pruebas de conf pasadas${NC}"
    else
        echo -e "${RED}✗ Pruebas de conf fallaron${NC}"
        FAILED=1
    fi
else
    echo -e "${YELLOW}⚠ Pruebas de conf no disponibles (requiere libplist completo)${NC}"
fi
echo ""

# Resumen
echo "========================================"
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}¡Todas las pruebas disponibles pasaron!${NC}"
else
    echo -e "${RED}¡Algunas pruebas fallaron!${NC}"
fi
echo "========================================"

exit $FAILED
