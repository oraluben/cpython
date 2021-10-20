#include "sharedheap.h"

#define FAST_PATCH 1

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <pycore_gc.h>

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
        else {
            if (not_serialization) {
                Py_FatalError("");
            }
            archivepatchfunc p = ty->tp_patch;
            if (p == NULL || ty->tp_patch(ref, shift) != NULL) {
                Py_FatalError("todo message.");
            }
        }
    }
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
    // reverse order to make sure leaf objects get deserialized first.
    for (int i = h->serialized_count - 1; i >= 0; --i) {
        HeapSerializedObject *serialized = &h->serialized_array[i];
        assert(_PyMem_IsShared(serialized->archive_addr_to_patch));
        assert(*serialized->archive_addr_to_patch == NULL);
        PyTypeObject *ty = UNSHIFT(serialized->ty, shift, PyTypeObject);
        verbose(0, "deserializing: %s", ty->tp_name);
        assert(ty->tp_patch);
        PyObject *real = ty->tp_patch(serialized->obj, shift);
        assert(real != NULL);
        *((PyObject **)serialized->archive_addr_to_patch) = real;
    }
}

static void *
_PyMem_SharedMalloc(size_t size)
{
    n_alloc++;
    if (!size)
        size = 1;
    // Extra memory for PyGC_Head,
    // otherwise previous object will be overwritten by GC
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

void
move_in(PyObject *op, PyObject **target, MoveInContext *ctx,
        void *(*alloc)(size_t))
{
    *target = NULL;
    if (op == NULL) {
        return;
    }
    PyTypeObject *ty = op->ob_type;
    if (ty->tp_move_in == NULL) {
        char buf[128];
        sprintf(buf, "move_in not supported for type: %s\n", ty->tp_name);
        Py_FatalError(buf);
    }
    else if (ty == &PyFrozenSet_Type) {
        // > (*) For now, I hacked around frozenset by generating a tuple. This
        //  works for "if x in {2, 3, 5}" and that's the only context where a
        //  frozenset can occur in a code object, AFAICT (it's put there by the
        //  AST optimizer).
        // >
        // > Currently the generated code is about 50x the size of the Python
        //  source (counting lines).
        // https://github.com/faster-cpython/ideas/issues/84#issuecomment-902004406
        PyTuple_Type.tp_move_in(PySequence_Tuple(op), target, ctx, alloc);
    }
    else {
        assert(ty->tp_move_in != FAKE_MOVE_IN);
        ty->tp_move_in(op, target, ctx, alloc);
    }
}

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
