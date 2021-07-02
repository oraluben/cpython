import unittest

from .utils import assert_archive_created
from .test_serialize_basic import SerializeTestMixin


class CdsSerializeComplexTest(SerializeTestMixin, unittest.TestCase):
    @assert_archive_created
    def test_frozenset(self):
        self.run_serialize_test(frozenset({1, 2, 3}), repr((1, 2, 3)))
