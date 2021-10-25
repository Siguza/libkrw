#include <dirent.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "libkrw.h"
#include "libkrw_plugin.h"
#include "libkrw_tfp0.h"

static struct krw_handlers_s krw_handlers;

static dispatch_once_t init_krw_handlers_once;

static int scandir_dylib_select(const struct dirent *entry)
{
    char *ext = strrchr(entry->d_name, '.');
    return (ext++ && strcmp(ext, "dylib") == 0);
}

static int scandir_alpha_compar(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

static int obtain_kcall_funcs(void *plugin) {
    krw_plugin_initializer_t init = (krw_plugin_initializer_t)dlsym(plugin, "kcall_initializer");
    if (init == NULL) return ENOTSUP;

    struct krw_handlers_s handlers = krw_handlers;
    int r = init(&handlers);
    if (r != 0) return r;

    // We got a plugin that says it can handle kcall
    if (handlers.kcall == NULL) {
        return ENOSYS;
    }
    if (handlers.version > LIBKRW_HANDLERS_VERSION) {
        fprintf(stderr, "Detected plugin of higher API version, please update libkrw if possible\n");
    }
    krw_handlers.kcall = handlers.kcall;
    krw_handlers.physread = handlers.physread;
    krw_handlers.physwrite = handlers.physwrite;
    return 0;
}

static int obtain_krw_funcs(void *plugin) {
    krw_plugin_initializer_t init = (krw_plugin_initializer_t)dlsym(plugin, "krw_initializer");
    if (init == NULL) return ENOTSUP;

    struct krw_handlers_s handlers = krw_handlers;
    int r = init(&handlers);
    if (r != 0) return r;

    // We got a plugin that says it can handle krw
    if (handlers.kread == NULL || handlers.kwrite == NULL) {
        return ENOSYS;
    }
    if (handlers.version > LIBKRW_HANDLERS_VERSION) {
        fprintf(stderr, "Detected plugin of higher API version, please update libkrw if possible\n");
    }
    krw_handlers.kbase = handlers.kbase;
    krw_handlers.kread = handlers.kread;
    krw_handlers.kwrite = handlers.kwrite;
    krw_handlers.kmalloc = handlers.kmalloc;
    krw_handlers.kdealloc = handlers.kdealloc;
    return 0;
}

static void iterate_plugins(int (*callback)(void *), void **check) {
    struct dirent **plugins;
    ssize_t nument = scandir("/usr/lib/libkrw", &plugins, &scandir_dylib_select, &scandir_alpha_compar);
    // Load any kcall handlers
    if (nument != -1) {
        char *path = strdup("/usr/lib/libkrw/");
        size_t path_size = strlen("/usr/lib/libkrw/");
        for (int i=0; *check == NULL && i<nument; i++) {
            size_t plugin_path_len = strlen("/usr/lib/libkrw/") + plugins[i]->d_namlen;
            if (path_size < plugin_path_len) {
                char *newpath = realloc(path, plugin_path_len + 1);
                if (newpath == NULL) {
                    fprintf(stderr, "Fatal Error: unable to realloc\n");
                    continue; // We failed to realloc - try next plugin I guess
                }
                path = newpath;
            }
            strcpy(path+strlen("/usr/lib/libkrw/"), plugins[i]->d_name);
            void *plugin = dlopen(path, RTLD_LOCAL|RTLD_LAZY);
            if (plugin == NULL) {
                fprintf(stderr, "Error attempting to load plugin %s: %s\n", path, dlerror());
                continue;
            }
            int rv = callback(plugin);
            if (rv == 0) break;

            if (rv == ENOSYS) {
                fprintf(stderr, "KRW plugin %s did not provide functions it purported to provide\n", path);
            }
            // We failed, will try next
            dlclose(plugin);
        }
        free(path);
        free(plugins);
    }
}

static void init_krw_handlers(void *ctx) {
    if (libkrw_initialization(&krw_handlers) != 0) {
        iterate_plugins(&obtain_krw_funcs, (void**)&krw_handlers.kread);
    }
    iterate_plugins(&obtain_kcall_funcs, (void**)&krw_handlers.kcall);
}

int kbase(uint64_t *addr) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.kbase == NULL) return ENOTSUP;
    return krw_handlers.kbase(addr);
}

int kread(uint64_t from, void *to, size_t len) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.kread == NULL) return ENOTSUP;
    return krw_handlers.kread(from, to, len);
}

int kwrite(void *from, uint64_t to, size_t len) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.kwrite == NULL) return ENOTSUP;
    return krw_handlers.kwrite(from, to, len);
}

int kmalloc(uint64_t *addr, size_t size) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.kmalloc == NULL) return ENOTSUP;
    return krw_handlers.kmalloc(addr, size);
}

int kdealloc(uint64_t addr, size_t size) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.kdealloc == NULL) return ENOTSUP;
    return krw_handlers.kdealloc(addr, size);
}

int kcall(uint64_t func, size_t argc, const uint64_t *argv, uint64_t *ret) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.kcall == NULL) return ENOTSUP;
    return krw_handlers.kcall(func, argc, argv, ret);
}

int physread(uint64_t from, void *to, size_t len, uint8_t granule) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.physread == NULL) return ENOTSUP;
    return krw_handlers.physread(from, to, len, granule);
}

int physwrite(void *from, uint64_t to, size_t len, uint8_t granule) {
    dispatch_once_f(&init_krw_handlers_once, NULL, &init_krw_handlers);
    if (krw_handlers.physwrite == NULL) return ENOTSUP;
    return krw_handlers.physwrite(from, to, len, granule);
}
