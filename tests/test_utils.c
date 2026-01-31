/*
 * test_utils.c
 *
 * Pruebas unitarias para el módulo utils
 *
 * Este programa es libre; puedes redistribuirlo y/o modificarlo
 * bajo los términos de la Licencia Pública General GNU.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "../src/utils.h"

/* Contador de pruebas */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

/* Macros para pruebas */
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

/* Prueba: fdlist_create */
void test_fdlist_create(void) {
    TEST_START("fdlist_create");
    
    struct fdlist list;
    fdlist_create(&list);
    
    ASSERT(list.count == 0, "El contador debe ser 0");
    ASSERT(list.capacity > 0, "La capacidad debe ser mayor que 0");
    ASSERT(list.owners != NULL, "owners no debe ser NULL");
    ASSERT(list.fds != NULL, "fds no debe ser NULL");
    
    fdlist_free(&list);
    TEST_PASS();
}

/* Prueba: fdlist_add */
void test_fdlist_add(void) {
    TEST_START("fdlist_add");
    
    struct fdlist list;
    fdlist_create(&list);
    
    int initial_count = list.count;
    fdlist_add(&list, FD_CLIENT, 42, POLLIN);
    
    ASSERT(list.count == initial_count + 1, "El contador debe incrementar en 1");
    ASSERT(list.fds[list.count - 1].fd == 42, "El fd debe ser 42");
    ASSERT(list.owners[list.count - 1] == FD_CLIENT, "El owner debe ser FD_CLIENT");
    
    fdlist_free(&list);
    TEST_PASS();
}

/* Prueba: fdlist_reset */
void test_fdlist_reset(void) {
    TEST_START("fdlist_reset");
    
    struct fdlist list;
    fdlist_create(&list);
    
    fdlist_add(&list, FD_CLIENT, 42, POLLIN);
    fdlist_add(&list, FD_USB, 43, POLLOUT);
    
    ASSERT(list.count == 2, "Debe haber 2 elementos");
    
    fdlist_reset(&list);
    
    ASSERT(list.count == 0, "El contador debe ser 0 después de reset");
    
    fdlist_free(&list);
    TEST_PASS();
}

/* Prueba: mstime64 */
void test_mstime64(void) {
    TEST_START("mstime64");
    
    uint64_t t1 = mstime64();
    
    /* Pequeña espera para asegurar que el tiempo avanza */
    struct timespec ts = {0, 10000000}; /* 10ms */
    nanosleep(&ts, NULL);
    
    uint64_t t2 = mstime64();
    
    ASSERT(t2 > t1, "El tiempo debe avanzar");
    ASSERT((t2 - t1) >= 10, "La diferencia debe ser al menos 10ms");
    
    TEST_PASS();
}

/* Prueba: get_tick_count */
void test_get_tick_count(void) {
    TEST_START("get_tick_count");
    
    struct timeval tv1, tv2;
    get_tick_count(&tv1);
    
    /* Pequeña espera */
    struct timespec ts = {0, 5000000}; /* 5ms */
    nanosleep(&ts, NULL);
    
    get_tick_count(&tv2);
    
    ASSERT(tv2.tv_sec >= tv1.tv_sec, "Los segundos deben ser iguales o mayores");
    if (tv2.tv_sec == tv1.tv_sec) {
        ASSERT(tv2.tv_usec > tv1.tv_usec, "Los microsegundos deben ser mayores");
    }
    
    TEST_PASS();
}

/* Función principal de pruebas */
int main(void) {
    printf("========================================\n");
    printf("Pruebas unitarias para utils.c\n");
    printf("========================================\n\n");
    
    printf("Pruebas de fdlist:\n");
    test_fdlist_create();
    test_fdlist_add();
    test_fdlist_reset();
    
    printf("\nPruebas de tiempo:\n");
    test_mstime64();
    test_get_tick_count();
    
    printf("\n========================================\n");
    printf("Resumen de pruebas:\n");
    printf("  Total:   %d\n", tests_run);
    printf("  Pasadas: %d\n", tests_passed);
    printf("  Falladas: %d\n", tests_failed);
    printf("========================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
