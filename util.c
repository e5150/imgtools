#include "util.h"

void *
erealloc(void *p, size_t size) {
	p = realloc(p, size);
	if (!p)
		err(1, "realloc %lu", size);
	return p;
}

void *
ecalloc(size_t nmemb, size_t size) {
	void *p = calloc(nmemb, size);
	if (!p)
		err(1, "calloc %lu * %lu", nmemb, size);
	return p;
}

void *
emalloc(size_t size) {
	void *p = malloc(size);
	if (!p)
		err(1, "malloc %lu", size);
	return p;
}
