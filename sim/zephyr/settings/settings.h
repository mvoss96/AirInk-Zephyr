#pragma once
#include <stddef.h>
#include <sys/types.h> // ssize_t, which prefs.cpp's read_cb returns

/** @file
 * Just enough <zephyr/settings/settings.h> for prefs.cpp to compile on the host.
 *
 * prefs.cpp is compiled for real (its table holds the ranges the menu's editor offers, so a stub
 * would put those numbers in a second place -- the duplication this preview is removing). What the
 * host lacks is the store underneath it, and prefs.cpp already survives that: a failed init leaves
 * `store_up` false, getters answer defaults and set() logs instead of saving.
 */

typedef ssize_t (*settings_read_cb)(void *cb_arg, void *data, size_t len);

struct settings_handler
{
	const char *name;
	int (*h_get)(const char *key, char *val, int val_len_max);
	int (*h_set)(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg);
	int (*h_commit)(void);
	int (*h_export)(int (*export_func)(const char *name, const void *val, size_t val_len));
};

int settings_subsys_init(void);
int settings_register(struct settings_handler *cf);
int settings_load_subtree(const char *subtree);
int settings_save_one(const char *name, const void *value, size_t val_len);
int settings_name_steq(const char *name, const char *key, const char **next);
