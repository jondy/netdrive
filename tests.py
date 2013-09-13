# -*- coding: utf-8 -*-

import sys
import os
import tempfile
import shutil
import datetime

sys.path.insert(0, os.getcwd())

import test.test_support
real_netuse = test.test_support.import_module('netuse')
threading = test.test_support.import_module('threading')
import unittest

class NetUseTests(unittest.TestCase):

    def test_user_info(self):
        r = real_netuse.userInfo()
        self.assertEquals(len(r), 3)
        self.assertEquals(r, ('SERVER', 'DOMAIN', 'Administrator'))

    def test_list_drive(self):
        r = real_netuse.listNetDrive()
        self.assertEquals(r, [('Z:', r'\\server\path', 'OK', r'domain\user')])
        
    def test_map_drive(self):
        r = real_netuse.mapNetDrive(r'\\server\path')
        self.assertEquals(r, 'Z:')
        r = real_netuse.mapNetDrive(r'\\server\path', 'X:')
        self.assertEquals(r, 'X:')
        r = real_netuse.mapNetDrive(r'\\server\path', 'T:', 'jondy.zhao', '123')
        self.assertEquals(r, 'T:')

    def test_usage_report(self):
        r = real_netuse.usageReport('Z:')
        self.assertEquals(r, (10240L, 102400L))

    def test_remove_drive(self):
        real_netuse.removeNetDrive('Z:')
        real_netuse.removeNetDrive('Z:', force=False)

if __name__ == "__main__":
    # unittest.main()
    loader = unittest.TestLoader()
    # loader.testMethodPrefix = 'test_'
    suite = loader.loadTestsFromTestCase(NetUseTests)
    alltests = unittest.TestSuite([suite])
    unittest.TextTestRunner(verbosity=2).run(alltests)
