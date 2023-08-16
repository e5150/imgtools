#pragma once
#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#include <optparse.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int
optparse_dump_zsh_comp(const struct optparse_long *longopts, const char *argv0, const char *rest) {
	printf("#compdef %s\n\n_arguments \\\n", argv0);
	for (int i = 0;; ++i) {
		const struct optparse_long *opt = longopts + i;
		if (!opt->longname)
			break;
		if (!strcmp(opt->longname, "zsh-comp-gen"))
			continue;
		if (isprint(opt->shortname))
			printf("\t'-%c[%s]", opt->shortname, opt->longname);
		else
			printf("\t'--%s", opt->longname);
		if (opt->argtype == OPTPARSE_REQUIRED)
			printf(":arg:");
		printf("' \\\n");
	}
	if (rest)
		printf("\t'*:rest:%s'\n", rest);
	printf("\n");
	return 0;
}

static void
optparse_dump_options(const struct optparse_long *longopts) {
	for (int i = 0;; ++i) {
		const struct optparse_long *opt = longopts + i;
		if (!opt->longname)
			break;
		printf("\t%s%c  --%s%s\n",
		       isprint(opt->shortname) ? "-" : " ",
		       isprint(opt->shortname) ? opt->shortname : ' ',
		       opt->longname,
		       opt->argtype == OPTPARSE_OPTIONAL ? " [arg]" :
		       opt->argtype == OPTPARSE_REQUIRED ? " <arg>" : "");
	}
}

#pragma GCC diagnostic pop
