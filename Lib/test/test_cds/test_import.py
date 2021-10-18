import unittest

from .utils import CdsTestMixin, assert_archive_created, CDSMode, assert_name_list_created

# todo: verify verbose messages.


try:
    import lzma
except ImportError:
    lzma = None


class CdsImportTest(CdsTestMixin, unittest.TestCase):
    @assert_archive_created
    @assert_name_list_created
    def test_dump_import(self):
        self.assert_python_ok(
            '-c', f'import json',
            **self.get_cds_env(CDSMode.DUMP_NAME_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_ARCHIVE_FROM_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        out = self.assert_python_ok(
            '-c', f'import sys; print(dict(sys.shm_getobj()).keys())',
            **self.get_cds_env(CDSMode.DEBUG_LOAD_ARCHIVE, self.TEST_ARCHIVE, self.TEST_ARCHIVE, 2))
        self.assertIn("'json'", out.out.decode())

    @assert_archive_created
    @assert_name_list_created
    def test_import(self):
        self.assert_python_ok(
            '-c', f'import json',
            **self.get_cds_env(CDSMode.DUMP_NAME_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_ARCHIVE_FROM_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assert_python_ok(
            '-c', f'import json',
            **self.get_cds_env(CDSMode.SHARE_ARCHIVE, self.TEST_ARCHIVE, self.NAME_LIST, 2))

    @assert_archive_created
    @assert_name_list_created
    def test_import_subpackage_not_in_archive(self):
        self.assert_python_ok(
            '-c', f'import xml',
            **self.get_cds_env(CDSMode.DUMP_NAME_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_ARCHIVE_FROM_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assert_python_ok(
            '-c', f'import xml.dom',
            **self.get_cds_env(CDSMode.SHARE_ARCHIVE, self.TEST_ARCHIVE, self.NAME_LIST, 2))

    @assert_archive_created
    @assert_name_list_created
    def test_cached_import_result(self):
        printer = "import xml\nfor i in ('__package__', '__file__', '__path__'):\n    print(xml.__dict__.get(i, None))"

        trace_out = self.assert_python_ok(
            '-c', printer,
            **self.get_cds_env(CDSMode.DUMP_NAME_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_ARCHIVE_FROM_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 2))
        replay_out = self.assert_python_ok(
            '-c', printer,
            **self.get_cds_env(CDSMode.SHARE_ARCHIVE, self.TEST_ARCHIVE, self.NAME_LIST, 2))

        self.assertEqual(trace_out.out.decode(), replay_out.out.decode())

    @unittest.skipIf(lzma is not None, 'test only works when lzma is not built.')
    @assert_archive_created
    @assert_name_list_created
    def test_fail_import(self):
        self.assert_python_ok(
            '-c', 'import zipfile',
            **self.get_cds_env(CDSMode.DUMP_NAME_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 1))
        # fixme: PYTHONHASHSEED=0 will trigger a crash here
        # don't know it's python or CDS's issue.
        out = self.assert_python_ok(
            **self.get_cds_env(CDSMode.DUMP_ARCHIVE_FROM_LIST, self.TEST_ARCHIVE, self.NAME_LIST, 1))
        self.assertIn('lzma is re-imported', out.err.decode())
        self.assert_python_ok(
            '-c', 'import zipfile',
            **self.get_cds_env(CDSMode.SHARE_ARCHIVE, self.TEST_ARCHIVE, self.NAME_LIST, 2))
