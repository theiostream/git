#include "builtin.h"
#include "color.h"
#include "diff.h"
#include "diffcore.h"
#include "hashmap.h"
#include "revision.h"

#define HEADER_INDENT "      "

enum collection_phase {
	WORKTREE,
	INDEX
};

struct file_stat {
	struct hashmap_entry ent;
	struct {
		uintmax_t added, deleted;
	} index, worktree;
	char name[FLEX_ARRAY];
};

struct collection_status {
	enum collection_phase phase;

	const char *reference;
	struct pathspec pathspec;

	struct hashmap file_map;
};

static int use_color = -1;
enum color_add_i {
	COLOR_PROMPT,
	COLOR_HEADER,
	COLOR_HELP,
	COLOR_ERROR
};

static char colors[][COLOR_MAXLEN] = {
	GIT_COLOR_BOLD_BLUE, /* Prompt */
	GIT_COLOR_BOLD,      /* Header */
	GIT_COLOR_BOLD_RED,  /* Help */
	GIT_COLOR_BOLD_RED   /* Error */
};

static const char *get_color(enum color_add_i ix)
{
	if (want_color(use_color))
		return colors[ix];
	return "";
}

static int parse_color_slot(const char *slot)
{
	if (!strcasecmp(slot, "prompt"))
		return COLOR_PROMPT;
	if (!strcasecmp(slot, "header"))
		return COLOR_HEADER;
	if (!strcasecmp(slot, "help"))
		return COLOR_HELP;
	if (!strcasecmp(slot, "error"))
		return COLOR_ERROR;

	return -1;
}

static int add_i_config(const char *var,
		const char *value, void *cbdata)
{
	const char *name;

	if (!strcmp(var, "color.interactive")) {
		use_color = git_config_colorbool(var, value);
		return 0;
	}

	if (skip_prefix(var, "color.interactive", &name)) {
		int slot = parse_color_slot(name);
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		return color_parse(value, colors[slot]);
	}

	return git_default_config(var, value, cbdata);
}

static int hash_cmp(const void *entry, const void *entry_or_key,
 			      const void *keydata)
{
 	const struct file_stat *e1 = entry, *e2 = entry_or_key;
 	const char *name = keydata ? keydata : e2->name;

	return strcmp(e1->name, name);
}

static int alphabetical_cmp(const void *a, const void *b)
{
	struct file_stat *f1 = *((struct file_stat **)a);
	struct file_stat *f2 = *((struct file_stat **)b);

	return strcmp(f1->name, f2->name);
}

static void collect_changes_cb(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	struct collection_status *s = data;
	struct diffstat_t stat = { 0 };
	int i;

	if (!q->nr)
		return;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p;
		p = q->queue[i];
		diff_flush_stat(p, options, &stat);
	}

	for (i = 0; i < stat.nr; i++) {
		struct file_stat *entry;
		const char *name = stat.files[i]->name;
		unsigned int hash = strhash(name);

		entry = hashmap_get_from_hash(&s->file_map, hash, name);
		if (!entry) {
			FLEX_ALLOC_STR(entry, name, name);
			hashmap_entry_init(entry, hash);
			hashmap_add(&s->file_map, entry);
		}

		if (s->phase == WORKTREE) {
			entry->worktree.added = stat.files[i]->added;
			entry->worktree.deleted = stat.files[i]->deleted;
		} else if (s->phase == INDEX) {
			entry->index.added = stat.files[i]->added;
			entry->index.deleted = stat.files[i]->deleted;
		}
	}
}

static void collect_changes_worktree(struct collection_status *s)
{
	struct rev_info rev;

	s->phase = WORKTREE;

	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, NULL);

	rev.max_count = 0;

	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = collect_changes_cb;
	rev.diffopt.format_callback_data = s;

	run_diff_files(&rev, 0);
}

static void collect_changes_index(struct collection_status *s)
{
	struct rev_info rev;
	struct setup_revision_opt opt = { 0 };

	s->phase = INDEX;

	init_revisions(&rev, NULL);
	opt.def = s->reference;
	setup_revisions(0, NULL, &rev, &opt);

	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = collect_changes_cb;
	rev.diffopt.format_callback_data = s;

	run_diff_index(&rev, 1);
}

static void list_modified_into_status(struct collection_status *s)
{
	collect_changes_worktree(s);
	collect_changes_index(s);
}

static void print_modified(void)
{
	int i = 0;
	struct collection_status s;
	/* TRANSLATORS: you can adjust this to align "git add -i" status menu */
	const char *modified_fmt = _("%12s %12s %s");
	const char *header_color = get_color(COLOR_HEADER);
	unsigned char sha1[20];

	struct hashmap_iter iter;
	struct file_stat **files;
	struct file_stat *entry;

	if (read_cache() < 0)
		return;

	s.reference = !get_sha1("HEAD", sha1) ? "HEAD": EMPTY_TREE_SHA1_HEX;
	hashmap_init(&s.file_map, hash_cmp, 0);
	list_modified_into_status(&s);

	if (s.file_map.size < 1) {
		printf("\n");
		return;
	}

	printf(HEADER_INDENT);
	color_fprintf(stdout, header_color, modified_fmt, _("staged"),
			_("unstaged"), _("path"));
	printf("\n");

	hashmap_iter_init(&s.file_map, &iter);

	files = xcalloc(s.file_map.size, sizeof(struct file_stat *));
	while ((entry = hashmap_iter_next(&iter))) {
		files[i++] = entry;
	}
	QSORT(files, s.file_map.size, alphabetical_cmp);

	for (i = 0; i < s.file_map.size; i++) {
		struct file_stat *f = files[i];

		char worktree_changes[50];
		char index_changes[50];

		if (f->worktree.added || f->worktree.deleted)
			snprintf(worktree_changes, 50, "+%ju/-%ju", f->worktree.added,
					f->worktree.deleted);
		else
			snprintf(worktree_changes, 50, "%s", _("nothing"));

		if (f->index.added || f->index.deleted)
			snprintf(index_changes, 50, "+%ju/-%ju", f->index.added,
					f->index.deleted);
		else
			snprintf(index_changes, 50, "%s", _("unchanged"));

		printf(" %2d: ", i + 1);
		printf(modified_fmt, index_changes, worktree_changes, f->name);
		printf("\n");
	}
	printf("\n");

	free(files);
}

static const char * const builtin_add_helper_usage[] = {
	N_("git add-interactive--helper <command>"),
	NULL
};

enum cmd_mode {
	DEFAULT = 0,
	STATUS
};

int cmd_add__helper(int argc, const char **argv, const char *prefix)
{
	enum cmd_mode mode = DEFAULT;

	struct option options[] = {
		OPT_CMDMODE(0, "status", &mode,
			 N_("print status information with diffstat"), STATUS),
		OPT_END()
	};

	git_config(add_i_config, NULL);
	argc = parse_options(argc, argv, NULL, options,
			     builtin_add_helper_usage,
			     PARSE_OPT_KEEP_ARGV0);

	if (mode == STATUS)
		print_modified();
	else
		usage_with_options(builtin_add_helper_usage,
				   options);

	return 0;
}
