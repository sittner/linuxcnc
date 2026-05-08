/********************************************************************
* Description: _shm.c
*   C implementation of rcslib shared memory API
*
*   Now uses plain heap allocation (calloc/free) with a key-based
*   lookup table, since all components run in a single process.
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: LGPL Version 2
* System: Linux
*
* Copyright (c) 2004 All rights reserved.
*
* Last change:
********************************************************************/

#include "_shm.h"
#include "rcs_print.hh"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

#define MAX_RCS_SHM 32

/* Internal registry: key-based lookup with refcount */
static struct {
    int key;
    void *addr;
    size_t size;
    int count;      /* number of attached handles */
} rcs_shm_registry[MAX_RCS_SHM];

/* Find registry slot by key, or -1 if not found */
static int rcs_shm_find(key_t key)
{
    for (int i = 0; i < MAX_RCS_SHM; i++) {
	if (rcs_shm_registry[i].key == key && rcs_shm_registry[i].addr != NULL)
	    return i;
    }
    return -1;
}

/* Find a free registry slot, or -1 */
static int rcs_shm_find_free(void)
{
    for (int i = 0; i < MAX_RCS_SHM; i++) {
	if (rcs_shm_registry[i].addr == NULL)
	    return i;
    }
    return -1;
}

shm_t *rcs_shm_open(key_t key, size_t size, int oflag, /* int mode */ ...)
{
    shm_t *shm;

    if (key == 0) {
	rcs_print_error("rcs_shm_open: key may not be zero.\n");
	return NULL;
    }

    shm = (shm_t *) calloc(sizeof(shm_t), 1);
    if (shm == NULL) {
	rcs_print_error("rcs_shm_open: calloc failed\n");
	return NULL;
    }
    shm->key = key;
    shm->size = size;
    shm->create_errno = 0;

    int slot = rcs_shm_find(key);

    if (slot >= 0) {
	/* Already exists — attach to it */
	if (rcs_shm_registry[slot].size < size) {
	    rcs_print_error("rcs_shm_open: existing buffer for key 0x%X has size %zu, requested %zu\n",
		key, rcs_shm_registry[slot].size, size);
	    shm->create_errno = EINVAL;
	    return shm;
	}
	shm->addr = rcs_shm_registry[slot].addr;
	shm->id = slot;
	shm->created = 0;
	rcs_shm_registry[slot].count++;
    } else if (oflag) {
	/* Create new */
	slot = rcs_shm_find_free();
	if (slot < 0) {
	    rcs_print_error("rcs_shm_open: no free slots (max %d)\n", MAX_RCS_SHM);
	    shm->create_errno = ENOMEM;
	    return shm;
	}
	void *mem = calloc(1, size);
	if (mem == NULL) {
	    rcs_print_error("rcs_shm_open: calloc(%zu) failed\n", size);
	    shm->create_errno = ENOMEM;
	    return shm;
	}
	rcs_shm_registry[slot].key = key;
	rcs_shm_registry[slot].addr = mem;
	rcs_shm_registry[slot].size = size;
	rcs_shm_registry[slot].count = 1;
	shm->addr = mem;
	shm->id = slot;
	shm->created = 1;
    } else {
	/* NOCREATE but doesn't exist */
	rcs_print_error("rcs_shm_open: no buffer exists for key 0x%X and create not requested\n", key);
	shm->create_errno = ENOENT;
    }

    return shm;
}

int rcs_shm_close(shm_t * shm)
{
    if (shm == NULL)
	return -1;

    if (shm->addr != NULL && shm->id >= 0 && shm->id < MAX_RCS_SHM) {
	rcs_shm_registry[shm->id].count--;
	if (rcs_shm_registry[shm->id].count <= 0) {
	    free(rcs_shm_registry[shm->id].addr);
	    rcs_shm_registry[shm->id].addr = NULL;
	    rcs_shm_registry[shm->id].key = 0;
	    rcs_shm_registry[shm->id].size = 0;
	    rcs_shm_registry[shm->id].count = 0;
	}
    }

    free(shm);
    return 0;
}

int rcs_shm_delete(shm_t * shm)
{
    if (shm == NULL)
	return -1;

    /* Force-remove regardless of refcount */
    if (shm->addr != NULL && shm->id >= 0 && shm->id < MAX_RCS_SHM) {
	free(rcs_shm_registry[shm->id].addr);
	rcs_shm_registry[shm->id].addr = NULL;
	rcs_shm_registry[shm->id].key = 0;
	rcs_shm_registry[shm->id].size = 0;
	rcs_shm_registry[shm->id].count = 0;
    }

    free(shm);
    return 0;
}

int rcs_shm_nattch(shm_t * shm)
{
    if (shm == NULL)
	return -1;
    if (shm->id < 0 || shm->id >= MAX_RCS_SHM)
	return -1;
    if (rcs_shm_registry[shm->id].addr == NULL)
	return 0;
    return rcs_shm_registry[shm->id].count;

}
