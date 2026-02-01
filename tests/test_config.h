/*
 * test_config.h
 *
 * Configuración para pruebas unitarias
 * Define las macros necesarias para compilar las pruebas
 */

#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/* Definir HAVE_CLOCK_GETTIME para usar clock_gettime del sistema en Linux */
#define HAVE_CLOCK_GETTIME 1

/* Definir HAVE_CONFIG_H para que utils.c incluya este archivo */
#define HAVE_CONFIG_H 1

#endif /* TEST_CONFIG_H */
