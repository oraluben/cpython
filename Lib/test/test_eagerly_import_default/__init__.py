'''
- Target:
    Add a new sample test that checks Python imports are executed eagerly by default.

- Description:
    We need to confirm one of these two options based on the scenario. Otherwise, this test fails.
        - If it is in default, the imports are executed eagerly.
        - If Lazy Imports is enabled, the imports are NOT executed eagerly.
'''

import os
import sys
import unittest
import test.test_eagerly_import_default.imported_module
from test.test_eagerly_import_default.imported_module2 import print_log
# import test.test_eagerly_import_default.non_imported_module


class LazyImportsEnabledChecker:
    def __init__(self):
        self.enabled = self.check_lazy_imports()

    # confirm if Lazy Imports is enabled or not
    def check_lazy_imports(self):
        # lazy imports is not enabled in default
        enabled = False

        # check if Lazy Imports is turned on
        return (
            self.check_flag() |
            self.check_env_var()
        )

    # check if the env variable PYTHONLAZYIMPORTSALL is set
    def check_env_var(self):
        env_variables = os.environ
        env_var_lazy_imports = "PYTHONLAZYIMPORTSALL"
        if env_var_lazy_imports not in env_variables:
            return False

        # Python's env variables only care about if the value is empty or not
        if env_variables[env_var_lazy_imports] == "":
            return False
        else:
            return True

    # check if -L flag is added to the Python interpreter
    def check_flag(self):
        argvs = sys.argv
        for argv in argvs:
            if argv == "-L":
                return True
        return False

    def is_enabled(self):
        return self.enabled


class TestEagerlyImport(unittest.TestCase):

    def __init__(self, *args, **kwargs):
        super(TestEagerlyImport, self).__init__(*args, **kwargs)
        self.modules = sys.modules

    def test_non_imported_module(self):
        # we did not import non_imported_module, so we should not have this sub-module of non_imported_module
        self.assertFalse("test.test_eagerly_import_default.non_imported_module_sub" in self.modules)

    def test_imported_module(self):
        # we import imported_module at the beginning, so we should have its sub-module in sys.modules
        self.assertTrue("test.test_eagerly_import_default.imported_module_sub" in self.modules)

    def test_imported_module2(self):
        # we import imported_module2 by using different import way at the beginning, but we should still have its sub-module
        self.assertTrue("test.test_eagerly_import_default.imported_module_sub2" in self.modules)


if __name__ == "__main__":

    lazy_imports_checker = LazyImportsEnabledChecker()

    if lazy_imports_checker.is_enabled():
        # no need to check
        # imports should be lazy
        pass
    else:
        # in default
        # imports should be eagerly
        unittest.main()
