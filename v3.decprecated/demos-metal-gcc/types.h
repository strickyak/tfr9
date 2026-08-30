#ifndef _TFR9_V3_APPS_TYPES_H_
#define _TFR9_V3_APPS_TYPES_H_

#include <stdarg.h>

typedef unsigned char bool;
typedef unsigned char byte;
typedef unsigned int word;
typedef unsigned int size_t;

typedef void (*int_consumer_fn)(int);
typedef void (*runnable_fn)(void);

#define TRUE 1
#define FALSE 0
#define NULL ((void*)0)

#define PEEK1(A) (*(volatile byte*)(A))
#define PEEK2(A) (*(volatile word*)(A))

#define POKE1(A, X) ((*(volatile byte*)(A)) = (X))
#define POKE2(A, X) ((*(volatile word*)(A)) = (X))

#endif  // _TFR9_V3_APPS_TYPES_H_
