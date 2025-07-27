#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

// printf.c
int             printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));
void            panic(const char*) __attribute__((noreturn));
void            printfinit(void);
int             vprintf(const char *fmt, va_list ap);

// swtch.S
void            swtch(struct context*, struct context*);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

#ifdef __cplusplus
}
#endif

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

