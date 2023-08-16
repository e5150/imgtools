#pragma once
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <err.h>

enum trans_t {
	TI_BASE, TI_ROT1, TI_ROT2, TI_ROT3,
	TI_FLIP, TI_FLR1, TI_FLR2, TI_FLR3, TI_LAST,
};

enum {
	TRANS_NONE   = 0,
	TRANS_ROTATE = (1 << 0),
	TRANS_FLIP   = (1 << 1),
};

struct item_t {
	bool valid;
	char *path;
	uint8_t *data;
	time_t mtime;
	time_t etime;
	int w, h, size;
	uint64_t hashes[TI_LAST];
	struct item_t *next;
	struct item_t *eq_parent;
	struct item_t *eq_next;
	enum trans_t eq_trans;
	int eq_dist;
	int eq_n;
};

const char *tname(enum trans_t);
struct item_t *free_item(struct item_t*);
void *ecalloc(size_t, size_t);
void *emalloc(size_t);
void *erealloc(void*, size_t);
void fputjson(FILE*, const char*, const struct item_t*, bool);
void free_items(struct item_t*);
