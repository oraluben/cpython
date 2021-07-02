import functools
import os
import random
import unittest
from enum import Enum

from test.support.script_helper import assert_python_ok, assert_python_failure

import typing as t


class CDSMode(Enum):
    DUMP_NAME_LIST = 'value of this enum is unused, only a placeholder'
    DUMP_ARCHIVE_FROM_LIST = 'DUMP'
    SHARE_ARCHIVE = 'SHARE'
    DEBUG_CREATE_ARCHIVE = 'DEBUG1'
    DEBUG_LOAD_ARCHIVE = 'DEBUG2'


class UtilMixin:
    exists = staticmethod(os.path.exists)
    assert_python_ok = staticmethod(assert_python_ok)
    assert_python_failure = staticmethod(assert_python_failure)

    assertExists = lambda self, file: self.assertTrue(self.exists(file))
    assertNotExists = lambda self, file: self.assertFalse(self.exists(file))


class CdsTestMixin(UtilMixin):
    TEST_ARCHIVE = 'test.img'
    NAME_LIST = 'test.lst'

    def _del_archive(self):
        if os.path.exists(self.TEST_ARCHIVE):
            os.remove(self.TEST_ARCHIVE)
        if os.path.exists(self.NAME_LIST):
            os.remove(self.NAME_LIST)

    setUp = _del_archive
    tearDown = _del_archive

    @staticmethod
    def get_cds_env(mode: CDSMode, archive: str, name_list: str, verbose: int,
                    random_hash_seed: t.Union[bool, int, str] = False):
        env = os.environ.copy()
        env['__cleanenv'] = True  # signal to assert_python not to do a copy
        # of os.environ on its own

        env['PYTHONHASHSEED'] = 'random' if random_hash_seed in (True, 'random') else \
            str(random_hash_seed) if random_hash_seed else '0'

        if mode == CDSMode.DUMP_NAME_LIST:
            env['PYDUMPMODULELIST'] = name_list
        else:
            env['PYCDSMODE'] = mode.value
            env['PYCDSLIST'] = name_list
        env['PYCDSARCHIVE'] = archive
        env['PYCDSVERBOSE'] = str(verbose)

        return env


def assert_archive_created(f: t.Callable):
    @functools.wraps(f)
    def inner(self: unittest.TestCase):
        self.assertIsInstance(self, CdsTestMixin)
        self.assertNotExists(self.TEST_ARCHIVE)

        f(self)

        self.assertExists(self.TEST_ARCHIVE)

    return inner


def assert_name_list_created(f: t.Callable):
    @functools.wraps(f)
    def inner(self: unittest.TestCase):
        self.assertIsInstance(self, CdsTestMixin)
        self.assertNotExists(self.NAME_LIST)

        f(self)

        self.assertExists(self.NAME_LIST)

    return inner


def random_branch():
    return random.choice([True, False])


def random_float():
    f = random.random()
    if f == 0.0:
        return f
    if random_branch():
        f = 1 / f
    return f
