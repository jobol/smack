/*
 * chsmack - Set smack attributes on files
 *
 * Copyright (C) 2011 Nokia Corporation.
 * Copyright (C) 2011, 2012, 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <sys/smack.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <libgen.h>
#include <dirent.h>

#include "config.h"

#define OC_VERSION        'v'
#define OC_HELP           'h'
#define OC_SET_ACCESS     'a'
#define OC_SET_EXEC       'e'
#define OC_SET_MMAP       'm'
#define OC_SET_TRANSMUTE  't'
#define OC_DROP_ACCESS    'A'
#define OC_DROP_EXEC      'E'
#define OC_DROP_MMAP      'M'
#define OC_DROP_TRANSMUTE 'T'
#define OC_DROP_OTHERS    'D'
#define OC_DEREFERENCE    'L'
#define OC_RECURSIVE      'r'
#define OC_NAME_ONLY      'n'
#define OC_IF_ACCESS      '\001'
#define OC_IF_EXEC        '\002'
#define OC_IF_MMAP        '\003'
#define OC_IF_TRANSMUTE   '\004'
#define OC_IF_NO_ACCESS   '\005'
#define OC_IF_NO_EXEC     '\006'
#define OC_IF_NO_MMAP     '\007'
#define OC_IF_NO_TRANSMUTE '\010'

static const char usage[] =
	"Usage: %s [options] <path>\n"
	"Options:\n"  
	" -v --version         output version information and exit\n"
	" -h --help            output usage information and exit\n"
	" -a --access VALUE    set "XATTR_NAME_SMACK" to VALUE\n"
	" -e --exec VALUE      set "XATTR_NAME_SMACKEXEC" to VALUE\n"
	" -m --mmap VALUE      set "XATTR_NAME_SMACKMMAP" to VALUE\n"
	" -t --transmute       set "XATTR_NAME_SMACKTRANSMUTE"\n"
	" -L --dereference     tell to follow the symbolic links\n"
	" -D --drop            remove unset attributes\n"
	" -A --drop-access     remove "XATTR_NAME_SMACK"\n"
	" -E --drop-exec       remove "XATTR_NAME_SMACKEXEC"\n"
	" -M --drop-mmap       remove "XATTR_NAME_SMACKMMAP"\n"
	" -T --drop-transmute  remove "XATTR_NAME_SMACKTRANSMUTE"\n"
	" -r --recursive       list or modify also files in subdirectories\n"
	" -n --name-only       don't print attributes\n"
	"    --if-access VALUE apply if access is value\n"
	"    --if-exec VALUE   apply if exec is value\n"
	"    --if-mmap VALUE   apply if mmap is value\n"
	"    --if-transmute    apply if transmuting\n"
	"    --if-no-access    apply if access is not set\n"
	"    --if-no-exec      apply if exec is not set\n"
	"    --if-no-mmap      apply if mmap is not set\n"
	"    --if-no-transmute apply if not transmuting\n"
;

static const char shortoptions[] = "vha:e:m:tLDAEMTrn";
static struct option options[] = {
	{"version", no_argument, 0, OC_VERSION},
	{"help", no_argument, 0, OC_HELP},
	{"access", required_argument, 0, OC_SET_ACCESS},
	{"exec", required_argument, 0, OC_SET_EXEC},
	{"mmap", required_argument, 0, OC_SET_MMAP},
	{"transmute", no_argument, 0, OC_SET_TRANSMUTE},
	{"dereference", no_argument, 0, OC_DEREFERENCE},
	{"drop", no_argument, 0, OC_DROP_OTHERS},
	{"drop-access", no_argument, 0, OC_DROP_ACCESS},
	{"drop-exec", no_argument, 0, OC_DROP_EXEC},
	{"drop-mmap", no_argument, 0, OC_DROP_MMAP},
	{"drop-transmute", no_argument, 0, OC_DROP_TRANSMUTE},
	{"recursive", no_argument, 0, OC_RECURSIVE},
	{"name-only", no_argument, 0, OC_NAME_ONLY},
	{"if-access", required_argument, 0, OC_IF_ACCESS},
	{"if-exec", required_argument, 0, OC_IF_EXEC},
	{"if-mmap", required_argument, 0, OC_IF_MMAP},
	{"if-transmute", no_argument, 0, OC_IF_TRANSMUTE},
	{"if-no-access", no_argument, 0, OC_IF_NO_ACCESS},
	{"if-no-exec", no_argument, 0, OC_IF_NO_EXEC},
	{"if-no-mmap", no_argument, 0, OC_IF_NO_MMAP},
	{"if-no-transmute", no_argument, 0, OC_IF_NO_TRANSMUTE},
	{NULL, 0, 0, 0}
};

/* enumeration for state of flags */
enum state {
	unset    = 0,
	positive = 1,
	negative = 2
};

/* structure for recording options of label and their init */
struct labelset {
	enum state isset;  /* how is it set */
	const char *value; /* value of the option set if any or NULL else */
};

static struct labelset access_set = { unset, NULL }; /* for option "access" */
static struct labelset exec_set = { unset, NULL }; /* for option "exec" */
static struct labelset mmap_set = { unset, NULL }; /* for option "mmap" */
static enum state transmute_flag = unset; /* for option "transmute" */
static enum state follow_flag = unset; /* for option "dereference" */
static enum state recursive_flag = unset; /* for option "recursive" */

static struct labelset if_access = { unset, NULL }; /* for option "if-access" */
static struct labelset if_exec = { unset, NULL }; /* for option "if-exec" */
static struct labelset if_mmap = { unset, NULL }; /* for option "if-mmap" */
static enum state if_transmute = unset; /* for option "if-transmute" */
static enum state name_only_flag = unset; /* for option "name-only" */

/* get the option for the given char */
static struct option *option_by_char(int car)
{
	struct option *result = options;
	while (result->name != NULL && result->val != car)
		result++;
	return result;
}

/* get a string describing the option for the given char */
static const char *describe_option(int car)
{
	static char buffer[50];
	struct option *opt = option_by_char(car);
	snprintf(buffer, sizeof buffer,
			opt->val > ' ' ? "--%s (or -%c)" : "--%s",
			opt->name, opt->val);
	return buffer;
}

/* check if prop matches the condition */
static int test_prop(const char *path, enum state flag, const char *value,
		     const char *attr)
{
	int rc;
	char *label;

	if (flag == unset)
		rc = 1;
	else {
		rc = smack_new_label_from_path(path, attr, follow_flag,
						&label);
		if (rc < 0)
			rc = flag == negative;
		else {
			rc = value != NULL
			  && !strcmp(label, value) == (flag == positive);
			free(label);
		}
	}
	return rc;
}

/* check if path matches the select conditions */
static int test_if_selected(const char *path)
{
	return test_prop(path,
			 if_access.isset, if_access.value, XATTR_NAME_SMACK)
	    && test_prop(path,
			     if_exec.isset, if_exec.value, XATTR_NAME_SMACKEXEC)
	    && test_prop(path,
			     if_mmap.isset, if_mmap.value, XATTR_NAME_SMACKMMAP)
	    && test_prop(path,
			     transmute_flag, "TRUE", XATTR_NAME_SMACKTRANSMUTE);
}

/* modify attributes of a file */
static void modify_prop(const char *path, struct labelset *ls, const char *attr)
{
	int rc;
	switch (ls->isset) {
	case positive:
		rc = smack_set_label_for_path(path, attr, follow_flag,
					      ls->value);
		if (rc < 0)
			perror(path);
		break;
	case negative:
		rc = smack_remove_label_for_path(path, attr, follow_flag);
		if (rc < 0 && errno != ENODATA)
			perror(path);
		break;
	}
}

/* modify transmutation of a directory */
static void modify_transmute(const char *path)
{
	struct stat st;
	int rc;
	switch (transmute_flag) {
	case positive:
		rc = follow_flag ? stat(path, &st) : lstat(path, &st);
		if (rc < 0)
			perror(path);
		else if (!S_ISDIR(st.st_mode)) {
			if (!recursive_flag) {
				fprintf(stderr,
					"%s: transmute: not a directory\n",
					path);
			}
		} else {
			rc = smack_set_label_for_path(path,
						      XATTR_NAME_SMACKTRANSMUTE,
						      follow_flag, "TRUE");
			if (rc < 0)
				perror(path);
		}
		break;
	case negative:
		rc = smack_remove_label_for_path(path,
						 XATTR_NAME_SMACKTRANSMUTE,
						 follow_flag);
		if (rc < 0 && errno != ENODATA)
			perror(path);
		break;
	}
}

/* modify the file (or directory) of path */
static void modify_file(const char *path)
{
	if (!test_if_selected(path))
		return;

	modify_prop(path, &access_set, XATTR_NAME_SMACK);
	modify_prop(path, &exec_set, XATTR_NAME_SMACKEXEC);
	modify_prop(path, &mmap_set, XATTR_NAME_SMACKMMAP);
	modify_transmute(path);
}

/* print the file (or directory) of path */
static void print_file(const char *path)
{
	ssize_t rc;
	char *label;
	int has_some_smack = 0;

	if (!test_if_selected(path))
		return;

	/* Print file path. */
	printf("%s", path);

	if (name_only_flag) {
		printf("\n");
		return;
	}

	errno = 0;
	rc = smack_new_label_from_path(path, XATTR_NAME_SMACK, follow_flag,
				       &label);
	if (rc > 0) {
		printf(" access=\"%s\"", label);
		free(label);
		has_some_smack = 1;
	}

	rc = smack_new_label_from_path(path, XATTR_NAME_SMACKEXEC, follow_flag,
				       &label);
	if (rc > 0) {
		printf(" execute=\"%s\"", label);
		free(label);
		has_some_smack = 1;
	}

	rc = smack_new_label_from_path(path, XATTR_NAME_SMACKMMAP, follow_flag,
				       &label);
	if (rc > 0) {
		printf(" mmap=\"%s\"", label);
		free(label);
		has_some_smack = 1;
	}

	rc = smack_new_label_from_path(path, XATTR_NAME_SMACKTRANSMUTE,
				       follow_flag, &label);
	if (rc > 0) {
		printf(" transmute=\"%s\"", label);
		free(label);
		has_some_smack = 1;
	}

	printf(has_some_smack ? "\n" : ": No smack property found\n");
}

static void explore(const char *path, void (*fun)(const char*), int follow)
{
	struct stat st;
	int rc;
	char *buf;
	size_t buf_size, dir_name_len, file_name_len;
	DIR *dir;
	struct dirent *dent;

	/* type of the path */
	rc = (follow ? stat : lstat)(path ? path : ".", &st);
	if (rc < 0) {
		perror(path);
		return;
	}

	/* no a directory, skip */
	if (!S_ISDIR(st.st_mode))
		return;

	/* open the directory */
	dir = opendir(path ? path : ".");
	if (dir == NULL) {
		perror(path);
		return;
	}

	/* iterate ove the directory's entries */
	dir_name_len = path ? strlen(path) : 1;

	/* dir_name_len + sizeof('/') + sizeof(dirent.d_name) + sizeof('\0') */
	buf_size = dir_name_len + 1 + 256 + 1;
	buf = malloc(buf_size);
	if (buf == NULL) {
		fprintf(stderr, "error: out of memory.\n");
		exit(1);
	}

	if (path) {
		memcpy(buf, path, dir_name_len);
		while (dir_name_len && buf[dir_name_len - 1] == '/')
			dir_name_len--;
		buf[dir_name_len] = '/';
	} else {
		buf[0] = '.';
		buf[1] = '/';
	}

	for (;;) {
		errno = 0;
		dent = readdir(dir);
		if (dent == NULL) {
			if (errno)
				fprintf(stderr,
					"error: while scaning directory '%s'.\n",
					path ? path : ".");
			free(buf);
			closedir(dir);
			return;
		}
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
			continue;

		file_name_len = strlen(dent->d_name);
		if (dir_name_len + 1 + file_name_len + 1 > buf_size) {
			char *tmp;

			buf_size = dir_name_len + 1 + file_name_len + 1;
			tmp = realloc(buf, buf_size);
			if (tmp == NULL) {
				fprintf(stderr, "error: out of memory.\n");
				exit(1);
			}
			buf = tmp;
		}

		memcpy(buf + dir_name_len + 1, dent->d_name, file_name_len + 1);
		fun(buf);
		if (recursive_flag)
			explore(buf, fun, follow);
	}
}

/* set the state to to */
static void set_state(enum state *to, enum state value, int car, int fatal)
{
	if (*to == unset)
		*to = value;
	else if (*to == value) {
		fprintf(stderr, "%s, option %s already set.\n",
			fatal ? "error" : "warning",
			describe_option(car));
		if (fatal)
			exit(1);
	} else {
		fprintf(stderr, "error, option %s opposite to an "
			"option already set.\n",
			describe_option(car));
		exit(1);
	}
}

/* set the label to value */
static void set_label(struct labelset *label, const char *value, int car)
{
	if (strnlen(value, SMACK_LABEL_LEN + 1) == SMACK_LABEL_LEN + 1) {
		fprintf(stderr, "error option %s: \"%s\" exceeds %d characters.\n",
			describe_option(car), value, SMACK_LABEL_LEN);
		exit(1);
	} else if (smack_label_length(value) < 0) {
		fprintf(stderr, "error option %s: invalid Smack label '%s'.\n",
			describe_option(car), value);
		exit(1);
	}

	set_state(&label->isset, positive, car, 1);
	label->value = value;
}

/* set the label to condition value */
static void set_if_label(struct labelset *label, const char *value, int car)
{
	enum state flag;

	if (value[0] != '/')
		flag = positive;
	else {
		flag = negative;
		value++;
	}
	if (strnlen(value, SMACK_LABEL_LEN + 1) == SMACK_LABEL_LEN + 1) {
		fprintf(stderr, "error option %s: \"%s\" exceeds %d characters.\n",
			describe_option(car), value, SMACK_LABEL_LEN);
		exit(1);
	} else if (smack_label_length(value) < 0) {
		fprintf(stderr, "error option %s: invalid Smack label '%s'.\n",
			describe_option(car), value);
		exit(1);
	}

	set_state(&label->isset, flag, car, 1);
	label->value = value;
}

/* main */
int main(int argc, char *argv[])
{
	struct labelset *labelset;

	void (*fun)(const char*);
	enum state delete_flag = unset;
	enum state svalue;
	int modify = 0;
	int rc;
	int c;
	int i;

	/* scan options */
	while ((c = getopt_long(argc, argv, shortoptions, options, NULL)) != -1) {

		switch (c) {
		case OC_SET_ACCESS:
			set_label(&access_set, optarg, c);
			modify = 1;
			break;
		case OC_SET_EXEC:
			set_label(&exec_set, optarg, c);
			modify = 1;
			break;
		case OC_SET_MMAP:
			set_label(&mmap_set, optarg, c);
			modify = 1;
			break;
		case OC_DROP_ACCESS:
			set_state(&access_set.isset, negative, c, 0);
			modify = 1;
			break;
		case OC_DROP_EXEC:
			set_state(&exec_set.isset, negative, c, 0);
			modify = 1;
			break;
		case OC_DROP_MMAP:
			set_state(&mmap_set.isset, negative, c, 0);
			modify = 1;
			break;
		case OC_DROP_TRANSMUTE:
			set_state(&transmute_flag, negative, c, 0);
			modify = 1;
			break;
		case OC_SET_TRANSMUTE:
			set_state(&transmute_flag, positive, c, 0);
			modify = 1;
			break;
		case OC_DROP_OTHERS:
			set_state(&delete_flag, negative, c, 0);
			break;
		case OC_DEREFERENCE:
			set_state(&follow_flag, positive, c, 0);
			break;
		case OC_RECURSIVE:
			set_state(&recursive_flag, positive, c, 0);
			break;
		case OC_NAME_ONLY:
			set_state(&name_only_flag, positive, c, 0);
			break;
		case OC_IF_ACCESS:
			set_if_label(&if_access, optarg, c);
			break;
		case OC_IF_EXEC:
			set_if_label(&if_exec, optarg, c);
			break;
		case OC_IF_MMAP:
			set_if_label(&if_mmap, optarg, c);
			break;
		case OC_IF_TRANSMUTE:
			set_state(&if_transmute, positive, c, 0);
			break;
		case OC_IF_NO_ACCESS:
			set_state(&if_access.isset, negative, c, 0);
			break;
		case OC_IF_NO_EXEC:
			set_state(&if_exec.isset, negative, c, 0);
			break;
		case OC_IF_NO_MMAP:
			set_state(&if_mmap.isset, negative, c, 0);
			break;
		case OC_IF_NO_TRANSMUTE:
			set_state(&if_transmute, negative, c, 0);
			break;
		case OC_VERSION:
			printf("%s (libsmack) version " PACKAGE_VERSION "\n",
			       basename(argv[0]));
			exit(0);
		case OC_HELP:
			printf(usage, basename(argv[0]));
			exit(0);
		default:
			printf(usage, basename(argv[0]));
			exit(1);
		}
	}

	/* update states */
	if (delete_flag == negative) {
		/* remove unset attributes */
		if (access_set.isset == unset)
			access_set.isset = negative;
		if (exec_set.isset == unset)
			exec_set.isset = negative;
		if (mmap_set.isset == unset)
			mmap_set.isset = negative;
		if (transmute_flag == unset)
			transmute_flag = negative;
		modify = 1;
	}

	/* process */
	fun = modify ? modify_file : print_file;
	if (optind == argc) {
		if (recursive_flag == unset) {
			fprintf(stderr, "error: no files.\n");
			exit(1);
		}
		explore(NULL, fun, 0);
	} else {
		for (i = optind; i < argc; i++) {
			fun(argv[i]);
			if (recursive_flag)
				explore(argv[i], fun, 1);
		}
	}
	exit(0);
}
