/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "unity.h"
#include <stdlib.h>  // Enable full libc usage for target tests
#include <string.h>  // For string manipulation functions
#include <stdio.h>   // For stdio functions
#include <stdint.h>  // For uint8_t
#include <esp-stub-lib/uart.h>  // For UART functions

// Forward declarations
void stub_target_uart_init(uint8_t uart_num);

/* Test setup and teardown */
void setUp(void)
{
    // Target-specific setup (no mocks needed usually)
}

void tearDown(void)
{
    // Target-specific cleanup
}

/* Target test examples that demonstrate libc functionality */
void test_target_basic_functionality(void)
{
    // Test basic arithmetic and logic operations on target
    volatile int a = 10;
    volatile int b = 20;
    volatile int result = a + b;

    TEST_ASSERT_EQUAL(30, result);
    TEST_ASSERT_GREATER_THAN(a, b);
    TEST_ASSERT_LESS_THAN(result, a);
}

void test_target_arithmetic_operations(void)
{
    // Test more arithmetic without library calls
    volatile int x = 100;
    volatile int y = 50;

    TEST_ASSERT_EQUAL(150, x + y);
    TEST_ASSERT_EQUAL(50, x - y);
    TEST_ASSERT_EQUAL(2, x / y);
    TEST_ASSERT_EQUAL(0, x % y);
}

void test_target_stack_variables(void)
{
    // Test that stack allocation works correctly with libc functions
    char local_buffer[32];

    // Use memset from libc
    memset(local_buffer, 0xAA, sizeof(local_buffer));

    // Verify pattern using standard comparison
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAA, local_buffer[i]);
    }

    // Use memset with different pattern
    memset(local_buffer, 0x55, sizeof(local_buffer));

    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x55, local_buffer[i]);
    }
}

void test_target_libc_string_functions(void)
{
    // Test string functions from libc
    char buffer[64];
    const char *test_string = "Hello ESP Target!";

    // Test strcpy
    strcpy(buffer, test_string);
    TEST_ASSERT_EQUAL_STRING(test_string, buffer);

    // Test strlen
    TEST_ASSERT_EQUAL(17, strlen(buffer));

    // Test strcat
    strcat(buffer, " Testing");
    TEST_ASSERT_EQUAL(25, strlen(buffer));
    TEST_ASSERT_EQUAL_STRING("Hello ESP Target! Testing", buffer);

    // Test memcmp
    char buffer2[64];
    strcpy(buffer2, "Hello ESP Target! Testing");
    TEST_ASSERT_EQUAL(0, memcmp(buffer, buffer2, strlen(buffer)));
}

void test_target_libc_malloc_free(void)
{
    // Test dynamic memory allocation using libc
    size_t size = 128;
    void *ptr = malloc(size);

    // malloc should succeed (assuming enough memory)
    TEST_ASSERT_NOT_NULL(ptr);

    if (ptr != NULL) {
        // Write pattern to allocated memory
        memset(ptr, 0xCC, size);

        // Verify pattern
        uint8_t *byte_ptr = (uint8_t *)ptr;
        for (size_t i = 0; i < size; i++) {
            TEST_ASSERT_EQUAL_HEX8(0xCC, byte_ptr[i]);
        }

        // Free the memory
        free(ptr);
        ptr = NULL;
    }

    // Test multiple allocations
    void *ptrs[5];
    for (int i = 0; i < 5; i++) {
        ptrs[i] = malloc(32);
        TEST_ASSERT_NOT_NULL(ptrs[i]);

        // Write unique pattern to each allocation
        memset(ptrs[i], 0x10 + i, 32);
    }

    // Verify patterns
    for (int i = 0; i < 5; i++) {
        uint8_t *byte_ptr = (uint8_t *)ptrs[i];
        for (int j = 0; j < 32; j++) {
            TEST_ASSERT_EQUAL_HEX8(0x10 + i, byte_ptr[j]);
        }
    }

    // Free all allocations
    for (int i = 0; i < 5; i++) {
        free(ptrs[i]);
        ptrs[i] = NULL;
    }
}

// ESP32 entry point required by linker script
// Forward declare the Unity-generated main function
extern int main(void);
extern void __system_init(void);

void esp_main(void)
{
    // Initialize minimal system (BSS, UART, etc.)
    __system_init();

    // Initialize Unity and run all tests
    UNITY_BEGIN();

    // Run each test
    RUN_TEST(test_target_basic_functionality);
    RUN_TEST(test_target_arithmetic_operations);
    RUN_TEST(test_target_stack_variables);
    RUN_TEST(test_target_libc_string_functions);
    RUN_TEST(test_target_libc_malloc_free);

    // Finish Unity testing
    int result = UNITY_END();

    // Send test completion marker for load-test.py
    stub_lib_uart_tx_flush();
    printf("\n--- UNITY TEST RUN COMPLETE ---\n");
    printf("Test Results: %s\n", (result == 0) ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    printf("--- END OF TESTS ---\n");
    stub_lib_uart_tx_flush();

    // Exit gracefully (could loop forever or halt depending on system requirements)
    while (1) {
        stub_lib_uart_tx_flush();
        stub_lib_delay_us(1000000);  // 1 second delay
    }
}
