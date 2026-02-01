/*
 * test_conf.c
 *
 * Pruebas unitarias para el módulo conf
 *
 * Este programa es libre; puedes redistribuirlo y/o modificarlo
 * bajo los términos de la Licencia Pública General GNU.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "../src/conf.h"

/* Contador de pruebas */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

/* Directorio de prueba temporal */
static char *test_config_dir = NULL;

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

/* Configuración inicial */
void setup_test_env(void) {
    /* Crear directorio temporal para pruebas */
    char template[] = "/tmp/usbmuxd_test_XXXXXX";
    test_config_dir = strdup(mkdtemp(template));
    
    /* Establecer el directorio de configuración */
    config_set_config_dir(test_config_dir);
}

/* Limpieza después de las pruebas */
void teardown_test_env(void) {
    if (test_config_dir) {
        /* Eliminar archivos de prueba */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", test_config_dir);
        system(cmd);
        free(test_config_dir);
        test_config_dir = NULL;
    }
}

/* Prueba: config_get_config_dir */
void test_config_get_config_dir(void) {
    TEST_START("config_get_config_dir");
    
    const char *dir = config_get_config_dir();
    
    ASSERT(dir != NULL, "El directorio no debe ser NULL");
    ASSERT(strlen(dir) > 0, "El directorio no debe estar vacío");
    ASSERT(strcmp(dir, test_config_dir) == 0, "El directorio debe coincidir con el configurado");
    
    TEST_PASS();
}

/* Prueba: config_set_config_dir */
void test_config_set_config_dir(void) {
    TEST_START("config_set_config_dir");
    
    char new_dir[] = "/tmp/usbmuxd_test_new";
    
    int result = config_set_config_dir(new_dir);
    
    ASSERT(result == 0, "config_set_config_dir debe retornar 0");
    
    const char *dir = config_get_config_dir();
    ASSERT(dir != NULL, "El directorio no debe ser NULL");
    ASSERT(strcmp(dir, new_dir) == 0, "El directorio debe coincidir con el nuevo");
    
    /* Restaurar el directorio original */
    config_set_config_dir(test_config_dir);
    
    /* Limpiar el directorio de prueba */
    rmdir(new_dir);
    
    TEST_PASS();
}

/* Prueba: config_has_device_record (sin registro) */
void test_config_has_device_record_no_record(void) {
    TEST_START("config_has_device_record (sin registro)");
    
    const char *test_udid = "0000000000000000000000000000000000000000";
    
    int result = config_has_device_record(test_udid);
    
    ASSERT(result == 0, "No debe haber registro para un UDID que no existe");
    
    TEST_PASS();
}

/* Prueba: config_get_system_buid */
void test_config_get_system_buid(void) {
    TEST_START("config_get_system_buid");
    
    char *system_buid = NULL;
    
    config_get_system_buid(&system_buid);
    
    ASSERT(system_buid != NULL, "El SystemBUID no debe ser NULL");
    ASSERT(strlen(system_buid) > 0, "El SystemBUID no debe estar vacío");
    
    free(system_buid);
    
    TEST_PASS();
}

/* Prueba: config_set_device_record y config_get_device_record */
void test_config_set_get_device_record(void) {
    TEST_START("config_set_device_record y config_get_device_record");
    
    const char *test_udid = "1111111111111111111111111111111111111111";
    
    /* Crear un registro de dispositivo simple en formato XML */
    const char *record_xml = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>HostID</key>\n"
        "    <string>TEST_HOST_ID_12345</string>\n"
        "</dict>\n"
        "</plist>\n";
    
    int set_result = config_set_device_record(test_udid, (char*)record_xml, strlen(record_xml));
    ASSERT(set_result == 0, "config_set_device_record debe retornar 0");
    
    /* Verificar que el registro existe */
    int has_result = config_has_device_record(test_udid);
    ASSERT(has_result == 1, "El registro debe existir después de ser creado");
    
    /* Obtener el registro */
    char *record_data = NULL;
    uint64_t record_size = 0;
    int get_result = config_get_device_record(test_udid, &record_data, &record_size);
    
    ASSERT(get_result == 0, "config_get_device_record debe retornar 0");
    ASSERT(record_data != NULL, "Los datos del registro no deben ser NULL");
    ASSERT(record_size > 0, "El tamaño del registro debe ser mayor que 0");
    
    free(record_data);
    
    /* Limpiar: eliminar el registro */
    config_remove_device_record(test_udid);
    
    TEST_PASS();
}

/* Prueba: config_remove_device_record */
void test_config_remove_device_record(void) {
    TEST_START("config_remove_device_record");
    
    const char *test_udid = "2222222222222222222222222222222222222222";
    
    /* Crear un registro primero */
    const char *record_xml = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>HostID</key>\n"
        "    <string>TEST_HOST_ID_67890</string>\n"
        "</dict>\n"
        "</plist>\n";
    
    config_set_device_record(test_udid, (char*)record_xml, strlen(record_xml));
    
    /* Verificar que existe */
    ASSERT(config_has_device_record(test_udid) == 1, "El registro debe existir");
    
    /* Eliminar el registro */
    int remove_result = config_remove_device_record(test_udid);
    ASSERT(remove_result == 0, "config_remove_device_record debe retornar 0");
    
    /* Verificar que ya no existe */
    ASSERT(config_has_device_record(test_udid) == 0, "El registro no debe existir después de ser eliminado");
    
    TEST_PASS();
}

/* Prueba: config_device_record_get_host_id */
void test_config_device_record_get_host_id(void) {
    TEST_START("config_device_record_get_host_id");
    
    const char *test_udid = "3333333333333333333333333333333333333333";
    const char *expected_host_id = "TEST_HOST_ID_ABCDE";
    
    /* Crear un registro con HostID */
    char record_xml[512];
    snprintf(record_xml, sizeof(record_xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>HostID</key>\n"
        "    <string>%s</string>\n"
        "</dict>\n"
        "</plist>\n", expected_host_id);
    
    config_set_device_record(test_udid, record_xml, strlen(record_xml));
    
    /* Obtener el HostID */
    char *host_id = NULL;
    config_device_record_get_host_id(test_udid, &host_id);
    
    ASSERT(host_id != NULL, "El HostID no debe ser NULL");
    ASSERT(strcmp(host_id, expected_host_id) == 0, "El HostID debe coincidir con el esperado");
    
    free(host_id);
    
    /* Limpiar */
    config_remove_device_record(test_udid);
    
    TEST_PASS();
}

/* Función principal de pruebas */
int main(void) {
    printf("========================================\n");
    printf("Pruebas unitarias para conf.c\n");
    printf("========================================\n\n");
    
    /* Configurar entorno de prueba */
    setup_test_env();
    
    printf("Pruebas de configuración:\n");
    test_config_get_config_dir();
    test_config_set_config_dir();
    
    printf("\nPruebas de registros de dispositivo:\n");
    test_config_has_device_record_no_record();
    test_config_set_get_device_record();
    test_config_remove_device_record();
    test_config_device_record_get_host_id();
    
    printf("\nPruebas de SystemBUID:\n");
    test_config_get_system_buid();
    
    /* Limpiar entorno de prueba */
    teardown_test_env();
    
    printf("\n========================================\n");
    printf("Resumen de pruebas:\n");
    printf("  Total:   %d\n", tests_run);
    printf("  Pasadas: %d\n", tests_passed);
    printf("  Falladas: %d\n", tests_failed);
    printf("========================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
