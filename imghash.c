/*
 * Copyright © 2010, 2013-2014, 2019, 2023 Lars Lindqvist
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>

#include <libexif/exif-data.h>
#include <turbojpeg.h>

#include "_optparse.h"
#include "thpool.h"
#include "imgcode.h"
#include "imgcmp.h"
#include "util.h"

static const char progname[] = "imghash";

FILE *jfp;
static bool jsondump = false;

struct item_t *head = NULL;
static off_t maxbuf = 64 * 1024 * 1024;
static int verbose = 1;
static uint32_t transform = TRANS_NONE;

static int nthreads = 8;
static threadpool threads;
pthread_mutex_t prlock;
pthread_mutex_t imlock;


static void
prhash(const struct item_t *item, enum trans_t t) {
	uint64_t hash = item->hashes[t];
	fprintf(stdout, "%016lx", hash);
	if (verbose)
		fprintf(stdout, "\t%s", item->path);
	if (verbose > 1)
		fprintf(stdout, "\t# %s", tname(t));
	fprintf(stdout, "\n");
}

static int
set_exif_date(struct item_t *item) {
	ExifData *d = exif_data_new_from_data(item->data, item->size);
	int ret = -1;

	if (!d) {
		return -1;
	}
	ExifEntry *e = NULL;

	if (!e) e = exif_data_get_entry(d, EXIF_TAG_DATE_TIME_ORIGINAL);
	if (!e) e = exif_data_get_entry(d, EXIF_TAG_DATE_TIME_DIGITIZED);
	if (!e) e = exif_data_get_entry(d, EXIF_TAG_DATE_TIME);

	if (e) {
		char buf[24];
		exif_entry_get_value(e, buf, sizeof(buf));
		struct tm tm;

		if (strptime(buf, "%Y:%m:%d %H:%M:%S", &tm)) {
			item->etime = mktime(&tm);
			ret = 0;
		}
	}

	exif_data_unref(d);
	return ret;
}

static void
print_item(const struct item_t *item) {
	pthread_mutex_lock(&prlock);
	static bool first = true;

	if (!item->valid)
		return;
	if (jsondump) {
		fputjson(jfp, "\t", item, first);
	} else {
		prhash(item, TI_BASE);
		if (transform & TRANS_ROTATE) {
			prhash(item, TI_ROT1);
			prhash(item, TI_ROT2);
			prhash(item, TI_ROT3);
		}
		if (transform & TRANS_FLIP) {
			prhash(item, TI_FLIP);
			if (transform & TRANS_ROTATE) {
				prhash(item, TI_FLR1);
				prhash(item, TI_FLR2);
				prhash(item, TI_FLR3);
			}
		}
	}
	first = false;
	pthread_mutex_unlock(&prlock);
}

static const double DCT_O[] = { /* √(2/N) * cos(п / 2N * y * (2x + 1)) */
	+0.500000000000, +0.500000000000, +0.500000000000, +0.500000000000, +0.500000000000, +0.500000000000, +0.500000000000, +0.500000000000,
	+0.490392640202, +0.415734806151, +0.277785116510, +0.097545161008, -0.097545161008, -0.277785116510, -0.415734806151, -0.490392640202,
	+0.461939766256, +0.191341716183, -0.191341716183, -0.461939766256, -0.461939766256, -0.191341716183, +0.191341716183, +0.461939766256,
	+0.415734806151, -0.097545161008, -0.490392640202, -0.277785116510, +0.277785116510, +0.490392640202, +0.097545161008, -0.415734806151,
	+0.353553390593, -0.353553390593, -0.353553390593, +0.353553390593, +0.353553390593, -0.353553390593, -0.353553390593, +0.353553390593,
	+0.277785116510, -0.490392640202, +0.097545161008, +0.415734806151, -0.415734806151, -0.097545161008, +0.490392640202, -0.277785116510,
	+0.191341716183, -0.461939766256, +0.461939766256, -0.191341716183, -0.191341716183, +0.461939766256, -0.461939766256, +0.191341716183,
	+0.097545161008, -0.277785116510, +0.415734806151, -0.490392640202, +0.490392640202, -0.415734806151, +0.277785116510, -0.097545161008,
};
static const double DCT_T[] = {
	+0.500000000000, +0.490392640202, +0.461939766256, +0.415734806151, +0.353553390593, +0.277785116510, +0.191341716183, +0.097545161008,
	+0.500000000000, +0.415734806151, +0.191341716183, -0.097545161008, -0.353553390593, -0.490392640202, -0.461939766256, -0.277785116510,
	+0.500000000000, +0.277785116510, -0.191341716183, -0.490392640202, -0.353553390593, +0.097545161008, +0.461939766256, +0.415734806151,
	+0.500000000000, +0.097545161008, -0.461939766256, -0.277785116510, +0.353553390593, +0.415734806151, -0.191341716183, -0.490392640202,
	+0.500000000000, -0.097545161008, -0.461939766256, +0.277785116510, +0.353553390593, -0.415734806151, -0.191341716183, +0.490392640202,
	+0.500000000000, -0.277785116510, -0.191341716183, +0.490392640202, -0.353553390593, -0.097545161008, +0.461939766256, -0.415734806151,
	+0.500000000000, -0.415734806151, +0.191341716183, +0.097545161008, -0.353553390593, +0.490392640202, -0.461939766256, +0.277785116510,
	+0.500000000000, -0.490392640202, +0.461939766256, -0.415734806151, +0.353553390593, -0.277785116510, +0.191341716183, -0.097545161008,
};

static void
scale_down(double *dst, const uint8_t *src, int w, int h) {
	int Dy = h / 8;
	int Dx = w / 8;
	int i = 0;

	int X0 = (w % 8) / 2;
	int Y0 = (h % 8) / 2;

	for (int y0 = 0; y0 < 8; ++y0) {
		for (int x0 = 0; x0 < 8; ++x0) {
			double sum = 0.0;
			for (int dy = 0; dy < Dy; ++dy)
			for (int dx = 0; dx < Dx; ++dx) {
				int row = Y0 + y0 * Dy + dy;
				int col = X0 + x0 * Dx + dx;
				sum += src[w * row + col];
			}
			dst[i++] = sum / (Dx * Dy);
			
		}
	}
}

uint64_t
genhash(double *ebe) {
	uint64_t ret, bit;
	double dct[64];
	int i;

	for (i = 0; i < 64; ++i)
		dct[i] = 0.0;
	for (int y = 0; y < 8; ++y) {
		const double *dct_row = DCT_O + 8 * y;
		double *ret_row = dct + 8 * y;
		for (int x = 0; x < 8; ++x) {
			const double *dct_col = DCT_T + 8 * x;
			double tmp = 0.0;
			for (i = 0; i < 8; ++i) {
				tmp += dct_row[i] * ebe[x + i * 8];
			}
			for (i = 0; i < 8; ++i) {
				ret_row[i] += dct_col[i] * tmp;
			}
		}
	}

	for (ret = 0, bit = 1, i = 0; i < 64; ++i, bit <<= 1) {
		if (dct[i] > 0.0) {
			ret |= bit;
		}
	}
	return ret;
}

static uint64_t
hflip(double *dst, const double *src) {
	const double *p = src;
	for (int y = 0; y < 8; ++y) {
		for (int x = 7; x >= 0; --x) {
			dst[8 * y + x] = *p++;
		}
	}
	return genhash(dst);
}

static uint64_t
hrot1(double *dst, const double *src) {
	const double *p = src;
	for (int x = 7; x >= 0; --x) {
		for (int y = 0; y < 8; ++y) {
			dst[8 * y + x] = *p++;
		}
	}
	return genhash(dst);
}

static uint64_t
hrot2(double *dst, const double *src) {
	const double *p = src;
	for (int y = 7; y >= 0; --y) {
		for (int x = 7; x >= 0; --x) {
			dst[8 * y + x] = *p++;
		}
	}
	return genhash(dst);
}

static uint64_t
hrot3(double *dst, const double *src) {
	const double *p = src;
	for (int x = 0; x < 8; ++x) {
		for (int y = 7; y >= 0; --y) {
			dst[8 * y + x] = *p++;
		}
	}
	return genhash(dst);
}

static uint8_t*
decompress_item(struct item_t *item) {
	uint8_t *data = NULL;
	tjhandle th;
	int ss, cs;

	if (!(th = tjInitDecompress())) {
		errx(1, "Unable to initialize decompressor");
	}
	if (!tjDecompressHeader3(th, item->data, item->size, &item->w, &item->h, &ss, &cs)) {
		data = emalloc(item->w * item->h * sizeof(*data));

		if (tjDecompress2(th, item->data, item->size, data, item->w, 0, item->h, TJPF_GRAY, 0) < 0) {
			data = NULL;
		}
	}

	tjDestroy(th);

	if (!data) {
		if (verbose > 1)
			warnx("failed to decompress, trying Imlib %s", item->path);
		pthread_mutex_lock(&imlock);
		data = imlib_grayscale(item->path, item->data, item->size, &item->w, &item->h);
		pthread_mutex_unlock(&imlock);
	}
	if (!data)
		warnx("Failed to read image data: %s", item->path);
	return data;
}

static int
read_item(struct item_t *item) {
	FILE *fp = NULL;
		
	if (!(fp = fopen(item->path, "rb")))
		warn("fopen %s", item->path);
	item->data = ecalloc(item->size + 1, sizeof(*item->data));
	if (fread(item->data, item->size, 1, fp) != 1)
		warn("fread %s %d", item->path, item->size);
	fclose(fp);
	return item->data ? 0 : -1;
}

static void
handle_item(void *arg) {
	double ebe_base[64];
	struct item_t *item = arg;
	uint8_t *img;

	if (read_item(item) < 0)
		return;
	if (!(img = decompress_item(item)))
		return;

	scale_down(ebe_base, img, item->w, item->h);
	free(img);
	item->valid = item->w >= 8 && item->h >= 8;

	if (!item->valid) {
		warnx("cannot handle %dx%d image %s", item->w, item->h, item->path);
		return;
	}

	if (jsondump)
		set_exif_date(item);
	
	item->hashes[TI_BASE] = genhash(ebe_base);
	if (transform) {
		double ebe_temp[64];

		if (transform & TRANS_ROTATE) {
			item->hashes[TI_ROT1] = hrot1(ebe_temp, ebe_base);
			item->hashes[TI_ROT2] = hrot2(ebe_temp, ebe_base);
			item->hashes[TI_ROT3] = hrot3(ebe_temp, ebe_base);
		}

		if (transform & TRANS_FLIP) {
			item->hashes[TI_FLIP] = hflip(ebe_temp, ebe_base);
			if (transform & TRANS_FLIP) {
				/* reuse stacked ebe_base */
				item->hashes[TI_FLR1] = hrot1(ebe_base, ebe_temp);
				item->hashes[TI_FLR2] = hrot2(ebe_base, ebe_temp);
				item->hashes[TI_FLR3] = hrot3(ebe_base, ebe_temp);
			}
		}
	}
	print_item(item);
}

static int
handle(const char *path) {
	struct stat st;
	int ret = 0;

	if (stat(path, &st) < 0) {
		warn("stat %s", path);
		return -1;
	}
	if (S_ISDIR(st.st_mode)) {
		struct dirent *dp;
		DIR *d;

		if (!(d = opendir(path))) {
			warn("opendir %s", path);
			return -1;
		}
		while ((dp = readdir(d))) {
			if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
				continue;
			char child[PATH_MAX];
			snprintf(child, sizeof(child), "%s/%s", path, dp->d_name);
			ret |= handle(child);
		}
	} else {
		if (st.st_size > maxbuf) {
			warnx("won't handle large file: %s", path);
			return -1;
		}

		struct item_t *item = ecalloc(1, sizeof(*item));
		item->path = strdup(path);
		item->size = st.st_size;
		item->mtime = st.st_mtime;
		item->eq_trans = TI_LAST;
		item->eq_dist = -1;

		item->next = head;
		head = item;

		if (nthreads > 1) {
			ret = thpool_add_work(threads, handle_item, item);
		} else {
			handle_item(item);
		}
	}
	return ret;
}


static const struct optparse_long longopts[] = {
	{ "verbose",        'v', OPTPARSE_NONE },
	{ "quiet",          'q', OPTPARSE_NONE },
	{ "raw",            'R', OPTPARSE_NONE },
	{ "threads",        'T', OPTPARSE_REQUIRED },
	{ "jsondump",       'a', OPTPARSE_NONE },
	{ "maxmegabytes",   'M', OPTPARSE_REQUIRED },
	{ "transform",      't', OPTPARSE_NONE },
	{ "rotate",         'r', OPTPARSE_NONE },
	{ "flip",           'f', OPTPARSE_NONE },
	{ "stdin",          'i', OPTPARSE_NONE },
	{ "dedup",          'd', OPTPARSE_NONE },
	{ "zsh-comp-gen", -3515, OPTPARSE_NONE },
	{ 0 },
};

static void
usage() {
	fprintf(stdout, "usage: %s [opts] <FILE [...]>\n", progname);
	optparse_dump_options(longopts);
	exit(1);
}

int
main(int argc, char **argv) {
	char jsonfile[] = "/tmp/imghash-XXXXXX";
	int ret = 0;
	int i;
	struct optparse op;
	long opt;
	bool from_stdin = false;
	bool dedup = false;

	optparse_init(&op, argv);
	while ((opt = optparse_long(&op, longopts, NULL)) != -1) {
		switch (opt) {
		case 'i':
			from_stdin = true;
			break;
		case 'd':
			dedup = true;
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

		case 't':
			transform = ~TRANS_NONE;
			break;
		case 'r':
			transform |= TRANS_ROTATE;
			break;
		case 'f':
			transform |= TRANS_FLIP;
			break;
		case 'T':
			nthreads = atoi(op.optarg);
			break;
		case 'M':
			maxbuf = atoi(op.optarg) * 1024 * 1024;
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

	if (jsondump)
		transform = ~TRANS_NONE;

	if (nthreads > 1)
		threads = thpool_init(nthreads);
	pthread_mutex_init(&prlock, NULL);
	pthread_mutex_init(&imlock, NULL);

	argv += op.optind;
	argc -= op.optind;

	if (!!argc == from_stdin)
		usage();

	if (jsondump)
		fprintf(jfp, "[");

	if (from_stdin) {
		ssize_t r;
		size_t len;
		char *buf = NULL;
		while ((r = getline(&buf, &len, stdin)) != -1) {
			if (buf[r - 1] == '\n')
				buf[r - 1] = '\0';
			ret |= handle(buf);
		}
		free(buf);
	} else {
		for (i = 0; i < argc; ++i) {
			ret |= handle(argv[i]);
		}
	}

	if (nthreads > 1)
		thpool_wait(threads);
	
	if (jsondump)
		fprintf(jfp, "\n]\n");
	if (jfp != stdout)
		fclose(jfp);
	
	while (head) {
		struct item_t *tmp = head->next;
		if (!head->valid)
			ret |= 1;
		free(head->data);
		free(head->path);
		free(head);
		head = tmp;
	}
	if (nthreads > 1)
		thpool_destroy(threads);

	if (dedup) {
		execlp("imgdups", "imgdups", "-a", jsonfile, NULL);
		return 127;
	}

	return ret;
}
