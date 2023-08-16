/*
 * Copyright © 2023 Lars Lindqvist <lars.lindqvist at yandex.ru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the “Software”),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "imgcmp.h"

const char *
tname(enum trans_t t) {
	switch (t) {
	case TI_BASE: return "base"; break;
	case TI_FLIP: return "flip"; break;
	case TI_ROT1: return "rot1"; break;
	case TI_ROT2: return "rot2"; break;
	case TI_ROT3: return "rot3"; break;
	case TI_FLR1: return "flr1"; break;
	case TI_FLR2: return "flr2"; break;
	case TI_FLR3: return "flr3"; break;
	default:
		return "";
	}
}

struct item_t *
free_item(struct item_t *item) {
	if (!item)
		return NULL;
	struct item_t *next = item->next;
	free(item->path);
	free(item->data);
	free(item);
	return next;
}

void
free_items(struct item_t *items) {
	while (items) {
		items = free_item(items);
	}
}

void
fputjson(FILE *fp, const char *indent, const struct item_t *item, bool first) {
	fprintf(fp, first ? "\n" : ",\n");
	fprintf(fp, "%s{\n", indent);
	fprintf(fp, "%s\t\"path\":\"%s\",\n", indent, item->path);
	fprintf(fp, "%s\t\"size\":%d,\n", indent, item->size);
	fprintf(fp, "%s\t\"w\":%d,\n", indent, item->w);
	fprintf(fp, "%s\t\"h\":%d,\n", indent, item->h);
	fprintf(fp, "%s\t\"mtime\":%ld,\n", indent, item->mtime);
	if (item->etime)
		fprintf(fp, "%s\t\"etime\":%ld,\n", indent, item->etime);
	if (item->eq_dist != -1) {
		fprintf(fp, "%s\t\"dist\":%d,\n", indent, item->eq_dist);
		fprintf(fp, "%s\t\"xform\":\"%s\",\n", indent, tname(item->eq_trans));
		fprintf(fp, "%s\t\"hash\": %lu\n", indent,
		       item->hashes[item->eq_trans]
		);
	} else for (size_t i = 0; i < TI_LAST; ++i) {
		fprintf(fp, "%s\t\"%s\": %lu%s\n", indent,
		       tname(i),
		       item->hashes[i],
		       i < TI_LAST - 1 ? "," : ""
		);
	}
	fprintf(fp, "%s}", indent);
}
