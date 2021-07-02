import unittest
from pathlib import Path

from .utils import CdsTestMixin, assert_archive_created, CDSMode, assert_name_list_created


class CdsArchiveTest(CdsTestMixin, unittest.TestCase):
    TEST_ARCHIVE: str
    NAME_LIST: str

    @assert_name_list_created
    def test_create_name_list(self):
        self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_NAME_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 0))
        self.assertIn('encodings', Path(self.NAME_LIST).read_text().split())

    @assert_name_list_created
    @assert_archive_created
    def test_create_archive(self):
        self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_NAME_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 0))
        self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_ARCHIVE_FROM_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 0))

    def test_create_archive_with_no_name_list(self):
        self.assert_python_failure(
            **self.get_cds_env(CDSMode.DUMP_ARCHIVE_FROM_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 0))

    def test_archive_not_exist_warning(self):
        out = self.assert_python_ok(
            **self.get_cds_env(CDSMode.SHARE_ARCHIVE, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assertIn('[sharedheap] open mmap file failed.', out.err.decode())
