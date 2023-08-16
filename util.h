#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <err.h>
void *ecalloc(size_t, size_t);
void *emalloc(size_t);
void *erealloc(void*, size_t);
