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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <err.h>
#include <string.h>
#include <limits.h>


#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>
#include "_optparse.h"
#include "imgcmp.h"
#include "util.h"

static const char progname[] = "imgdups";

FILE *jfp;
static bool jsondump = false;
static int threshold = 1;
static int verbose = 1;
static bool missing_ok = false;
static struct item_t *g_head = NULL;
static char *last_key;

static char *
mkstr(const unsigned char *val, size_t len) {
	char *ret = emalloc(len + 1);
	ret[len] = 0;
	memcpy(ret, val, len);
	return ret;
}

#define STOREATIF(attr, key) do {	\
	if (!strcmp(last_key, key)) {	\
		g_head->attr = val;	\
		return 1;		\
	} } while (0)

static int
jstr(void *ctx, const unsigned char *str, size_t len) {
	char *val = mkstr(str, len);
	if (!last_key || !g_head)
		return 0;
	STOREATIF(path, "path");
	free(val);
	return 0;
}

static int
jnum(void *ctx, const char *str, size_t len) {
	char *end;
	unsigned long val = strtoul(str, &end, 10);
	if (end - str != (long int)len)
		return 0;
	if (!last_key || !g_head)
		return 0;
	STOREATIF(size, "size");
	STOREATIF(w, "w");
	STOREATIF(h, "h");
	STOREATIF(mtime, "mtime");
	STOREATIF(etime, "etime");
	STOREATIF(hashes[TI_BASE], "base");
	STOREATIF(hashes[TI_ROT1], "rot1");
	STOREATIF(hashes[TI_ROT2], "rot2");
	STOREATIF(hashes[TI_ROT3], "rot3");
	STOREATIF(hashes[TI_FLIP], "flip");
	STOREATIF(hashes[TI_FLR1], "flr1");
	STOREATIF(hashes[TI_FLR2], "flr2");
	STOREATIF(hashes[TI_FLR3], "flr3");
	return 0;
}

static int
jkey(void *ctx, const unsigned char *str, size_t len) {
	char *val = mkstr(str, len);
	if (last_key)
		free(last_key);
	last_key = val;
	return 1;
}

static int
jmaps(void *ctx) {
	struct item_t *item = ecalloc(1, sizeof(*item));
	item->next = g_head;
	item->eq_dist = -1;
	item->eq_trans = TI_LAST;
	g_head = item;
	return 1;
}

static int
jmape(void *ctx) {
	if (!g_head || !g_head->path)
		return 0;
	if (!missing_ok && access(g_head->path, F_OK) != 0) {
		if (verbose > 1) {
			warnx("skipping missing file %s", g_head->path);
		}
		g_head = free_item(g_head);
	}
	if (last_key) {
		free(last_key);
		last_key = NULL;
	}
	return 1;
}

static yajl_callbacks jcb = {
	.yajl_string = jstr,
	.yajl_start_map = jmaps,
	.yajl_map_key = jkey,
	.yajl_end_map = jmape,
	.yajl_number = jnum,
};


static size_t
dist(uint64_t a, uint64_t b) {
	uint64_t c;
	size_t d = 0;

	for (c = a^b; c; c &= c - 1)
		++d;
	return d;
}

static void
postproc(struct item_t *items) {
	static bool first = true;
	struct item_t *ref;
	struct item_t *tmp;


	for (ref = items; ref; ref = ref->next) {
		if (!ref->eq_n)
			continue;

		if (jsondump)
			fprintf(jfp, first ? "\t[" : ",[");
		if (jsondump) {
			fputjson(jfp, "\t\t", ref, true);
		} else {
			fprintf(stdout, "%s\n", ref->path);
		}
		for (tmp = ref->eq_next; tmp; tmp = tmp->eq_next) {
			if (jsondump) {
				tmp->eq_dist = dist(ref->hashes[TI_BASE], tmp->hashes[tmp->eq_trans]);
				fputjson(jfp, "\t\t", tmp, false);
			} else {
				fprintf(stdout, "%s\n", tmp->path);
			}
		}
		if (jsondump)
			fprintf(jfp, "\n\t]");
		first = false;
	}
}

static bool
hasheq(uint64_t a, uint64_t b) {
	int d = 0;
	for (uint64_t c = a^b; c; c &= c - 1)
		if (++d > threshold)
			return false;
	return true;
}

static int
cmp_items(const struct item_t *a, const struct item_t *b) {
#define _CMP(t) do {						\
	if (hasheq(a->hashes[TI_BASE], b->hashes[t]))		\
		return TI_LAST + t;				\
	} while(0)

	_CMP(TI_BASE);
	_CMP(TI_FLIP);
	_CMP(TI_ROT1);
	_CMP(TI_ROT2);
	_CMP(TI_ROT3);
	_CMP(TI_FLR1);
	_CMP(TI_FLR2);
	_CMP(TI_FLR3);
	return 0;
}

static void
handle_pair(struct item_t *ref, struct item_t *tmp) {
	int eqt;

	if (tmp->eq_parent)
		return;
	
	if ((eqt = cmp_items(ref, tmp))) {
		struct item_t *p;
		for (p = ref; p->eq_parent; p = p->eq_parent);
		tmp->eq_parent = p;
		tmp->eq_next = p->eq_next;
		tmp->eq_trans = eqt - TI_LAST;
		p->eq_next = tmp;
		p->eq_n++;
	}
}

static void
refcmp(struct item_t *items, struct item_t *refs) {
	struct item_t *ref;
	struct item_t *tmp;

	for (ref = refs; ref; ref = ref->next) {
		for (tmp = items; tmp; tmp = tmp->next) {
			handle_pair(ref, tmp);

		}
	}
	postproc(refs);
}

static void
intracmp(struct item_t *items) {
	struct item_t *ref;
	struct item_t *tmp;

	for (ref = items; ref; ref = ref->next) {
		for (tmp = ref->next; tmp; tmp = tmp->next) {
			handle_pair(ref, tmp);
		}
	}
	postproc(items);
}

static const struct optparse_long longopts[] = {
	{ "threshold",      'l', OPTPARSE_REQUIRED },
	{ "verbose",        'v', OPTPARSE_NONE },
	{ "quiet",          'q', OPTPARSE_NONE },
	{ "jsondump",       'a', OPTPARSE_NONE },

	{ "stdin",          'i', OPTPARSE_NONE },
	{ "missing-ok",     'x', OPTPARSE_NONE },
	{ "reference-files",'R', OPTPARSE_REQUIRED },
	{ "intragroupcheck",'G', OPTPARSE_NONE },

	{ "dedup",          'd', OPTPARSE_NONE },
	{ "zsh-comp-gen", -3515, OPTPARSE_NONE },
	{ 0 },
};

static void
usage() {
	fprintf(stdout, "usage: %s ...\n", progname);
	optparse_dump_options(longopts);
	exit(1);
}

static void
parse_json(FILE *fp, const char *name) {
	yajl_handle hand;
	size_t bufsiz = 4096;
	uint8_t *data;
	hand = yajl_alloc(&jcb, NULL, NULL);

	data = emalloc(bufsiz);

	for (;;) {
		size_t rd;

		if (!(rd = fread(data, 1, bufsiz, fp))) {
			if (!feof(fp)) {
				err(1, "fread %s", name);
			}
			break;
		}
		if (yajl_parse(hand, data, rd) != yajl_status_ok) {
			errx(1, "Unable to parse json past %lu in %s\n",
			       yajl_get_bytes_consumed(hand), name);
		}
	}

	free(data);

	yajl_complete_parse(hand);
	yajl_free(hand);
}

static void
read_file(const char *path) {
	FILE *fp;

	if (!(fp = fopen(path, "r")))
		err(1, "fopen %s", path);
	parse_json(fp, path);
	fclose(fp);
}

static struct item_t *
reset_head(void) {
	struct item_t *ret = g_head;
	g_head = NULL;
	return ret;
}

static void
iorrcmp(struct item_t *items, struct item_t *refs) {
	if (!items)
		return;
	if (refs)
		refcmp(items, refs);
	else
		intracmp(items);
}

int
main(int argc, char *argv[]) {
	char jsonfile[] = "/tmp/imghash-XXXXXX";
	bool dedup = false;
	struct optparse op;
	long opt;
	struct item_t *refitems = NULL;
	bool global = true;
	bool from_stdin = false;

	optparse_init(&op, argv);
	while ((opt = optparse_long(&op, longopts, NULL)) != -1) {
		switch (opt) {
		case 'i':
			from_stdin = true;
			break;
		case 'x':
			missing_ok = true;
			break;
		case 'a':
			jsondump = true;
			break;
		case 'v':
			++verbose;
			break;
		case 'q':
			--verbose;
			break;
		case 'l':
			threshold = atoi(op.optarg);
			break;
		case 'G':
			global = false;
			break;
		case 'R':
			read_file(op.optarg);
			if (!(refitems = reset_head()))
				errx(1, "no references in %s", op.optarg);
			break;
		case '?':
			warnx("%s", op.errmsg);
			usage();
			break;

		case -3515:
			optparse_dump_zsh_comp(longopts, progname, "_files");
			exit(0);
		}
	}

	argv += op.optind;
	argc -= op.optind;

	if (dedup) {
		jsondump = true;
		int fd = mkstemp(jsonfile);
		if (fd < 0 || !(jfp = fdopen(fd, "w"))) {
			err(1, "unable to get tempfile %s", jsonfile);
		}
		printf("Writing to tempfile %s\n", jsonfile);
	} else {
		jfp = stdout;
	}

	if (from_stdin == !!argc)
		usage();

	if (jsondump)
		fprintf(jfp, "[");


	if (from_stdin) {
		parse_json(stdin, "stdin");
		iorrcmp(g_head, refitems);
		free_items(g_head);
		g_head = NULL;
	}
	if (global) {
		for (int i = 0; i < argc; ++i) {
			read_file(argv[i]);
		}
		iorrcmp(g_head, refitems);
	} else {
		for (int i = 0; i < argc; ++i) {
			read_file(argv[i]);
			struct item_t *items = reset_head();
			iorrcmp(items, refitems);
			free_items(items);
		}
	}

	if (jsondump)
		fprintf(jfp, "\n]\n");
	if (jfp != stdout)
		fclose(jfp);
	
	if (refitems)
		free_items(refitems);
	if (g_head)
		free_items(g_head);

	return 0;
}
