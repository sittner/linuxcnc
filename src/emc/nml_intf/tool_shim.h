/*
 * tool_shim.h — C interface to the tooldata shared memory.
 *
 * Provides extern "C" wrappers around the C++ tooldata API,
 * callable from Go via cgo. Implementation in tool_shim.cc.
 */

#ifndef TOOL_SHIM_H
#define TOOL_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

#define TOOL_SHIM_MAX_POCKETS 1001
#define TOOL_SHIM_COMMENT_LEN 256

typedef struct {
    int     toolno;
    int     pocketno;
    double  x_offset;
    double  y_offset;
    double  z_offset;
    double  a_offset;
    double  b_offset;
    double  c_offset;
    double  u_offset;
    double  v_offset;
    double  w_offset;
    double  diameter;
    double  frontangle;
    double  backangle;
    int     orientation;
    char    comment[TOOL_SHIM_COMMENT_LEN];
} tool_shim_entry_t;

/* Initialize tool mmap (user/client mode). Returns 0 on success. */
int tool_shim_init(void);

/* Load tool table from file (populates mmap + comments). Returns 0 on success. */
int tool_shim_load(const char *filename);

/* Save tool table to file (persists mmap + comments). Returns 0 on success. */
int tool_shim_save(const char *filename);

/* Return the highest occupied tool index. */
int tool_shim_last_index(void);

/* Get tool at mmap index. Returns 0 on success, -1 on failure. */
int tool_shim_get(int idx, tool_shim_entry_t *out);

/* Put tool at mmap index. Returns 0 on success, -1 on failure. */
int tool_shim_put(int idx, const tool_shim_entry_t *in);

/* Find mmap index for a given tool number. Returns -1 if not found. */
int tool_shim_find_by_toolno(int toolno);

/* Check if random toolchanger. */
int tool_shim_is_random(void);

#ifdef __cplusplus
}
#endif

#endif /* TOOL_SHIM_H */
