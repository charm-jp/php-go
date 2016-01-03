/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2015 Arnaud Le Blanc                              |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Arnaud Le Blanc <arnaud.lb@gmail.com>                        |
  +----------------------------------------------------------------------+
*/

#include <dlfcn.h>
#include "phpgo.h"
#include "module.h"

static char* find_php_go_exports_fun(PHPExportsFun *fun, void *handle) {
	*fun = dlsym(handle, "PHPGoExports");
	if (!*fun) {
		char *buf;
		spprintf(&buf, 0, "PHPGoExports symbol not found: %s", dlerror());
		return buf;
	}
	return NULL;
}

static char* find_php_go_call_fun(PHPGoCallFun *fun, void *handle) {
	*fun = dlsym(handle, "PHPGoCall");
	if (!*fun) {
		char *buf;
		spprintf(&buf, 0, "PHPGoCall symbol not found: %s", dlerror());
		return buf;
	}
	return NULL;
}

static phpgo_module *find_cached_module(const char *path, const char *name TSRMLS_DC) {

	size_t path_len = strlen(path);
	size_t name_len = strlen(name);
	size_t key_len = path_len + 1 + name_len + 1;
	char *key = emalloc(key_len);
	int found;
	phpgo_module **pm;

	memcpy(key, path, path_len+1);
	memcpy(&key[path_len+1], name, name_len+1);

	found = zend_hash_find(&PHPGO_G(modules), key, key_len, (void**)&pm);
	efree(key);

	if (found != SUCCESS) {
		return NULL;
	}

	return *pm;
}

static void cache_module(const char *path, const char *name, phpgo_module *m TSRMLS_DC) {
	size_t path_len = strlen(path);
	size_t name_len = strlen(name);
	size_t key_len = path_len + 1 + name_len + 1;
	char *key = emalloc(key_len);
	zend_hash_add(&PHPGO_G(modules), key, key_len, &m, sizeof(m), NULL);
	efree(key);
}

char* phpgo_module_load(phpgo_module **module_pp, const char *path, const char *name TSRMLS_DC) {

	char *err;
	void *handle;
	PHPExportsFun php_go_exports_fun;
	PHPGoCallFun php_go_call_fun;
	php_exports *exports;
	int i;
	phpgo_module *module;

	module = find_cached_module(path, name TSRMLS_CC);
	if (module != NULL) {
		*module_pp = module;
		return NULL;
	}

	handle = dlopen(path, RTLD_NOW);
	if (!handle) {
		return estrdup(dlerror());
	}

	err = find_php_go_exports_fun(&php_go_exports_fun, handle);
	if (err) {
		return err;
	}

	err = find_php_go_call_fun(&php_go_call_fun, handle);
	if (err) {
		return err;
	}

	exports = php_go_exports_fun(name);
	if (!exports) {
		char *buf;
		spprintf(&buf, 0, "Not exports found for `%s`", name);
		return buf;
	}

	if (exports->version != PHPGO_API_VERSION) {
		char *buf;
		spprintf(&buf, 0, "Invalid module API version; found %d, expected %d. Please consider rebuilding the Go module.", exports->version, PHPGO_API_VERSION);
		return buf;
	}

	module = malloc(sizeof(*module));

	module->name = strdup(name);
	module->php_go_call_fun = php_go_call_fun;

	zend_hash_init(&module->exports, exports->num_exports, NULL, NULL, 1);

	for (i = 0; i < exports->num_exports; i++) {
		php_export *e = &exports->exports[i];
		zend_hash_add(&module->exports, e->name, strlen(e->name)+1, &e, sizeof(php_export*), NULL);
	}

	cache_module(path, name, module TSRMLS_CC);

	*module_pp = module;

	return NULL;
}

void phpgo_module_free(phpgo_module *module) {
	free(module->name);
	zend_hash_destroy(&module->exports);
	free(module);
}

void phpgo_module_dtor(void *data) {
	phpgo_module_free(*(phpgo_module**)data);
}
