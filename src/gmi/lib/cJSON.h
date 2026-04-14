// cJSON - Ultralightweight JSON parser in ANSI C
// https://github.com/DaveGamble/cJSON
//
// Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
// SPDX-License-Identifier: MIT
//
// NOTE: This is a stub header. Download the real cJSON from:
//   https://github.com/DaveGamble/cJSON/blob/master/cJSON.h
//   https://github.com/DaveGamble/cJSON/blob/master/cJSON.c
//
// Or install via package manager: libcjson-dev (Debian/Ubuntu)

#ifndef cJSON__h
#define cJSON__h

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// cJSON Types
#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7)

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

// The cJSON structure
typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;

    int type;

    char *valuestring;
    int valueint;  // deprecated, use valuedouble
    double valuedouble;

    char *string;  // key name
} cJSON;

typedef struct cJSON_Hooks {
    void *(*malloc_fn)(size_t sz);
    void (*free_fn)(void *ptr);
} cJSON_Hooks;

// Supply malloc/free functions
void cJSON_InitHooks(cJSON_Hooks* hooks);

// Parse JSON string
cJSON *cJSON_Parse(const char *value);
cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length);

// Render to string (caller must free)
char *cJSON_Print(const cJSON *item);
char *cJSON_PrintUnformatted(const cJSON *item);

// Delete cJSON object
void cJSON_Delete(cJSON *item);

// Create items
cJSON *cJSON_CreateNull(void);
cJSON *cJSON_CreateTrue(void);
cJSON *cJSON_CreateFalse(void);
cJSON *cJSON_CreateBool(int boolean);
cJSON *cJSON_CreateNumber(double num);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateObject(void);

// Array functions
int cJSON_GetArraySize(const cJSON *array);
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);
void cJSON_AddItemToArray(cJSON *array, cJSON *item);

// Object functions
cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);
int cJSON_HasObjectItem(const cJSON *object, const char *string);
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
cJSON *cJSON_AddNullToObject(cJSON *object, const char *name);
cJSON *cJSON_AddTrueToObject(cJSON *object, const char *name);
cJSON *cJSON_AddFalseToObject(cJSON *object, const char *name);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int boolean);
cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number);
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string);

// Type checks
#define cJSON_IsInvalid(item) ((item) == NULL || ((item)->type & 0xFF) == cJSON_Invalid)
#define cJSON_IsFalse(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_False)
#define cJSON_IsTrue(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_True)
#define cJSON_IsBool(item) ((item) != NULL && (((item)->type & 0xFF) == cJSON_True || ((item)->type & 0xFF) == cJSON_False))
#define cJSON_IsNull(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_NULL)
#define cJSON_IsNumber(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_Number)
#define cJSON_IsString(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_String)
#define cJSON_IsArray(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_Array)
#define cJSON_IsObject(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_Object)

// Value getters
double cJSON_GetNumberValue(const cJSON *item);
char *cJSON_GetStringValue(const cJSON *item);

// Iteration
#define cJSON_ArrayForEach(element, array) \
    for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

#ifdef __cplusplus
}
#endif

#endif
