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

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <turbojpeg.h>
#include <string.h>
#include <err.h>

#include "_optparse.h"
#include "util.h"

static const char progname[] = "jpgtrim";
static int verbose = 1;
static int threshold = 26;
static int gradient = 10;
static int margin = 4;
static bool clobber = false;
static bool dry_run = false;
static const char default_oldext[] = ".0ld";
static const char *oldext = default_oldext;

static const struct optparse_long longopts[] = {
	{ "clobber",       'f', OPTPARSE_NONE },
	{ "verbose",       'v', OPTPARSE_NONE },
	{ "dry-run",       'd', OPTPARSE_NONE },
	{ "quiet",         'q', OPTPARSE_NONE },
	{ "threshold",     't', OPTPARSE_REQUIRED },
	{ "gradient",      'g', OPTPARSE_REQUIRED },
	{ "margin",        'm', OPTPARSE_REQUIRED },
	{ "oldsuffix",     'o', OPTPARSE_REQUIRED },
	{ "help",          'h', OPTPARSE_NONE },
	{ "zsh-comp-gen", -3515, OPTPARSE_NONE },
	{ 0 },
};

static void
help(void) {
	printf("usage: jpgtrim [opts] <file> ...\n");
	printf(" -f\tOverwrite files\n");
	printf(" -v|-q\tChange verbosity\n");
	printf(" -o S\tBackup suffix when not clobbering (%s)\n", default_oldext);
	printf(" -t T\tThreshold [0, 255] (%d)\n", threshold);
	printf("\tMinimum luminosity difference within a line\n");
	printf("\tfor it to not be considered a border.\n");
	printf(" -g G\tGradient threshold [0, 255] (%d)\n", gradient);
	printf("\tMaximium luminosity difference betweed ajacent\n");
	printf("\tpixels within a border line.\n");
	printf(" -m M\tMargin [0, max(w,h)] (%d)\n", margin);
	printf("\tEdges determined to be borders will be cropped\n");
	printf("\tby this many pixels beyond the computed border.\n");
	printf("\tImages will always be cropped to an integer\n");
	printf("\tmultiple of the file's JPEG block size.\n");
	printf("\tOnly jpg, others: gm convert -fuzz 90%% -trim\n");
}

static void
usage(void) {
	printf("usage: %s ...\n", progname);
	optparse_dump_options(longopts);
	exit(1);
}


static int
findborder(const uint8_t *data, int is, int ie, int os, int oe, int od, int iM, int oM) {
	int ret = margin;
	for (int o = os; o != oe; o += od) {
		int min = 1000.0;
		int max = 0;
		int di = 0;
		int prev = -1;
		for (int i = is; i < ie; ++i) {
			uint8_t v = data[o * oM + i * iM];
			if (v < min)
				min = v;
			if (v > max)
				max = v;
			if (i) {
				int delta = abs(prev - v);
				if (delta > di) {
					di = delta;
				}
			}
			prev = v;
		}
		if (di > gradient && abs(min - max) > threshold) {
			break;
		}
		++ret;
	}
	return ret;
}

static int
crop(const uint8_t *srcbuf, size_t srclen, const char *path, int x, int y, int w, int h) {
	int ret = 0;
	FILE *fp = NULL;
	size_t dstlen = 0;
	uint8_t *dstbuf = NULL;
	tjhandle th = NULL;
	char *old = NULL;

	tjtransform tf = {
		.options = TJXOPT_CROP | TJXOPT_PERFECT,
		.op = TJXOP_NONE,
		.r.x = x,	.r.w = w,
		.r.y = y,	.r.h = h,
	};

	if (!clobber) {
		old = emalloc(strlen(path) + strlen(oldext) + 1);
		sprintf(old, "%s%s", path, oldext);
		if (rename(path, old) < 0) {
			warn("cannot backup %s, skipping", path);
			free(old);
			return 1;
		}
	}

	if (!(th = tjInitTransform())) {
		err(1, "tjInitTransform");
	}
	if (tj3Transform(th, srcbuf, srclen, 1, &dstbuf, &dstlen, &tf) < 0) {
		warnx("cannot transform %s: %s", path, tjGetErrorStr2(th));
		tjDestroy(th);
		return 1;
	}
	tjDestroy(th);


	if (!(fp = fopen(path, "wb"))) {
		warn("fopen %s", path);
		ret = 1;
	} else {
		if (fwrite(dstbuf, dstlen, 1, fp) != 1) {
			warn("fwrite %s %lu", path, dstlen);
			ret = 1;
		}
	}
	if (ret != 0 && old)
		rename(old, path);
	fclose(fp);
	free(old);
	free(dstbuf);
	return ret;
}

#define CLAMPCROP(var, mod) do { 				\
	if (var % (mod) != 0) { 				\
		int old = var;					\
		var -= var % (mod);				\
	} } while(0)

static int
handle(const char *path) {
	int ret = 0;
	struct stat st;
	FILE *fp = NULL;
	uint8_t *srcbuf = NULL;
	uint8_t *data = NULL;
	tjhandle th;
	int w, h, ss, cs;

	if (!(th = tjInitDecompress())) {
		errx(1, "Unable to initialize");
	}
	if (stat(path, &st) < 0) {
		warn("stat %s", path);
		return 1;
	}
	if (!(fp = fopen(path, "rb"))) {
		warn("fopen %s", path);
		return 1;
	}
	size_t srclen = st.st_size;
	if (!(srcbuf = tjAlloc(srclen)))
		err(1, "tjAlloc %lu", srclen);
	if (fread(srcbuf, srclen, 1, fp) != 1) {
		warn("fread %s %lu", path, srclen);
		ret = 1;
		goto jpegbail;
	}
	fclose(fp);
	fp = NULL;

	if (tjDecompressHeader3(th, srcbuf, srclen, &w, &h, &ss, &cs) < 0) {
		warnx("unable to read header: %s", path);
		ret = 1;
		goto jpegbail;
	}

	data = emalloc(w * h * sizeof(*data));

	if (tjDecompress2(th, srcbuf, srclen, data, w, 0, h, TJPF_GRAY, 0) < 0) {
		warnx("unable to decompress: %s", path);
		goto jpegbail;
	}

	int mt = findborder(data, 0, w, 0,     h - 1, +1, 1, w);
	int mb = findborder(data, 0, w, h - 1, -1,    -1, 1, w);
	int ml = findborder(data, mt, h - mb, 0,     w - 1, +1, w, 1);
	int mr = findborder(data, mt, h - mb, w - 1, -1,    -1, w, 1);

	if (mt > margin || mb > margin || ml > margin || mr > margin) {
		int xmod = tjMCUWidth[ss];
		int ymod = tjMCUHeight[ss];
		int xm = ml % xmod;
		int ym = mt % ymod;
		int cx = ml + (xmod - xm);
		int cy = mt + (ymod - ym);
		int cw = w - mr - xm - cx; cw -= cw % xmod;
		int ch = h - mb - ym - cy; ch -= ch % ymod;
		bool do_crop = cx + cw <= w && cy + ch <= h;
		if (verbose > 1 || (verbose && do_crop))
			printf("%s %d l=%d t=%d r=%d b=%d (%dx%d) %dx%d+%d+%d\n",
			       path, do_crop, ml, mt, mr, mb, w, h, cw, ch, cx, cy);
		if (do_crop && !dry_run) {
			ret |= crop(srcbuf, srclen, path, cx, cy, cw, ch);
		}
	}

jpegbail:
	free(data);
	tjDestroy(th);
	tjFree(srcbuf);
	if (fp)
		fclose(fp);
	return ret;
}

int
main(int argc, char *argv[]) {
	int ret = 0;
	struct optparse op;
	long opt;

	optparse_init(&op, argv);
	while ((opt = optparse_long(&op, longopts, NULL)) != -1) {
		switch (opt) {
		case 'v':
			++verbose;
			break;
		case 'q':
			--verbose;
			break;
		case 'd':
			dry_run = true;
			break;


		case 'f':
			clobber = true;
			break;
		case 'o':
			oldext = op.optarg;
			break;
		case 't':
			threshold = atoi(op.optarg);
			break;
		case 'g':
			gradient = atoi(op.optarg);
			break;
		case 'm':
			margin = atoi(op.optarg);
			break;

		case '?':
			warnx("%s", op.errmsg);
			usage();
			break;
		case -3515:
			optparse_dump_zsh_comp(longopts, progname, "_files");
			exit(0);
		case 'h':
			help();
			exit(0);
		}
	}

	argv += op.optind;
	argc -= op.optind;
	if (!argc)
		usage();

	for (int i = 0; i < argc; ++i) {
		ret |= handle(argv[i]);
	}

	return ret ? 1 : 0;
}
