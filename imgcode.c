#include "imgcode.h"
#include "util.h"

uint8_t *
imlib_grayscale(const char *path, const uint8_t *srcbuf, size_t srclen, int *w, int *h) {
	Imlib_Image im;
	uint8_t *data = NULL;

	if (!(im = imlib_load_image_mem(path, srcbuf, srclen))) {
		return NULL;
	}

	imlib_context_set_image(im);
	*w = imlib_image_get_width(),
	*h = imlib_image_get_height();

	data = emalloc(*w * *h * sizeof(*data));
	uint8_t *p = data;

	for (int y = 0; y < *h; y++)
	for (int x = 0; x < *w; x++) {
		Imlib_Color color;
		imlib_image_query_pixel(x, y, &color);
		*p++ = .30 * color.red
		     + .58 * color.green
		     + .12 * color.blue;
	}
	imlib_free_image();
	return data;
}
