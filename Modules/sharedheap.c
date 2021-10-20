#include "sharedheap.h"

#define FAST_PATCH 1

#include <fcntl.h>
#include <pycore_gc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define USING_MMAP 1

static char *shm;
struct HeapArchiveHeader *h;
static int n_alloc;
static long shift;
static bool dumped;
static int fd;
static long t0, t1, t2, t3;

static inline long
nanoTime()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000 + t.tv_nsec;
}

void
verbose(int verbosity, const char *fmt, ...)
{
    if (Py_CDSVerboseFlag >= verbosity) {
        va_list arg;
        va_start(arg, fmt);
        fprintf(stderr, "[sharedheap] ");
        vfprintf(stderr, fmt, arg);
        fprintf(stderr, "\n");
        va_end(arg);
    }
}

#define CHECK_NAME(archive)                                 \
    if ((archive) == NULL) {                                \
        verbose(0, "PYCDSARCHIVE not specific, skip CDS."); \
        return NULL;                                        \
    }

void *
_PyMem_CreateSharedMmap(wchar_t *const archive)
{
    CHECK_NAME(archive);
    fd =
        open(Py_EncodeLocale(archive, NULL), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        verbose(0, "create mmap file failed.");
        return NULL;
    }
    ftruncate(fd, CDS_MAX_IMG_SIZE);

    shm = mmap(CDS_REQUESTING_ADDR, CDS_MAX_IMG_SIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        verbose(0, "mmap failed, file will not be cleaned.");
        return NULL;
    }
    h = (struct HeapArchiveHeader *)shm;
    h->mapped_addr = shm;
    h->none_addr = Py_None;
    h->true_addr = Py_True;
    h->false_addr = Py_False;
    h->ellipsis_addr = Py_Ellipsis;
    h->used = 4096;
    h->serialized_count = 0;
    h->serialized_array = NULL;
    return shm;
}

void
prepare_shared_heap(void);

#ifdef MAP_POPULATE
#define M_POPULATE MAP_POPULATE
#else
#define M_POPULATE 0
#endif

void *
_PyMem_LoadSharedMmap(wchar_t *const archive)
{
    CHECK_NAME(archive);
    t0 = nanoTime();
    char *local_archive = Py_EncodeLocale(archive, NULL);
    fd = open(local_archive, O_RDWR);
    if (fd < 0) {
        verbose(0, "open mmap file failed.");
        goto fail;
    }
    struct HeapArchiveHeader hbuf;
    if (read(fd, &hbuf, sizeof(hbuf)) != sizeof(hbuf)) {
        verbose(0, "read header failed.");
        goto fail;
    }
    verbose(2, "requesting %p...", hbuf.mapped_addr);
    size_t aligned_size = ALIEN_TO(hbuf.used, 4096);
    shm = mmap(
        hbuf.mapped_addr, aligned_size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_FIXED | (USING_MMAP ? M_POPULATE : MAP_ANONYMOUS),
        fd, 0);
    t1 = nanoTime();
    if (shm == MAP_FAILED) {
        verbose(0, "mmap failed.");
        goto fail;
    }
    else if (shm != hbuf.mapped_addr) {
        verbose(0, "mmap relocated.");
        goto fail;
    }
    h = REINTERPRET_CAST(struct HeapArchiveHeader, shm);
    //    if (USING_MMAP) {
    //        for (size_t i = 0; i < h->used; i += 4096) {
    //            ((char volatile *)shm)[i] += 0;
    //        }
    //    }
    //    else {
    //        lseek(fd, 0, SEEK_SET);
    //        size_t nread = 0;
    //        while (nread < hbuf.used) {
    //            nread += read(fd, shm + nread, hbuf.used - nread);
    //        }
    //    }
    //    close(fd);
    prepare_shared_heap();
    t3 = nanoTime();
    verbose(2, "ASLR data segment shift = %c0x%lx", shift < 0 ? '-' : ' ',
            labs(shift));
    verbose(1, "mmap success elapsed=%ld ns (%ld + %ld + %ld)", t3 - t0,
            t1 - t0, t2 - t1, t3 - t2);
    return shm;
fail:
    close(fd);
    return NULL;
}

void
prepare_shared_heap(void)
{
    assert(shm);
    if (h->none_addr) {
        shift = (void *)Py_None - (void *)h->none_addr;
    }
    patch_pyobject(&h->obj, shift, false);
    t2 = nanoTime();

    // currently, no serialization is needed
    assert(h->serialized_count == 0);
    // reverse order to make sure leaf objects get deserialized first.
    for (int i = h->serialized_count - 1; i >= 0; --i) {
        HeapSerializedObject *serialized = &h->serialized_array[i];
        assert(_PyMem_IsShared(serialized->archive_addr_to_patch));
        assert(*serialized->archive_addr_to_patch == NULL);
        PyTypeObject *ty = UNSHIFT(serialized->ty, shift, PyTypeObject);
        verbose(0, "deserializing: %s", ty->tp_name);
        //        assert(ty->tp_patch);
        //        PyObject *real = ty->tp_patch(serialized->obj, shift);
        //        assert(real != NULL);
        //        *((PyObject **)serialized->archive_addr_to_patch) = real;
    }
}

static void *
_PyMem_SharedMalloc(size_t size)
{
    n_alloc++;
    if (!size)
        size = 1;
    // Extra memory for PyGC_Head,
    // otherwise previous object will be overwritten by GC.
    // Objects in CPython are not compact.
    h->used += sizeof(PyGC_Head);
    size_t size_aligned = ALIEN_TO(size, 8);
    void *res = shm + h->used;
    if ((h->used += size_aligned) > CDS_MAX_IMG_SIZE) {
        h->used -= sizeof(PyGC_Head);
        h->used -= size_aligned;
        return NULL;
    }
    verbose(2, "SharedMalloc: [%p, %p)", res, (shm + h->used));
    return res;
}

int
_PyMem_IsShared(void *ptr)
{
    return h && ptr >= (void *)h && ptr < ((void *)h + h->used);
}

#define UNWIND_CODE_FIELDS                 \
    SERIALIZE_HANDLER(co_consts);          \
    SERIALIZE_HANDLER(co_names);           \
    SERIALIZE_HANDLER(co_code);            \
    SERIALIZE_HANDLER(co_exceptiontable);  \
                                           \
    SIMPLE_HANDLER(co_flags);              \
    SIMPLE_HANDLER(co_warmup);             \
                                           \
    SIMPLE_HANDLER(co_argcount);           \
    SIMPLE_HANDLER(co_posonlyargcount);    \
    SIMPLE_HANDLER(co_kwonlyargcount);     \
    SIMPLE_HANDLER(co_stacksize);          \
    SIMPLE_HANDLER(co_flags);              \
    SIMPLE_HANDLER(co_firstlineno);        \
                                           \
    SERIALIZE_HANDLER(co_localsplusnames); \
    SERIALIZE_HANDLER(co_localspluskinds); \
    SERIALIZE_HANDLER(co_filename);        \
    SERIALIZE_HANDLER(co_name);            \
    SERIALIZE_HANDLER(co_qualname);        \
    SERIALIZE_HANDLER(co_linetable);       \
    SERIALIZE_HANDLER(co_endlinetable);    \
    SERIALIZE_HANDLER(co_columntable);     \
                                           \
    SIMPLE_HANDLER(co_nlocalsplus);        \
    SIMPLE_HANDLER(co_nlocals);            \
    SIMPLE_HANDLER(co_nplaincellvars);     \
    SIMPLE_HANDLER(co_ncellvars);          \
    SIMPLE_HANDLER(co_nfreevars);          \
                                           \
    SERIALIZE_HANDLER(co_varnames);        \
    SERIALIZE_HANDLER(co_freevars);        \
    SERIALIZE_HANDLER(co_cellvars);

/* todo: The semantic is not straight forward, add detail doc. */
void
patch_pyobject(PyObject **ref, long shift, bool not_serialization)
{
    if (FAST_PATCH && shift == 0)
        return;
    PyObject *op = *ref;
    if (op == NULL) {
        // deserialize later
        return;
    }
    else if (op == h->none_addr || op == h->true_addr || op == h->false_addr ||
             op == h->ellipsis_addr) {
        *ref = UNSHIFT(op, shift, PyObject);
        return;
    }
    else {
        PyTypeObject *ty = UNSHIFT(op->ob_type, shift, PyTypeObject);

        if (/* numbers */ ty == &PyComplex_Type || ty == &PyLong_Type ||
            ty == &PyFloat_Type ||
            /* strings */ ty == &PyBytes_Type || ty == &PyUnicode_Type) {
            // simple patch
            Py_SET_TYPE(*ref, ty);
        }
        else if (ty == &PyTuple_Type) {
            Py_SET_TYPE(op, &PyTuple_Type);
            PyTupleObject *tuple_op = (PyTupleObject *)op;
            for (Py_ssize_t i = Py_SIZE(tuple_op); --i >= 0;) {
                patch_pyobject(&tuple_op->ob_item[i], shift, false);
            }
        }
        else if (ty == &PyCode_Type) {
            Py_SET_TYPE(op, &PyCode_Type);
            PyCodeObject *code_op = (PyCodeObject *)op;
#define SIMPLE_HANDLER(field)
#define SERIALIZE_HANDLER(field)                       \
    do {                                               \
        patch_pyobject(&code_op->field, shift, false); \
    } while (0)

            UNWIND_CODE_FIELDS

#undef SERIALIZE_HANDLER
#undef SIMPLE_HANDLER
        }
        else {
            assert(false);  // should not reach here
        }
    }
}

void
move_in(PyObject *op, PyObject **target, MoveInContext *ctx,
        void *(*alloc)(size_t))
{
    *target = NULL;
    if (op == NULL) {
        return;
    }
    PyTypeObject *ty = op->ob_type;

    verbose(2, "move %s@%p into %p", op->ob_type->tp_name, op, target);

#define SIMPLE_MOVE_IN(obj_type, type, copy)                   \
    obj_type *res = _PyMem_SharedMalloc(_PyObject_SIZE(type)); \
    PyObject_INIT(res, type);                                  \
    do {                                                       \
        copy                                                   \
    } while (0);                                               \
    *target = (PyObject *)res;                                 \
    PyObject_GC_UnTrack(*target);

    if (ty == &PyBool_Type || ty == &_PyNone_Type || ty == &PyEllipsis_Type) {
        *target = op;
    }
    else if (ty == &PyBytes_Type) {
        // PyBytesObject_SIZE
        Py_ssize_t size = Py_SIZE(op);
        PyBytesObject *res = (PyBytesObject *)_PyMem_SharedMalloc(
            offsetof(PyBytesObject, ob_sval) + 1 + size);

        (void)PyObject_INIT_VAR(res, &PyBytes_Type, size);
        res->ob_shash = -1;
        memcpy(res->ob_sval, ((PyBytesObject *)op)->ob_sval, size + 1);

        *target = (PyObject *)res;
        PyObject_GC_UnTrack(*target);
    }
    else if (ty == &PyComplex_Type) {
        SIMPLE_MOVE_IN(PyComplexObject, &PyComplex_Type,
                       { res->cval = ((PyComplexObject *)op)->cval; })
    }
    else if (ty == &PyFloat_Type) {
        SIMPLE_MOVE_IN(PyFloatObject, &PyFloat_Type,
                       { res->ob_fval = ((PyFloatObject *)op)->ob_fval; })
    }
    else if (ty == &PyTuple_Type || ty == &PyFrozenSet_Type) {
        // PyTuple_New starts

        PyTupleObject *res;
        PyTupleObject *src;
        if (ty != &PyTuple_Type)
            src = (PyTupleObject *)PySequence_Tuple(op);
        else
            src = (PyTupleObject *)op;
        Py_ssize_t nitems = PyTuple_Size((PyObject *)src);

        // tuple_alloc & _PyObject_GC_NewVar starts
        size_t var_size = _PyObject_VAR_SIZE(&PyTuple_Type, nitems);

        PyVarObject *var = (PyVarObject *)_PyMem_SharedMalloc(var_size);
        PyObject_INIT_VAR(var, &PyTuple_Type, nitems);
        // tuple_alloc & _PyObject_GC_NewVar ends

        res = (PyTupleObject *)var;

        for (Py_ssize_t i = 0; i < nitems; i++) {
            move_in(src->ob_item[i], &res->ob_item[i], ctx,
                    _PyMem_SharedMalloc);
        }
        PyObject_GC_UnTrack(res);

        *target = (PyObject *)res;
    }
    else if (ty == &PyLong_Type) {
        // _PyLong_Copy starts
        PyLongObject *src = (PyLongObject *)op;
        Py_ssize_t size = Py_SIZE(src);
        if (size < 0)
            size = -(size);

        // _PyLong_New starts
        PyLongObject *res = _PyMem_SharedMalloc(
            offsetof(PyLongObject, ob_digit) + size * sizeof(digit));
        PyObject_INIT_VAR((PyVarObject *)res, &PyLong_Type, Py_SIZE(src));
        // _PyLong_New ends

        while (--size >= 0) {
            res->ob_digit[size] = src->ob_digit[size];
        }
        // _PyLong_Copy ends

        PyObject_GC_UnTrack(res);

        *target = (PyObject *)res;
    }
    else if (ty == &PyUnicode_Type) {
        // basically copied from unicodeobject.c, todo: optimize

        PyObject *res;
        // _PyUnicode_Copy starts
        Py_ssize_t size = PyUnicode_GET_LENGTH(op);
        Py_UCS4 maxchar = PyUnicode_MAX_CHAR_VALUE(op);

        // PyUnicode_New(Py_ssize_t size, Py_UCS4 maxchar) starts
        PyCompactUnicodeObject *unicode;
        void *data;
        enum PyUnicode_Kind kind;
        int is_sharing, is_ascii;
        Py_ssize_t char_size;
        Py_ssize_t struct_size;

        is_ascii = 0;
        is_sharing = 0;
        struct_size = sizeof(PyCompactUnicodeObject);
        if (maxchar < 128) {
            kind = PyUnicode_1BYTE_KIND;
            char_size = 1;
            is_ascii = 1;
            struct_size = sizeof(PyASCIIObject);
        }
        else if (maxchar < 256) {
            kind = PyUnicode_1BYTE_KIND;
            char_size = 1;
        }
        else if (maxchar < 65536) {
            kind = PyUnicode_2BYTE_KIND;
            char_size = 2;
            if (sizeof(wchar_t) == 2)
                is_sharing = 1;
        }
        else {
            kind = PyUnicode_4BYTE_KIND;
            char_size = 4;
            if (sizeof(wchar_t) == 4)
                is_sharing = 1;
        }

        res = (PyObject *)_PyMem_SharedMalloc(struct_size +
                                              (size + 1) * char_size);
        PyObject_Init(res, &PyUnicode_Type);

        unicode = (PyCompactUnicodeObject *)res;
        if (is_ascii)
            data = ((PyASCIIObject *)res) + 1;
        else
            data = unicode + 1;
        (((PyASCIIObject *)(unicode))->length) = size;
        (((PyASCIIObject *)(unicode))->hash) = -1;
        (((PyASCIIObject *)(unicode))->state).interned = 0;
        (((PyASCIIObject *)(unicode))->state).kind = kind;
        (((PyASCIIObject *)(unicode))->state).compact = 1;
        (((PyASCIIObject *)(unicode))->state).ready = 1;
        (((PyASCIIObject *)(unicode))->state).ascii = is_ascii;
        if (is_ascii) {
            ((char *)data)[size] = 0;
            (((PyASCIIObject *)(unicode))->wstr) = NULL;
        }
        else if (kind == PyUnicode_1BYTE_KIND) {
            ((char *)data)[size] = 0;
            (((PyASCIIObject *)(unicode))->wstr) = NULL;
            (((PyCompactUnicodeObject *)(unicode))->wstr_length) = 0;
            unicode->utf8 = NULL;
            unicode->utf8_length = 0;
        }
        else {
            unicode->utf8 = NULL;
            unicode->utf8_length = 0;
            if (kind == PyUnicode_2BYTE_KIND)
                ((Py_UCS2 *)data)[size] = 0;
            else /* kind == PyUnicode_4BYTE_KIND */
                ((Py_UCS4 *)data)[size] = 0;
            if (is_sharing) {
                (((PyCompactUnicodeObject *)(unicode))->wstr_length) = size;
                (((PyASCIIObject *)(unicode))->wstr) = (wchar_t *)data;
            }
            else {
                (((PyCompactUnicodeObject *)(unicode))->wstr_length) = 0;
                (((PyASCIIObject *)(unicode))->wstr) = NULL;
            }
        }
        // PyUnicode_New(Py_ssize_t size, Py_UCS4 maxchar) ends

        memcpy(PyUnicode_DATA(res), PyUnicode_DATA(op),
               size * PyUnicode_KIND(op));
        // _PyUnicode_Copy ends

        *target = (PyObject *)res;
        PyObject_GC_UnTrack(*target);
    }
    else if (ty == &PyCode_Type) {
        PyCodeObject *src = (PyCodeObject *)op;

        PyCodeObject *res =
            (PyCodeObject *)_PyMem_SharedMalloc(_PyObject_SIZE(&PyCode_Type));
        PyObject_INIT(res, &PyCode_Type);

#define SIMPLE_HANDLER(field)    \
    do {                         \
        res->field = src->field; \
    } while (0)
#define SERIALIZE_HANDLER(field)                      \
    do {                                              \
        move_in(src->field, &res->field, ctx, alloc); \
    } while (0)

        UNWIND_CODE_FIELDS

#undef SERIALIZE_HANDLER
#undef SIMPLE_HANDLER

        res->co_firstinstr = (_Py_CODEUNIT *)PyBytes_AS_STRING(res->co_code);

        res->co_weakreflist = NULL;
        res->co_extra = NULL;
        res->co_quickened = NULL;

        *target = (PyObject *)res;
        PyObject_GC_UnTrack(*target);
    }
    else {
        assert(false);  // should not reach here
    }

#undef SIMPLE_MOVE_IN
}

#undef UNWIND_CODE_FIELDS

void
_PyMem_SharedMoveIn(PyObject *o)
{
    const PyConfig *conf = _Py_GetConfig();
    if (dumped || (conf->cds_mode & 3) != 2)
        return;

    h->used = ALIEN_TO(sizeof(struct HeapArchiveHeader), 8);
    MoveInContext ctx;
    ctx.size = 0;
    ctx.header = NULL;
    move_in(o, &h->obj, &ctx, _PyMem_SharedMalloc);

    if (ctx.size > 0) {
        h->serialized_count = ctx.size;
        h->serialized_array =
            _PyMem_SharedMalloc(ctx.size * sizeof(HeapSerializedObject));
        for (int idx = 0; ctx.size > 0;
             ctx.size--, ctx.header = ctx.header->next, ++idx) {
            assert((ctx.size == 0) == (ctx.header == NULL));

            h->serialized_array[idx].archive_addr_to_patch =
                ctx.header->archive_addr_to_patch;
            h->serialized_array[idx].obj = ctx.header->obj;
            h->serialized_array[idx].ty = ctx.header->ty;
        }
    }

    dumped = true;
    ftruncate(fd, h->used);
    close(fd);
    fd = 0;
}

static PyObject *obj;

PyObject *
_PyMem_SharedGetObj()
{
    if (!obj) {
        if (h && h->obj) {
            obj = h->obj;
        }
        else {
            obj = Py_None;
        }
    }
    // extra INCREF to make sure no GC happened to archived object
    Py_INCREF(obj);
    return obj;
}

// static PyObject *class_list = NULL;
static FILE *class_list = NULL;

PyObject *
_PyMem_TraceImport(PyObject *pkg)
{
    if (class_list == NULL) {
        const PyConfig *conf = _Py_GetConfig();
        FILE *fp = _Py_wfopen(conf->cds_name_list, L"wb");
        if (fp == NULL) {
            verbose(0, "1");
            return NULL;
        }
        class_list = fp;
    }
    PyObject_Print(pkg, class_list, Py_PRINT_RAW);
    PyObject_Print(PyUnicode_FromString("\n"), class_list, Py_PRINT_RAW);
    return Py_None;
}
