# -*- coding: utf-8 -*-

import sys
import os
import tempfile
import shutil
import datetime

sys.path.insert(0, 'slapos.core')
sys.path.insert(0, os.getcwd())

import test.test_support
real_netuse = test.test_support.import_module('netuse')
netreport = test.test_support.import_module('netreport')
threading = test.test_support.import_module('threading')
import unittest

class NetUseTests(unittest.TestCase):

    def test_user_info(self):
        r = real_netuse.userInfo()
        self.assertEquals(len(r), 3)
        self.assertEquals(r, ('JONDY', 'JONDY', 'Administrator'))

    def test_map_drive(self):
        r = real_netuse.mapDrive(r'\\server\path')
        self.assertEquals(r, 'X:')

    def test_usage_report(self):
        r = real_netuse.usageReport()
        self.assertEquals(len(r), 0)
        self.assertEquals(r, [])

    def test_usage_report_server(self):
        r = real_netuse.usageReport('myserver')
        self.assertEquals(len(r), 0)
        self.assertEquals(r, [])

    def test_usage_report_server_is_none(self):
        r = real_netuse.usageReport()
        self.assertEquals(len(r), 0)
        self.assertEquals(r, [])

    def test_usage_report_one_share_folder_to_many_drive(self):
        r = real_netuse.usageReport()
        self.assertEquals(len(r), 0)
        self.assertEquals(r, [])

    def test_usage_report_share_subfolder(self):
        r = real_netuse.usageReport()
        self.assertEquals(len(r), 0)
        self.assertEquals(r, [])

class BaseTestCase(unittest.TestCase):
    def setUp(self):
        self._threads = test.test_support.threading_setup()
        self._db_file = '/tmp/mytest.db'
        options = { 'master_url' : 'http://localhost:12006',
                    'computer_id' : 'COMP-1500',
                    'server_name' : '',
                    'report_interval' : 300,
                    'data_file' : self._db_file,
                    }
        self.reporter = netreport.NetDriveUsageReporter(options)
        self.db = self.reporter._db

    def tearDown(self):
        test.test_support.threading_cleanup(*self._threads)
        test.test_support.reap_children()
        self.db.close()
        if os.path.exists(self._db_file):
            os.remove(self._db_file)

class NetReportTests(BaseTestCase):

    def test_parse_argument(self):
        argv_orig = sys.argv[:]
        sys.argv = ['me',
                    '--master-url', 'http://127.0.0.1',
                    '--computer-id', 'COMP-1500',
                    '--cert-file', '~/certificate',
                    '--key-file', '~/key',
                    '--report-interval', '500',
                    '--data-file', '/tmp/myreporter',
                    ]
        opts = netreport.parseArgumentTuple()
        self.assertEquals(opts,
                          {'cert_file': '~/certificate',
                           'computer_id': 'COMP-1500',
                           'data_file': '/tmp/myreporter',
                           'key_file': '~/key',
                           'master_url': 'http://127.0.0.1',
                           'report_interval': '500',
                           'server_name': ''}
                          )
        sys.argv[:] = argv_orig

    def test_init_config(self):
        self.reporter.initializeConfigData()
        self.assertEquals(self.reporter._config_id, 1)
        self.assertEquals(self.reporter._domain_account, 'JONDY\\Administrator')
        self.assertEquals(self.reporter._report_date, datetime.date.today().isoformat())

    def test_insert_report(self):
        self.reporter.initializeConfigData()
        t = datetime.datetime.now().isoformat()
        self.reporter.insertUsageReport(t, 500)

    def test_send_all_report(self):
        self.reporter.initializeConfigData()
        self.reporter.sendAllReport()

    def test_generate_daily_report_no_data(self):
        self.reporter.initializeConfigData()
        self.reporter.insertUsageReport('2011-01-02 12:23:32', 500)
        xml = self.reporter.generateDailyReport(1,
                                                'COMP-1500',
                                                'SERVER\\JONDY',
                                                '2021-03-05',
                                                )
        self.assertEquals(xml, None)

    def test_generate_daily_report_no_data(self):
        self.reporter.initializeConfigData()
        self.reporter.insertUsageReport('2011-01-02 12:23:32', 500)
        report_date = '2021-03-05'
        config_id = self.reporter._config_id
        q = self.db.execute
        q( "INSERT INTO net_drive_usage "
           "(config_id, drive_letter, remote_folder, "
           " start, duration, usage_bytes )"
           " VALUES (?, ?, ?, ?, ?, ?)",
           (config_id, 'E:', r'\Server\Path', '2021-03-05 12:23:30', 300, 1000))
        xml = self.reporter.generateDailyReport(config_id,
                                                self.reporter.computer_id,
                                                'JONDY\\Adminstrator',
                                                report_date,
                                                )
        self.assertEquals(xml, "<?xml version='1.0' encoding='utf-8'?>\n<report><computer>COMP-1500</computer><account>JONDY\\Adminstrator</account><date>2021-03-05</date><usage>300000.0</usage><details/></report>")

if __name__ == "__main__":
    # unittest.main()
    loader = unittest.TestLoader()
    # loader.testMethodPrefix = 'test_'
    suite1 = loader.loadTestsFromTestCase(NetUseTests)
    suite2 = loader.loadTestsFromTestCase(NetReportTests)
    alltests = unittest.TestSuite([suite1, suite2])
    unittest.TextTestRunner(verbosity=2).run(alltests)
