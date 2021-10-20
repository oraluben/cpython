import sys

import unittest

from .utils import assert_archive_created
from .test_serialize_basic import SerializeTestMixin

Py_DEBUG = hasattr(sys, 'gettotalrefcount')


class CdsSerializeComplexTest(SerializeTestMixin, unittest.TestCase):
    @unittest.skipIf(Py_DEBUG, 'frozenset will be serialized in debug mode.')
    @assert_archive_created
    def test_frozenset_as_tuple(self):
        self.run_serialize_test(frozenset({1, 2, 3}), repr((1, 2, 3)))

    @unittest.skipIf(not Py_DEBUG, 'frozenset will be serialized in debug mode.')
    @assert_archive_created
    def test_frozenset(self):
        self.run_serialize_test(frozenset({1, 2, 3}))

    @assert_archive_created
    def test_deep_tuple(self):
        t = (1, 2, 3)
        for i in range(100):
            t = (t, i, 1)
        self.run_serialize_test(t)
