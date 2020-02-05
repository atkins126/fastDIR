
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include "fastcommon/shared_func.h"
#include "fastcommon/logger.h"
#include "fastcommon/sched_thread.h"
#include "common/fdir_types.h"
#include "server_handler.h"
#include "dentry.h"

#define INIT_LEVEL_COUNT 2

typedef struct fdir_manager {
    FDIRDentry dentry_root;
    struct fast_allocator_context name_acontext;
} FDIRManager;

const int delay_free_seconds = 3600;
static FDIRManager fdir_manager;

static int dentry_strdup(string_t *dest, const char *src, const int len)
{
    dest->str = (char *)fast_allocator_alloc(
            &fdir_manager.name_acontext, len + 1);
    if (dest->str == NULL) {
        logError("file: "__FILE__", line: %d, "
                "malloc %d fail", __LINE__, len + 1);
        return ENOMEM;
    }

    memcpy(dest->str, src, len + 1);
    dest->len = len;
    return 0;
}

#define dentry_strdup_ex(dest, src) \
    dentry_strdup(dest, (src)->str, (src)->len)

int dentry_init()
{
#define NAME_REGION_COUNT 1

    int result;
    struct fast_region_info regions[NAME_REGION_COUNT];

    memset(&fdir_manager, 0, sizeof(fdir_manager));

    FAST_ALLOCATOR_INIT_REGION(regions[0], 0, NAME_MAX + 1, 8, 16 * 1024);
    if ((result=fast_allocator_init_ex(&fdir_manager.name_acontext,
            regions, NAME_REGION_COUNT, 0, 0.00, 0, true)) != 0)
    {
        return result;
    }

    if ((result=dentry_strdup(&fdir_manager.dentry_root.name, "/", 1)) != 0) {
        return result;
    }
    fdir_manager.dentry_root.stat.mode |= S_IFDIR;

    return 0;
}

void dentry_destroy()
{
}

static int dentry_compare(const void *p1, const void *p2)
{
    return fc_string_compare(&((FDIRDentry *)p1)->name, 
            &((FDIRDentry *)p2)->name);
}

void dentry_free(void *ptr, const int delay_seconds)
{
    FDIRDentry *dentry;
    dentry = (FDIRDentry *)ptr;

    fast_allocator_free(&fdir_manager.name_acontext, dentry->name.str);
    if (delay_seconds > 0) {
        fast_mblock_delay_free_object(&dentry->context->dentry_allocator,
                    (void *)dentry, delay_seconds);
    } else {
        fast_mblock_free_object(&dentry->context->dentry_allocator,
                    (void *)dentry);
    }
}

int dentry_init_obj(void *element, void *init_args)
{
    FDIRDentry *dentry;
    dentry = (FDIRDentry *)element;
    dentry->context = (FDIRDentryContext *)init_args;
    return 0;
}

int dentry_init_context(FDIRDentryContext *context)
{
    int result;
    const int max_level_count = 20;

    if ((result=uniq_skiplist_init_ex(&context->factory,
                    max_level_count, dentry_compare, dentry_free,
                    16 * 1024, SKIPLIST_DEFAULT_MIN_ALLOC_ELEMENTS_ONCE,
                    delay_free_seconds)) != 0)
    {
        return result;
    }

     if ((result=fast_mblock_init_ex(&context->dentry_allocator,
                     sizeof(FDIRDentry), 64 * 1024,
                     dentry_init_obj, context, false)) != 0)
     {
        return result;
     }

     return 0;
}

static const FDIRDentry *dentry_find_ex(const string_t *paths, const int count)
{
    const string_t *p;
    const string_t *end;
    FDIRDentry *current;
    FDIRDentry target;

    current = &fdir_manager.dentry_root;
    end = paths + count;
    for (p=paths; p<end; p++) {
        if ((current->stat.mode & S_IFDIR) == 0) {
            return NULL;
        }

        target.name = *p;
        current = (FDIRDentry *)uniq_skiplist_find(current->children, &target);
        if (current == NULL) {
            return NULL;
        }
    }

    return current;
}

static int dentry_find_parent_and_me(const string_t *path, string_t *my_name,
        FDIRDentry **parent, FDIRDentry **me)
{
    FDIRDentry target;
    string_t paths[FDIR_MAX_PATH_COUNT];
    int count;

    if (path->len == 0 || path->str[0] != '/') {
        *parent = *me = NULL;
        my_name->len = 0;
        my_name->str = NULL;
        return EINVAL;
    }

    count = split_string_ex(path, '/', paths, FDIR_MAX_PATH_COUNT, true);
    if (count == 0) {
        *parent = NULL;
        *me = &fdir_manager.dentry_root;
        my_name->len = 0;
        my_name->str = "";
        return 0;
    }

    *my_name = paths[count - 1];
    if (count == 1) {
        *parent = &fdir_manager.dentry_root;
    } else {
        *parent = (FDIRDentry *)dentry_find_ex(paths, count - 1);
        if (*parent == NULL) {
            *me = NULL;
            return ENOENT;
        }
        if (((*parent)->stat.mode & S_IFDIR) == 0) {
            *me = NULL;
            return ENOENT;
        }
    }

    target.name = *my_name;
    *me = (FDIRDentry *)uniq_skiplist_find((*parent)->children, &target);
    return 0;
}

int dentry_create(FDIRDentryContext *context, const string_t *path,
        const int flags, const mode_t mode)
{
    FDIRDentry *parent;
    FDIRDentry *current;
    string_t my_name;
    int result;

    if ((mode & S_IFMT) == 0) {
        logError("file: "__FILE__", line: %d, "
                "invalid file mode: %d", __LINE__, mode);
        return EINVAL;
    }

    if ((result=dentry_find_parent_and_me(path, &my_name,
                    &parent, &current)) != 0)
    {
        return result;
    }

    if (parent == NULL || current != NULL) {
        return EEXIST;
    }

    if (uniq_skiplist_count(parent->children) >= MAX_ENTRIES_PER_PATH) {
        char *parent_end;
        parent_end = (char *)strrchr(path->str, '/');
        logError("file: "__FILE__", line: %d, "
                "too many entries in path %.*s, exceed %d",
                __LINE__, (int)(parent_end - path->str),
                path->str, MAX_ENTRIES_PER_PATH);
        return ENOSPC;
    }
    
    current = (FDIRDentry *)fast_mblock_alloc_object(
            &context->dentry_allocator);
    if (current == NULL) {
        return ENOMEM;
    }

    if ((mode & S_IFDIR) == 0) {
        current->children = NULL;
    } else {
        current->children = uniq_skiplist_new(&context->factory,
                INIT_LEVEL_COUNT);
        if (current->children == NULL) {
            return ENOMEM;
        }
    }

    if ((result=dentry_strdup_ex(&current->name, &my_name)) != 0) {
        return result;
    }
    current->stat.mode = mode;
    current->stat.size = 0;
    current->stat.atime = 0;
    current->stat.ctime = current->stat.mtime = g_current_time;

    if ((result=uniq_skiplist_insert(parent->children, current)) != 0) {
        return result;
    }
    return 0;
}

int dentry_remove(FDIRServerContext *server_context, const string_t *path)
{
    FDIRDentry *parent;
    FDIRDentry *current;
    string_t my_name;
    int result;

    if ((result=dentry_find_parent_and_me(path, &my_name,
                    &parent, &current)) != 0)
    {
        return result;
    }

    if (current == NULL) {
        return ENOENT;
    }

    if ((current->stat.mode & S_IFDIR) != 0) {
        if (uniq_skiplist_count(current->children) > 0) {
            return ENOTEMPTY;
        }

        server_add_to_delay_free_queue(&server_context->delay_free_context,
                current->children, delay_free_seconds);
    }

    return uniq_skiplist_delete(parent->children, current);
}

int dentry_find(const string_t *path, FDIRDentry **dentry)
{
    FDIRDentry *parent;
    string_t my_name;
    int result;

    
    if ((result=dentry_find_parent_and_me(path, &my_name,
                    &parent, dentry)) != 0)
    {
        return result;
    }

    if (*dentry == NULL) {
        return ENOENT;
    }

    return 0;
}

static int check_alloc_dentry_array(FDIRDentryArray *array, const int target_count)
{
    FDIRDentry **entries;
    int new_alloc;
    int bytes;

    if (array->alloc >= target_count) {
        return 0;
    }

    new_alloc = (array->alloc > 0) ? array->alloc : 4 * 1024;
    while (new_alloc < target_count) {
        new_alloc *= 2;
    }

    bytes = sizeof(FDIRDentry *) * new_alloc;
    entries = (FDIRDentry **)malloc(bytes);
    if (entries == NULL) {
        logError("file: "__FILE__", line: %d, "
                "malloc %d bytes fail", __LINE__, bytes);
        return ENOMEM;
    }

    if (array->entries != NULL) {
        if (array->count > 0) {
            memcpy(entries, array->entries,
                    sizeof(FDIRDentry *) * array->count);
        }
        free(array->entries);
    }

    array->alloc = new_alloc;
    array->entries = entries;
    return 0;
}

int dentry_list(const string_t *path, FDIRDentryArray *array)
{
    FDIRDentry *dentry;
    FDIRDentry *current;
    FDIRDentry **pp;
    UniqSkiplistIterator iterator;
    int result;
    int count;

    array->count = 0;
    if ((result=dentry_find(path, &dentry)) != 0) {
        return result;
    }

    if ((dentry->stat.mode & S_IFDIR) == 0) {
        count = 1;
    } else {
        count = uniq_skiplist_count(dentry->children);
    }

    if ((result=check_alloc_dentry_array(array, count)) != 0) {
        return result;
    }

    if ((dentry->stat.mode & S_IFDIR) == 0) {
        array->entries[array->count++] = dentry;
    } else {
        pp = array->entries;
        uniq_skiplist_iterator(dentry->children, &iterator);
        while ((current=(FDIRDentry *)uniq_skiplist_next(&iterator)) != NULL) {
           *pp++ = dentry;
        }
        array->count = pp - array->entries;
    }

    return 0;
}

void dentry_array_free(FDIRDentryContext *context, FDIRDentryArray *array)
{
    if (array->entries != NULL) {
        free(array->entries);
        array->entries = NULL;
        array->alloc = array->count = 0;
    }
}