#ifndef CPYTHON_INCLUDE_SHAREDHEAP_H_
#define CPYTHON_INCLUDE_SHAREDHEAP_H_

#define CDS_MAX_IMG_SIZE (1024 * 1024 * 1024)
#define CDS_REQUESTING_ADDR ((void *)0x280000000L)

#include <stdbool.h>

#include "Python.h"

#define ALIEN_TO(size, align) (((size) + ((align)-1)) & ~((align)-1))
#define UNSHIFT(p, shift, ty) ((ty *)((void *)(p) + (shift)))

/* Indicates that I know what I'm doing when casting one pointer to an
 * unrelated type. */
#define REINTERPRET_CAST(t, p) ((t *)(void *)(p))

typedef struct _heapserializedobject {
    PyTypeObject *ty;
    PyObject **archive_addr_to_patch;
    void *obj;
} HeapSerializedObject;

typedef struct _moveinctxitem {
    PyObject **archive_addr_to_patch;
    void *obj;
    PyTypeObject *ty;

    struct _moveinctxitem *next;
} MoveInItem;
typedef struct _moveinctx {
    size_t size;
    MoveInItem *header;
} MoveInContext;

struct HeapArchiveHeader {
    void *mapped_addr;
    void *none_addr;
    void *true_addr;
    void *false_addr;
    void *ellipsis_addr;
    size_t used;

    PyObject *obj;
    int serialized_count;
    HeapSerializedObject *serialized_array;
};

void
move_in(PyObject *, PyObject **, MoveInContext *, void *(*alloc)(size_t));
void
patch_pyobject(PyObject **, long, bool);

void
_PyMem_SharedMoveIn(PyObject *o);
PyObject *
_PyMem_SharedGetObj(void);
PyObject *
_PyMem_TraceImport(PyObject *pkg);

PyAPI_FUNC(int) _PyMem_IsShared(void *ptr);

void
verbose(int verbosity, const char *fmt, ...);

#endif  // CPYTHON_INCLUDE_SHAREDHEAP_H_
