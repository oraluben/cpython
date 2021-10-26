CDS for Python
~~~~~~~~~~~~~~

Introduction
============

CDS for Python is an extension for CPython,
to speed up python application startup time.

The design is inspired by the Application Class-Data Sharing (AppCDS_) feature,
introduced in OpenJDK.
AppCDS allows a set of application classes to be pre-processed into a shared archive file,
which can then be memory-mapped at runtime to reduce startup time and memory footprint.

CDS for Python use mmap to store and share data likely to not change.
Detail design and implementation will be elaborate below.

Design & Implementation
=======================

CDS for Python now caches and shares bytecode of modules in Python,
in a memory-mapped file we'll be referring as the archive.

:code:`importlib` in Python
---------------------------
Roughly speaking,
importing a module in Python includes `several steps <https://docs.python.org/3/library/importlib.html#approximating-importlib-import-module>`_:

1. Find the "spec", which includes the meta data of py[c] files;
2. Get the code object from "spec", this involves compilation;
3. Run the code object and get the ready module.

The first two steps require IO operations and Python compilation,
while CDS for Python can skip these time-consuming steps.

CDS Importing
-------------
The workflow of CDS for Python could be briefly divided into three parts/roles:

1. tracer is a normal python process that runs python program
and record imported modules during runtime;
2. dumper is a special python instances that only
creates the archive from the list of modules dumped by tracer;
3. loader is a normal python process that will try to
import modules from mmap-ed archive.

CDS Archive
-----------
CDS creates and later load a mmap file as the archive,
which stores deep-copied name and code object of each module.
When a python program is run as a loader,
CDS will inject into importing logic and find the corresponding
code object of a module needed to be imported,
if present in the archive,
without reading source file or compiling.

The data structure of archive is designed to allow Python process
to read directly without deserialization.

Implementation
--------------

The current implementation is combined by two parts:

Runtime support in :code:`importlib`
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
We patched python's import mechanism in :func:`patch_import_paths()`.

Low-level mechanism
^^^^^^^^^^^^^^^^^^^

Memory layout of archive and mmap region::

   +-0x0-|-0x4-|-0x8-|-0xa-+------------ <- 0x280000000
   | (1) | (2) | (3) | (4) |
   | (5) | (6) | (7) | (8) |
   | (9) |                 |
   +-----------------------+
   :          (a)          :
   +-----------------------+

   # (1): mmap address for runtime verification, is supposed to be 0x280000000;
   # (2) - (5): address of :c:data:`Py_None`, :c:data:`Py_True`, :c:data:`Py_False`, :c:data:`Py_Ellipsis`, respectively;
   # (6): size of archive;
   # (7): pointer-in-archive of shared object;
   # (8): size of serialized objects;
   # (9): header serialized objects;
   # (a): object storage.

   * Serialization is disabled by default


To support sharing code object of python modules,
a subset of python objects is sufficient.
More specifically,
only
:const:`None` (:c:data:`Py_None`),
:const:`True` (:c:data:`Py_True`),
:const:`False` (:c:data:`Py_False`),
:const:`Ellipsis` (:c:data:`Py_Ellipsis`),
and instances of
:class:`bytes` (:c:type:`PyBytesObject`),
:class:`code` (:c:type:`PyCodeObject`),
:class:`complex` (:c:type:`PyComplexObject`),
:class:`float` (:c:type:`PyFloatObject`),
:class:`int` (:c:type:`PyLongObject`),
:class:`frozenset` (:c:type:`PyFrozenSetObject`),
:class:`tuple` (:c:type:`PyTupleObject`),
:class:`str` (:c:type:`PyUnicodeObject`),
are necessary to store code object of python modules.

Pointer of constants will be copied as-is
and updated by loader process.
Instances will be deep-copied to archive.
Special cases are hashable objects (:class:`str`/:class:`bytes`, :class:`tuple`)
and container based on hash of objects (:class:`frozenset`, and :class:`dict` which we don't plan to support for now).

For hashable objects,
we set their hash to "not set" in archive,
and Python will rehash them on demand.

For containers
whose object layout relies on hashes of stored objects,
we can either convert them to tuples,
or use a serialization mechanism (see :c:macro:`CDS_ENABLE_SERIALIZE`)
to support their in-archive storage.

Functions
---------

.. function:: patch_import_paths()

   Patch CDS mechanism when initializing Python instances.

   :source:`Lib/importlib/_bootstrap_external.py`

.. c:type:: HeapArchiveHeader

   WIP

   +--------------------------+-------------------------+--------------------------------+
   | Field                    | C Type                  | Meaning                        |
   +==========================+=========================+================================+
   | :attr:`mapped_addr`      | void \*                 | mmap address for verification  |
   |                          |                         | purpose, should be 0x280000000 |
   +--------------------------+-------------------------+--------------------------------+
   | :attr:`none_addr`        | void \*                 | Address of :c:data:`Py_None`   |
   +--------------------------+-------------------------+--------------------------------+
   | :attr:`true_addr`        | void \*                 |                                |
   +--------------------------+-------------------------+--------------------------------+
   | :attr:`false_addr`       | void \*                 |                                |
   +--------------------------+-------------------------+--------------------------------+
   | :attr:`ellipsis_addr`    | void \*                 |                                |
   +--------------------------+-------------------------+--------------------------------+
   | :attr:`obj`              | PyObject \*             |                                |
   +--------------------------+-------------------------+--------------------------------+
   | :attr:`serialized_count` | int                     |                                |
   +--------------------------+-------------------------+--------------------------------+
   | :attr:`serialized_array` | HeapSerializedObject \* |                                |
   +--------------------------+-------------------------+--------------------------------+

Usage
==========

The design and implementation are still evolving,
meanwhile the API seems to meet our requirement and be stable enough.

Example of CDS features could be found in
:code:`run.sh`.

Simple Usage
------------

Behavior of CDS are controlled by several environment variables.

PYCDSMODE={DUMP,SHARE}
PYCDSARCHIVE
PYCDSLIST

PYCDSVERBOSE

Internal APIs
-------------
:code:`sys.shm_move_in`
:code:`sys.shm_getobj`

Related Projects
================

PyICE

PyOxidized & oxidized-importer

.. _AppCDS: https://openjdk.java.net/jeps/310
