/*
 * Copyright (C) 2009 Vincent Hanquez <vincent@snarc.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 or version 3.0 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <locale.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>

#include "json.h"
#include "deque.h"
#include "jsonlint.h"

char *indent_string = NULL;

char *string_of_errors[] =
{
	[JSON_ERROR_NO_MEMORY] = "out of memory",
	[JSON_ERROR_BAD_CHAR] = "bad character",
	[JSON_ERROR_POP_EMPTY] = "stack empty",
	[JSON_ERROR_POP_UNEXPECTED_MODE] = "pop unexpected mode",
	[JSON_ERROR_NESTING_LIMIT] = "nesting limit",
	[JSON_ERROR_DATA_LIMIT] = "data limit",
	[JSON_ERROR_COMMENT_NOT_ALLOWED] = "comment not allowed by config",
	[JSON_ERROR_UNEXPECTED_CHAR] = "unexpected char",
	[JSON_ERROR_UNICODE_MISSING_LOW_SURROGATE] = "missing unicode low surrogate",
	[JSON_ERROR_UNICODE_UNEXPECTED_LOW_SURROGATE] = "unexpected unicode low surrogate",
	[JSON_ERROR_COMMA_OUT_OF_STRUCTURE] = "error comma out of structure",
	[JSON_ERROR_CALLBACK] = "error in a callback"
};

static int printchannel(void *userdata, const char *data, uint32_t length)
{
	FILE *channel = userdata;
	int ret;
	/* should check return value */
	ret = fwrite(data, length, 1, channel);
	return 0;
}

static int prettyprint(void *userdata, int type, const char *data, uint32_t length)
{
	json_printer *printer = userdata;
	
	return json_print_pretty(printer, type, data, length);
}

FILE *open_filename(const char *filename, const char *opt, int is_input)
{
	FILE *input;
	if (strcmp(filename, "-") == 0)
		input = (is_input) ? stdin : stdout;
	else {
		input = fopen(filename, opt);
		if (!input) {
			fprintf(stderr, "error: cannot open %s: %s", filename, strerror(errno));
			return NULL;
		}
	}
	return input;
}

void close_filename(const char *filename, FILE *file)
{
	if (strcmp(filename, "-") != 0)
		fclose(file);
}

int process_file(json_parser *parser, FILE *input, int *retlines, int *retcols)
{
	char buffer[4096];
	int ret = 0;
	int32_t read;
	int lines, col, i;

	lines = 1;
	col = 0;
	while (1) {
		uint32_t processed;
		read = fread(buffer, 1, 4096, input);
		if (read <= 0)
			break;
		ret = json_parser_string(parser, buffer, read, &processed);
		for (i = 0; i < processed; i++) {
			if (buffer[i] == '\n') { col = 0; lines++; } else col++;
		}
		if (ret)
			break;
	}
	if (retlines) *retlines = lines;
	if (retcols) *retcols = col;
	return ret;
}

static int do_verify(json_config *config, const char *filename)
{
	FILE *input;
	json_parser parser;
	int ret;

	input = open_filename(filename, "r", 1);
	if (!input)
		return 2;

	/* initialize the parser structure. we don't need a callback in verify */
	ret = json_parser_init(&parser, config, NULL, NULL);
	if (ret) {
		fprintf(stderr, "error: initializing parser failed (code=%d): %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = process_file(&parser, input, NULL, NULL);
	if (ret)
		return 1;

	ret = json_parser_is_done(&parser);
	if (!ret)
		return 1;
	
	close_filename(filename, input);
	return 0;
}

static int do_parse(json_config *config, const char *filename)
{
	FILE *input;
	json_parser parser;
	int ret;
	int col, lines;

	input = open_filename(filename, "r", 1);
	if (!input)
		return 2;

	/* initialize the parser structure. we don't need a callback in verify */
	ret = json_parser_init(&parser, config, NULL, NULL);
	if (ret) {
		fprintf(stderr, "error: initializing parser failed (code=%d): %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = process_file(&parser, input, &lines, &col);
	if (ret) {
		fprintf(stderr, "line %d, col %d: [code=%d] %s\n",
		        lines, col, ret, string_of_errors[ret]);
		return 1;
	}

	ret = json_parser_is_done(&parser);
	if (!ret) {
		fprintf(stderr, "syntax error\n");
		return 1;
	}
	
	close_filename(filename, input);
	return 0;
}

static int do_format(json_config *config, const char *filename, const char *outputfile)
{
	FILE *input, *output;
	json_parser parser;
	json_printer printer;
	int ret;
	int col, lines;

	input = open_filename(filename, "r", 1);
	if (!input)
		return 2;

	output = open_filename(outputfile, "a+", 0);
	if (!output)
		return 2;

	/* initialize printer and parser structures */
	ret = json_print_init(&printer, printchannel, stdout);
	if (ret) {
		fprintf(stderr, "error: initializing printer failed: [code=%d] %s\n", ret, string_of_errors[ret]);
		return ret;
	}
	if (indent_string)
		printer.indentstr = indent_string;

	ret = json_parser_init(&parser, config, &prettyprint, &printer);
	if (ret) {
		fprintf(stderr, "error: initializing parser failed: [code=%d] %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = process_file(&parser, input, &lines, &col);
	if (ret) {
		fprintf(stderr, "line %d, col %d: [code=%d] %s\n",
		        lines, col, ret, string_of_errors[ret]);
		return 1;
	}

	ret = json_parser_is_done(&parser);
	if (!ret) {
		fprintf(stderr, "syntax error\n");
		return 1;
	}

	/* cleanup */
	json_parser_free(&parser);
	json_print_free(&printer);
	fwrite("\n", 1, 1, stdout);
	close_filename(filename, input);
	return 0;
}


struct json_val_elem {
	char *key;
	uint32_t key_length;
	struct json_val *val;
};

typedef struct json_val {
	int type;
	int length;
	union {
		char *data;
		struct json_val **array;
		struct json_val_elem **object;
	} u;
} json_val_t;

static void *tree_create_structure(int nesting, int is_object)
{
	json_val_t *v = malloc(sizeof(json_val_t));
	if (v) {
		/* instead of defining a new enum type, we abuse the
		 * meaning of the json enum type for array and object */
		if (is_object) {
			v->type = JSON_OBJECT_BEGIN;
			v->u.object = NULL;
		} else {
			v->type = JSON_ARRAY_BEGIN;
			v->u.array = NULL;
		}
		v->length = 0;
	}
	return v;
}

static char *memalloc_copy_length(const char *src, uint32_t n)
{
	char *dest;

	dest = calloc(n + 1, sizeof(char));
	if (dest)
		memcpy(dest, src, n);
	return dest;
}

static void *tree_create_data(int type, const char *data, uint32_t length)
{
	json_val_t *v;

	v = malloc(sizeof(json_val_t));
	if (v) {
		v->type = type;
		v->length = length;
		v->u.data = memalloc_copy_length(data, length);
		if (!v->u.data) {
			free(v);
			return NULL;
		}
	}
	return v;
}

static int tree_append(void *structure, char *key, uint32_t key_length, void *obj)
{
	json_val_t *parent = structure;
	if (key) {
		struct json_val_elem *objelem;

		if (parent->length == 0) {
			parent->u.object = calloc(1 + 1, sizeof(json_val_t *)); /* +1 for null */
			if (!parent->u.object)
				return 1;
		} else {
			uint32_t newsize = parent->length + 1 + 1; /* +1 for null */
			void *newptr;

			newptr = realloc(parent->u.object, newsize * sizeof(json_val_t *));
			if (!newptr)
				return -1;
			parent->u.object = newptr;
		}

		objelem = malloc(sizeof(struct json_val_elem));
		if (!objelem)
			return -1;

		objelem->key = memalloc_copy_length(key, key_length);
		objelem->key_length = key_length;
		objelem->val = obj;
		parent->u.object[parent->length++] = objelem;
		parent->u.object[parent->length] = NULL;
	} else {
		if (parent->length == 0) {
			parent->u.array = calloc(1 + 1, sizeof(json_val_t *)); /* +1 for null */
			if (!parent->u.array)
				return 1;
		} else {
			uint32_t newsize = parent->length + 1 + 1; /* +1 for null */
			void *newptr;

			newptr = realloc(parent->u.object, newsize * sizeof(json_val_t *));
			if (!newptr)
				return -1;
			parent->u.array = newptr;
		}
		parent->u.array[parent->length++] = obj;
		parent->u.array[parent->length] = NULL;
	}
	return 0;
}

static int do_tree(json_config *config, const char *filename, json_val_t **root_structure)
{
	FILE *input;
	json_parser parser;
	json_parser_dom dom;
	int ret;
	int col, lines;

	input = open_filename(filename, "r", 1);
	if (!input)
		return 2;

	ret = json_parser_dom_init(&dom, tree_create_structure, tree_create_data, tree_append);
	if (ret) {
		fprintf(stderr, "error: initializing helper failed: [code=%d] %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = json_parser_init(&parser, config, json_parser_dom_callback, &dom);
	if (ret) {
		fprintf(stderr, "error: initializing parser failed: [code=%d] %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = process_file(&parser, input, &lines, &col);
	if (ret) {
		fprintf(stderr, "line %d, col %d: [code=%d] %s\n",
		        lines, col, ret, string_of_errors[ret]);

		return 1;
	}

	ret = json_parser_is_done(&parser);
	if (!ret) {
		fprintf(stderr, "syntax error\n");
		return 1;
	}

	if (root_structure)
		*root_structure = dom.root_structure;

	/* cleanup */
	json_parser_free(&parser);
	close_filename(filename, input);
	return 0;
}

static int print_tree_iter(json_val_t *element, FILE *output)
{
	int i;
	if (!element) {
		fprintf(stderr, "error: no element in print tree\n");
		return -1;
	}

	switch (element->type) {
	case JSON_OBJECT_BEGIN:
		fprintf(output, "object begin (%d element)\n", element->length);
		for (i = 0; i < element->length; i++) {
			fprintf(output, "key: %s\n", element->u.object[i]->key);
			print_tree_iter(element->u.object[i]->val, output);
		}
		fprintf(output, "object end\n");
		break;
	case JSON_ARRAY_BEGIN:
		fprintf(output, "array begin\n");
		for (i = 0; i < element->length; i++) {
			print_tree_iter(element->u.array[i], output);
		}
		fprintf(output, "array end\n");
		break;
	case JSON_FALSE:
	case JSON_TRUE:
	case JSON_NULL:
		fprintf(output, "constant\n");
		break;
	case JSON_INT:
		fprintf(output, "integer: %s\n", element->u.data);
		break;
	case JSON_STRING:
		fprintf(output, "string: %s\n", element->u.data);
		break;
	case JSON_FLOAT:
		fprintf(output, "float: %s\n", element->u.data);
		break;
	default:
		break;
	}
	return 0;
}

static int print_tree(json_val_t *root_structure, char *outputfile)
{
	FILE *output;

	output = open_filename(outputfile, "a+", 0);
	if (!output)
		return 2;
	print_tree_iter(root_structure, output);
	close_filename(outputfile, output);
	return 0;
}

static time_t	time_from_string(char* str)
{
	time_t ret = 0;
	
	//"2013-07-22 18:00:00"
	struct tm temp = {0};
	int year 	= 0;
	int month 	= 0;
	int day		= 0;
	int hour	= 0;
	int minute	= 0;
	int second 	= 0;	
	sscanf(str, "%04d-%02d-%02d %02d:%02d:%02d", &year, &month, &day, &hour, &minute, &second);
	temp.tm_sec		= second;
	temp.tm_min 	= minute;
	temp.tm_hour	= hour;
	temp.tm_mday	= day;
	temp.tm_mon		= month - 1;
	temp.tm_year 	= year - 1900;

	ret = mktime(&temp);

	return ret;
}

static int read_tree_time(PLAY_STATISTICS_T* statp, json_val_t *element)
{
	int ret = -1;

	char time_str[MAX_TIME_LEN];
	
	if (!element) 
	{
		fprintf(stderr, "%s: error, no element\n", __FUNCTION__);
		return -1;
	}
	
	switch (element->type) 
	{
	case JSON_STRING:
		strcpy(time_str, element->u.data);	
		statp->start_time = time_from_string(time_str);
		fprintf(stdout, "%s: %ld\n", __FUNCTION__, statp->start_time);
		ret = 0;
		break;
	default:
		fprintf(stderr, "%s: wierd, type=%d, %s\n", __FUNCTION__, element->type, element->u.data);
		break;
	}

	return ret;
}

static int read_tree_playnum(HASHID_STATISTICS_T* hashidp, json_val_t *element)
{
	int ret = -1;	
	if (!element) 
	{
		fprintf(stderr, "%s: error, no element\n", __FUNCTION__);
		return -1;
	}
	
	switch (element->type) 
	{
	case JSON_STRING:		
		hashidp->play_num = atol(element->u.data);
		//fprintf(stdout, "%s: json_playnum=%s\n", __FUNCTION__, json_playnum);
		ret = 0;
		break;
	case JSON_INT:		
		hashidp->play_num = atol(element->u.data);
		//fprintf(stdout, "%s: json_playnum=%s\n", __FUNCTION__, json_playnum);
		ret = 0;
		break;
	default:
		fprintf(stderr, "%s: wierd, type=%d, %s\n", __FUNCTION__, element->type, element->u.data);
		break;
	}

	return ret;
}

static int read_tree_hashid(HASHID_STATISTICS_T* hashidp, json_val_t *element)
{
	int ret = -1;
	int i = 0;
	if (!element) 
	{
		fprintf(stderr, "%s: error, no element\n", __FUNCTION__);
		return -1;
	}
	
	switch (element->type) 
	{
	case JSON_OBJECT_BEGIN:			
		for (i = 0; i < element->length; i++) 
		{
			//fprintf(stdout, "%s: key[%d/%d]=%s\n", __FUNCTION__, i, element->length, element->u.object[i]->key);			
			strcpy(hashidp->hash_id, element->u.object[i]->key);
			ret = read_tree_playnum(hashidp, element->u.object[i]->val);
		}
		break;	
	default:
		fprintf(stderr, "%s: wierd, type=%d, %s\n", __FUNCTION__, element->type, element->u.data);
		break;
	}

	return ret;
}

static int read_tree_hashids(AREA_STATISTICS_T* areap, json_val_t *element)
{
	int ret = -1;
	int i = 0;
	if (!element) 
	{
		fprintf(stderr, "%s: error, no element\n", __FUNCTION__);
		return -1;
	}
	
	switch (element->type) 
	{
	case JSON_ARRAY_BEGIN:		
		for (i = 0; i < element->length; i++) 
		{
			//fprintf(stdout, "%s: array[%d/%d]\n", __FUNCTION__, i, element->length);
			DEQUE_NODE* nodep = (DEQUE_NODE*)malloc(sizeof(DEQUE_NODE));
			if(nodep == NULL)
			{
				break;
			}
			memset(nodep, 0, sizeof(DEQUE_NODE));
			
			HASHID_STATISTICS_T* hashidp = (HASHID_STATISTICS_T*)malloc(sizeof(HASHID_STATISTICS_T));
			if(hashidp == NULL)
			{
				free(nodep);
				break;
			}
			memset(hashidp, 0, sizeof(HASHID_STATISTICS_T));
			nodep->datap = hashidp;
			
			ret = read_tree_hashid(hashidp, element->u.array[i]);

			areap->hashid_list = deque_append(areap->hashid_list, nodep);
			
		}		
		break;
	default:
		fprintf(stderr, "%s: wierd, type=%d, %s\n", __FUNCTION__, element->type, element->u.data);
		break;
	}

	return ret;
}


static int read_tree_area(AREA_STATISTICS_T* areap, json_val_t *element)
{
	int ret  = -1;
	int i = 0;
	if (!element) 
	{
		fprintf(stderr, "%s: error, no element\n", __FUNCTION__);
		return -1;
	}
	
	switch (element->type) 
	{
	case JSON_OBJECT_BEGIN:			
		for (i = 0; i < element->length; i++) 
		{
			//fprintf(stdout, "%s: key[%d/%d]=%s\n", __FUNCTION__, i, element->length, element->u.object[i]->key);			
			areap->area_id = atoi(element->u.object[i]->key);			
			ret = read_tree_hashids(areap, element->u.object[i]->val);
		}
		break;	
	default:
		fprintf(stderr, "%s: wierd, type=%d, %s\n", __FUNCTION__, element->type, element->u.data);
		break;
	}

	return ret;
}


static int read_tree_platform(PLAY_STATISTICS_T* statp, json_val_t *element, int platform)
{
	int ret = -1;
	int i = 0;
	
	if (!element) 
	{
		fprintf(stderr, "%s: error, no element\n", __FUNCTION__);
		return -1;
	}
	
	switch (element->type) 
	{
	case JSON_ARRAY_BEGIN:		
		for (i = 0; i < element->length; i++) 
		{
			//fprintf(stdout, "%s: array[%d/%d]\n", __FUNCTION__,  i,  element->length);
			DEQUE_NODE* nodep = (DEQUE_NODE*)malloc(sizeof(DEQUE_NODE));
			if(nodep == NULL)
			{
				break;
			}
			memset(nodep, 0, sizeof(DEQUE_NODE));
			
			AREA_STATISTICS_T* areap = (AREA_STATISTICS_T*)malloc(sizeof(AREA_STATISTICS_T));
			if(areap == NULL)
			{
				free(nodep);
				break;
			}
			memset(areap, 0, sizeof(AREA_STATISTICS_T));
			nodep->datap = areap;
			
			ret = read_tree_area(areap, element->u.array[i]);

			if(platform == PLATFORM_PC)
			{
				statp->pc_list = deque_append(statp->pc_list, nodep);
			}
			else if(platform == PLATFORM_MOBILE)
			{
				statp->mobile_list = deque_append(statp->mobile_list, nodep);
			}

		}		
		break;
	default:
		fprintf(stderr, "%s: wierd, type=%d, %s\n", __FUNCTION__, element->type, element->u.data);
		break;
	}

	return ret;
}


static int read_tree_root(PLAY_STATISTICS_T* statp, json_val_t *element)
{
	int ret = -1;
	int i = 0;
	if (!element) 
	{
		fprintf(stderr, "error: no element in print tree\n");
		return -1;
	}

	switch (element->type) 
	{
	case JSON_OBJECT_BEGIN:		
		for (i = 0; i < element->length; i++) 
		{
			//fprintf(stdout, "%s: key[%d/%d]=%s\n", __FUNCTION__, i, element->length, element->u.object[i]->key);
			if(strcmp(element->u.object[i]->key, "time") == 0)
			{
				ret = read_tree_time(statp, element->u.object[i]->val);
			}
			else if(strcmp(element->u.object[i]->key, "pc") == 0)
			{
				ret = read_tree_platform(statp, element->u.object[i]->val, PLATFORM_PC);
			}
			else if(strcmp(element->u.object[i]->key, "mobile") == 0)
			{
				ret = read_tree_platform(statp, element->u.object[i]->val, PLATFORM_MOBILE);
			}
			else
			{
				fprintf(stderr, "%s: unknown key: %s\n", __FUNCTION__, element->u.object[i]->key);
			}
		}
		break;
	default:
		fprintf(stderr, "%s: wierd, type=%d, %s\n", __FUNCTION__, element->type, element->u.data);
		break;
	}
	
	return ret;
}


static int read_tree(PLAY_STATISTICS_T* statp, json_val_t *root_structure)
{	
	read_tree_root(statp, root_structure);
	return 0;
}


int usage(const char *argv0)
{
	printf("usage: %s [options] JSON-FILE(s)...\n", argv0);
	printf("\t--no-comments : disallow C and YAML comments in json file (default to both on)\n");
	printf("\t--no-yaml-comments : disallow YAML comment (default to on)\n");
	printf("\t--no-c-comments : disallow C comment (default to on)\n");
	printf("\t--format : pretty print the json file to stdout (unless -o specified)\n");
	printf("\t--verify : quietly verified if the json file is valid. exit 0 if valid, 1 if not\n");
	printf("\t--max-nesting : limit the number of nesting in structure (default to no limit)\n");
	printf("\t--max-data : limit the number of characters of data (string/int/float) (default to no limit)\n");
	printf("\t--indent-string : set the string to use for indenting one level (default to 1 tab)\n");
	printf("\t--tree : build a tree (DOM)\n");
	printf("\t-o : output to a specific file instead of stdout\n");
	exit(0);
}

#if 0
int main(int argc, char **argv)
{
	int format = 0, verify = 0, use_tree = 0;
	int ret = 0, i;
	json_config config;
	char *output = "-";

	memset(&config, 0, sizeof(json_config));
	config.max_nesting = 0;
	config.max_data = 0;
	config.allow_c_comments = 1;
	config.allow_yaml_comments = 1;

	while (1) {
		int option_index;
		struct option long_options[] = {
			{ "no-comments", 0, 0, 0 },
			{ "no-yaml-comments", 0, 0, 0 },
			{ "no-c-comments", 0, 0, 0 },
			{ "format", 0, 0, 0 },
			{ "verify", 0, 0, 0 },
			{ "help", 0, 0, 0 },
			{ "max-nesting", 1, 0, 0 },
			{ "max-data", 1, 0, 0 },
			{ "indent-string", 1, 0, 0 },
			{ "tree", 0, 0, 0 },
			{ 0 },
		};
		int c = getopt_long(argc, argv, "o:", long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 0: {
			const char *name = long_options[option_index].name;
			if (strcmp(name, "help") == 0)
				usage(argv[0]);
			else if (strcmp(name, "no-c-comments") == 0)
				config.allow_c_comments = 0;
			else if (strcmp(name, "no-yaml-comments") == 0)
				config.allow_yaml_comments = 0;
			else if (strcmp(name, "no-comments") == 0)
				config.allow_c_comments = config.allow_yaml_comments = 0;
			else if (strcmp(name, "format") == 0)
				format = 1;
			else if (strcmp(name, "verify") == 0)
				verify = 1;
			else if (strcmp(name, "max-nesting") == 0)
				config.max_nesting = atoi(optarg);
			else if (strcmp(name, "max-data") == 0)
				config.max_data = atoi(optarg);
			else if (strcmp(name, "indent-string") == 0)
				indent_string = strdup(optarg);
			else if (strcmp(name, "tree") == 0)
				use_tree = 1;
			break;
			}
		case 'o':
			output = strdup(optarg);
			break;
		default:
			break;
		}
	}
	if (config.max_nesting < 0)
		config.max_nesting = 0;
	if (!output)
		output = "-";
	if (optind >= argc)
		usage(argv[0]);

	for (i = optind; i < argc; i++) {
		if (use_tree) {
			json_val_t *root_structure;
			ret = do_tree(&config, argv[i], &root_structure);
			if (ret)
				exit(ret);
			print_tree(root_structure, output);
		} else {
			if (format)
				ret = do_format(&config, argv[i], output);
			else if (verify)
				ret = do_verify(&config, argv[i]);
			else
				ret = do_parse(&config, argv[i]);
		}
		if (ret)
			exit(ret);
	}
	return ret;
}
#endif

int json_parse(PLAY_STATISTICS_T* statp, char* json_file)
{
	int format = 0, verify = 0, use_tree = 0;
	int ret = 0;
	json_config config;
	char *output = "-";

	memset(&config, 0, sizeof(json_config));
	config.max_nesting = 0;
	config.max_data = 0;
	config.allow_c_comments = 1;
	config.allow_yaml_comments = 1;

	use_tree = 1;
	if (config.max_nesting < 0)
		config.max_nesting = 0;
	if (!output)
		output = "-";

	if (use_tree) 
	{
		json_val_t *root_structure;
		ret = do_tree(&config, json_file, &root_structure);		
		if (ret)
		{
			return ret;
		}		
		//print_tree(root_structure, output);
		read_tree(statp, root_structure);
	} 
	else 
	{
		if (format)
			ret = do_format(&config, json_file, output);
		else if (verify)
			ret = do_verify(&config, json_file);
		else
			ret = do_parse(&config, json_file);
	}
	
	return ret;
}

void hashid_statistics_release(void* datap)
{
	HASHID_STATISTICS_T* hashidp = (HASHID_STATISTICS_T*)datap;
	if(hashidp != NULL)
	{
		free(hashidp);
	}
}

void area_statistics_release(void* datap)
{
	AREA_STATISTICS_T* areap = (AREA_STATISTICS_T*)datap;
	if(areap != NULL)
	{
		deque_release(areap->hashid_list, hashid_statistics_release);	
		areap->hashid_list = NULL;
		free(areap);
	}
}

void play_statistics_release(PLAY_STATISTICS_T * statp)
{
	fprintf(stdout, "%s\n", __FUNCTION__);
	statp->start_time = 0;
	deque_release(statp->pc_list, area_statistics_release);
	statp->pc_list = NULL;
	deque_release(statp->mobile_list, area_statistics_release);
	statp->mobile_list = NULL;
}


