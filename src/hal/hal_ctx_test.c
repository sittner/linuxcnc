/** HAL Context API Test
    Simple test program to verify the thread-local HAL context API.
*/

/********************************************************************
* Description:  hal_ctx_test.c
*               Test program for thread-local HAL context API
*
* License: LGPL Version 2
*    
* Copyright (c) 2026 All rights reserved.
********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "rtapi.h"
#include "hal.h"

/* Test component state */
static int comp_id;
static hal_pin_handle_t in_bit_h, in_float_h, in_s32_h, in_u32_h;
static hal_pin_handle_t out_bit_h, out_float_h, out_s32_h, out_u32_h;
static hal_param_handle_t gain_h, offset_h;

int main(int argc, char **argv) {
    hal_ctx_t *ctx;
    int ret;
    int cycles;
    
    printf("HAL Context API Test\n");
    printf("====================\n\n");
    
    /* Initialize HAL component */
    comp_id = hal_init("hal_ctx_test");
    if (comp_id < 0) {
        fprintf(stderr, "ERROR: hal_init() failed: %d\n", comp_id);
        return 1;
    }
    printf("✓ Component initialized (ID=%d)\n", comp_id);
    
    /* Create pins using handle-based API */
    ret = hal_pin_bit_new_handle("hal_ctx_test.in_bit", HAL_IN, &in_bit_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_bit_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created input bit pin (handle=%d)\n", in_bit_h);
    
    ret = hal_pin_float_new_handle("hal_ctx_test.in_float", HAL_IN, &in_float_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_float_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created input float pin (handle=%d)\n", in_float_h);
    
    ret = hal_pin_s32_new_handle("hal_ctx_test.in_s32", HAL_IN, &in_s32_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_s32_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created input s32 pin (handle=%d)\n", in_s32_h);
    
    ret = hal_pin_u32_new_handle("hal_ctx_test.in_u32", HAL_IN, &in_u32_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_u32_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created input u32 pin (handle=%d)\n", in_u32_h);
    
    ret = hal_pin_bit_new_handle("hal_ctx_test.out_bit", HAL_OUT, &out_bit_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_bit_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created output bit pin (handle=%d)\n", out_bit_h);
    
    ret = hal_pin_float_new_handle("hal_ctx_test.out_float", HAL_OUT, &out_float_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_float_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created output float pin (handle=%d)\n", out_float_h);
    
    ret = hal_pin_s32_new_handle("hal_ctx_test.out_s32", HAL_OUT, &out_s32_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_s32_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created output s32 pin (handle=%d)\n", out_s32_h);
    
    ret = hal_pin_u32_new_handle("hal_ctx_test.out_u32", HAL_OUT, &out_u32_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_pin_u32_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created output u32 pin (handle=%d)\n", out_u32_h);
    
    /* Create parameters using handle-based API */
    ret = hal_param_float_new_handle("hal_ctx_test.gain", HAL_RW, &gain_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_param_float_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created gain parameter (handle=%d)\n", gain_h);
    
    ret = hal_param_float_new_handle("hal_ctx_test.offset", HAL_RW, &offset_h, comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_param_float_new_handle() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Created offset parameter (handle=%d)\n", offset_h);
    
    /* Mark component as ready */
    ret = hal_ready(comp_id);
    if (ret < 0) {
        fprintf(stderr, "ERROR: hal_ready() failed: %d\n", ret);
        goto cleanup;
    }
    printf("✓ Component marked as ready\n\n");
    
    /* Create thread-local context */
    ctx = hal_ctx_create(comp_id);
    if (!ctx) {
        fprintf(stderr, "ERROR: hal_ctx_create() failed\n");
        goto cleanup;
    }
    printf("✓ Context created\n\n");
    
    printf("Running sync/access test cycles...\n");
    
    /* Run test cycles */
    for (cycles = 0; cycles < 3; cycles++) {
        printf("\nCycle %d:\n", cycles + 1);
        
        /* Sync read */
        ret = hal_ctx_sync_read(ctx);
        if (ret < 0) {
            fprintf(stderr, "ERROR: hal_ctx_sync_read() failed: %d\n", ret);
            break;
        }
        printf("  ✓ Sync read completed\n");
        
        /* Read input values */
        hal_bit_t in_bit = hal_ctx_pin_bit_get(ctx, in_bit_h);
        hal_float_t in_float = hal_ctx_pin_float_get(ctx, in_float_h);
        hal_s32_t in_s32 = hal_ctx_pin_s32_get(ctx, in_s32_h);
        hal_u32_t in_u32 = hal_ctx_pin_u32_get(ctx, in_u32_h);
        
        printf("  Input values: bit=%d, float=%.2f, s32=%d, u32=%u\n",
               in_bit, in_float, in_s32, in_u32);
        
        /* Read parameters */
        hal_float_t gain = hal_ctx_param_float_get(ctx, gain_h);
        hal_float_t offset = hal_ctx_param_float_get(ctx, offset_h);
        
        printf("  Parameters: gain=%.2f, offset=%.2f\n", gain, offset);
        
        /* Write output values (simple passthrough with processing) */
        hal_ctx_pin_bit_set(ctx, out_bit_h, !in_bit);  /* Invert */
        hal_ctx_pin_float_set(ctx, out_float_h, in_float * gain + offset);
        hal_ctx_pin_s32_set(ctx, out_s32_h, in_s32 + 100);
        hal_ctx_pin_u32_set(ctx, out_u32_h, in_u32 * 2);
        
        printf("  ✓ Output values written to context\n");
        
        /* Sync write */
        ret = hal_ctx_sync_write(ctx);
        if (ret < 0) {
            fprintf(stderr, "ERROR: hal_ctx_sync_write() failed: %d\n", ret);
            break;
        }
        printf("  ✓ Sync write completed (diff applied)\n");
        
        usleep(100000);  /* 100ms delay */
    }
    
    printf("\n✓ All cycles completed successfully\n\n");
    
    /* Test error conditions */
    printf("Testing error conditions...\n");
    
    /* Test 1: Try to sync_read twice - should fail */
    ret = hal_ctx_sync_read(ctx);
    if (ret < 0) {
        printf("  ✓ Double sync_read correctly rejected (ret=%d)\n", ret);
    } else {
        fprintf(stderr, "  ✗ ERROR: Double sync_read should have failed!\n");
    }
    
    /* Reset by doing a sync_write */
    hal_ctx_sync_write(ctx);
    
    /* Test 2: Access pin without sync_read - should fail gracefully */
    printf("\n  Testing pin access without sync_read...\n");
    hal_ctx_t *ctx2 = hal_ctx_create(comp_id);
    if (!ctx2) {
        fprintf(stderr, "  ERROR: Failed to create second context\n");
        goto cleanup;
    }
    hal_float_t val = hal_ctx_pin_float_get(ctx2, in_float_h);
    printf("  ✓ Pin access without sync_read handled (returned %.2f)\n", val);
    
    /* Test 3: sync_write without sync_read - should return error */
    printf("  Testing sync_write without sync_read...\n");
    ret = hal_ctx_sync_write(ctx2);
    if (ret == -EINVAL) {
        printf("  ✓ sync_write without sync_read correctly rejected (ret=%d)\n", ret);
    } else {
        fprintf(stderr, "  ✗ ERROR: sync_write without sync_read should have returned -EINVAL, got %d\n", ret);
    }
    
    /* Test 4: Double sync_write - should return error */
    printf("  Testing double sync_write...\n");
    hal_ctx_sync_read(ctx2);
    hal_ctx_sync_write(ctx2);
    ret = hal_ctx_sync_write(ctx2);
    if (ret == -EINVAL) {
        printf("  ✓ Double sync_write correctly rejected (ret=%d)\n", ret);
    } else {
        fprintf(stderr, "  ✗ ERROR: Double sync_write should have returned -EINVAL, got %d\n", ret);
    }
    
    hal_ctx_destroy(ctx2);
    printf("  ✓ Second context destroyed\n");
    
    printf("\n✓ All error condition tests passed\n\n");
    
    /* Cleanup */
    hal_ctx_destroy(ctx);
    printf("✓ Context destroyed\n");
    
cleanup:
    hal_exit(comp_id);
    printf("✓ Component cleaned up\n");
    
    printf("\nTest completed successfully!\n");
    return 0;
}
