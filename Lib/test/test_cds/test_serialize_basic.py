import random
import string
import unittest

from .utils import CdsTestMixin, assert_archive_created, random_float, CDSMode


class SerializeTestMixin(CdsTestMixin):
    def run_serialize_test(self, value, oracle=None):
        s = repr(value)
        if oracle is None:
            oracle = s

        self.assert_python_ok(
            '-c', f'import sys; sys.shm_move_in({s})',
            **self.get_cds_env(CDSMode.DEBUG_CREATE_ARCHIVE, self.TEST_ARCHIVE, self.NAME_LIST, 0))

        out = self.assert_python_ok(
            '-c', 'import sys; print(repr(sys.shm_getobj()))',
            **self.get_cds_env(CDSMode.DEBUG_LOAD_ARCHIVE, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assertEqual(oracle, out.out.decode().strip())


class CdsSerializeBasicTest(SerializeTestMixin, unittest.TestCase):
    TEST_ARCHIVE: str

    @assert_archive_created
    def test_none(self):
        self.run_serialize_test(None)

    @assert_archive_created
    def test_long(self):
        for i in (0, 1, 10, 100, 2 ** 100):
            self.run_serialize_test(i)
            if i != 0:
                self.run_serialize_test(-i)

    @assert_archive_created
    def test_bool_true(self):
        self.run_serialize_test(True)

    @assert_archive_created
    def test_bool_false(self):
        self.run_serialize_test(False)

    @assert_archive_created
    def test_string_ascii(self):
        self.run_serialize_test("")
        self.run_serialize_test(string.printable)
        self.run_serialize_test(''.join(random.choice(string.printable) for _ in range(10000)))

    @assert_archive_created
    def test_string_unicode(self):
        # From https://stackoverflow.com/a/21666621
        include_ranges = [
            (0x0021, 0x0021),
            (0x0023, 0x0026),
            (0x0028, 0x007E),
            (0x00A1, 0x00AC),
            (0x00AE, 0x00FF),
            (0x0100, 0x017F),
            (0x0180, 0x024F),
            (0x2C60, 0x2C7F),
            (0x16A0, 0x16F0),
            (0x0370, 0x0377),
            (0x037A, 0x037E),
            (0x0384, 0x038A),
            (0x038C, 0x038C),
        ]
        chars = [chr(c) for (start, end) in include_ranges for c in range(start, end + 1)]
        for _ in range(10):
            self.run_serialize_test(''.join(random.choice(chars) for _ in range(10000)))

    @assert_archive_created
    def test_float(self):
        self.run_serialize_test(0.0)
        self.run_serialize_test(-0.0)
        for _ in range(10):
            r = random_float()
            self.run_serialize_test(r)
            self.run_serialize_test(-r)

    @assert_archive_created
    def test_complex(self):
        for _ in range(10):
            self.run_serialize_test(random_float() + random_float() * 1j)

    @assert_archive_created
    def test_bytes(self):
        self.run_serialize_test(''.join(random.choice(string.printable) for _ in range(10000)).encode())

    @assert_archive_created
    def test_tuple(self):
        self.run_serialize_test(())
        self.run_serialize_test((1,))
