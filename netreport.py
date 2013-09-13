# -*- coding: utf-8 -*-
##############################################################################
#
# Copyright (c) 2012 Vifib SARL and Contributors.
# All Rights Reserved.
#
# WARNING: This program as such is intended to be used by professional
# programmers who take the whole responsibility of assessing all potential
# consequences resulting from its eventual inadequacies and bugs
# End users who are looking for a ready-to-use solution with commercial
# guarantees and support are strongly advised to contract a Free Software
# Service Company
#
# This program is Free Software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
##############################################################################
import argparse
import os.path
import slapos.slap.slap
import sqlite3
import sys
import xmlrpclib

from datetime import datetime, date
from lxml import etree
from time import sleep

def parseArgumentTuple():
    parser = argparse.ArgumentParser()
    parser.add_argument("--master-url",
                        help="The master server URL. Mandatory.",
                        required=True)
    parser.add_argument("--computer-id",
                        help="The computer id defined in the server.",
                        required=True)
    parser.add_argument("--cert-file",
                        help="Path to computer certificate file.",
                        default="/etc/slapos/ssl/computer.crt")
    parser.add_argument("--key-file",
                        help="Path to computer key file.",
                        default="/etc/slapos/ssl/computer.key")
    parser.add_argument("--report-interval",
                        help="Interval in seconds to send report to master.",
                        default=300.0)
    parser.add_argument("--report-path",
                        help="Where to save TioXML report.",
                        required=True)
    parser.add_argument("--data-file",
                        help="File used to save report data.",
                        default="net_drive_usage_report.data")
    parser.add_argument("--port",
                        help="RPC Port of SlapMonitor.",
                        default=8008)
    parser.add_argument("--batch",
                        help="If True, send report per day at mid-night. "
                             "Otherwise send report instantly.",
                        default=False)
    option = parser.parse_args()

    # Build option_dict
    option_dict = {}

    for argument_key, argument_value in vars(option).iteritems():
        option_dict.update({argument_key: argument_value})

    return option_dict


class NetDriveUsageReporter(object):

    def __init__(self, option_dict):
      for option, value in option_dict.items():
        setattr(self, option, value)
      self._slap_computer = None
      self._domain_name = None
      self._domain_account = None
      self._config_id = None
      self._report_date = None
      self.report_interval = float(self.report_interval)
      self.initializeDatabase(self.data_file)
      self.slap_monitor_uri = 'http://localhost:%d' % option_dict['port']
      
    def initializeConnection(self):
        connection_dict = {}
        connection_dict['key_file'] = self.key_file
        connection_dict['cert_file'] = self.cert_file
        slap = slapos.slap.slap()
        slap.initializeConnection(self.master_url,
                                  **connection_dict)
        self._slap_computer = slap.registerComputer(self.computer_id)

    def initializeConfigData(self):
        self._domain_account = "SlapMonitor"

        q = self._db.execute
        s = "SELECT _rowid_, report_date FROM config " \
            "WHERE domain_account=? and computer_id=?"
        for r in q(s, (self._domain_account, self.computer_id)):
            self._config_id, self._report_date = r
        else:
            q("INSERT OR REPLACE INTO config"
              "(domain_account, computer_id, report_date)"
              " VALUES (?,?,?)",
              (self._domain_account, self.computer_id, date.today().isoformat()))
        for r in q(s, (self._domain_account, self.computer_id)):
            self._config_id, self._report_date = r

    def run(self):
        self.initializeConfigData()
        self.sendAllReport()
        self.initializeConnection()
        last_timestamp = datetime.now()
        interval = 30.0 if self.report_interval > 60 else (self.report_interval / 2)
        try:
            monitor = xmlrpclib.ServerProxy(self.slap_monitor_uri)
            while True:
                current_timestamp = datetime.now()
                d = current_timestamp - last_timestamp
                if d.seconds < self.report_interval:
                    sleep(interval)
                    continue
                self.insertUsageReport(monitor, last_timestamp.isoformat(), d.seconds)
                self.sendReport()
                last_timestamp = current_timestamp
        except KeyboardInterrupt:
            pass
        finally:
            self._db.close()

    def insertUsageReport(self, monitor, start, duration):
        q = self._db.execute
        for r in monitor.usageReport():
            q( "INSERT INTO net_drive_usage "
               "(config_id, domain_user, drive_letter, remote_folder, "
               " start, duration, usage_bytes )"
               " VALUES (?, ?, ?, ?, ?, ?)",
               (self._config_id, r[0], r[1], r[2], start, duration, r[4] - r[3]))

    def sendAllReport(self):
        """Called at startup of this application, send all report
          in the config table."""
        q = self._db.execute
        for r in q("SELECT _rowid_, computer_id, report_date "
                   "FROM config "
                   "WHERE report_date < date('now')"):
            self._postData(self.generateDailyReport(*r))
        q("UPDATE config SET report_date = date('now') "
          "WHERE report_date < date('now')")

    def sendReport(self):
        # If report_date is not today, then
        #    Generate xml data from local table by report_date
        #    Send xml data to master node
        #    Change report_date to today
        #    (Optional) Move all the reported data to histroy table
        today = date.today().isoformat()
        if (not self.batch) or self._report_date < today:
            self._postData(self.generateDailyReport(self._config_id,
                                                    self.computer_id,
                                                    self._report_date))
            self._db.execute("UPDATE config SET report_date=? where _rowid_=?",
                             (today, self._config_id))

    def _postData(self, report):
        """Send a marshalled dictionary of the net drive usage record
        serialized via_getDict.
        """
        if report is not None:
            name = "netdrive-report-%s.xml" % datetime.now().isoformat()
            etree.ElementTree(report).write(
                os.path.join(self.report_path, name),
                xml_declaration=True
                )

    def initializeDatabase(self, db_path):
        self._db = sqlite3.connect(db_path, isolation_level=None)
        q = self._db.execute
        q("""CREATE TABLE IF NOT EXISTS config (
            domain_account TEXT PRIMARY KEY,
            computer_id TEXT NOT NULL,
            report_date TEXT NOT NULL,
            remark TEXT)""")
        q("""CREATE TABLE IF NOT EXISTS net_drive_usage (
            config_id INTEGER REFERENCES config ( _rowid_ ),
            drive_letter TEXT NOT NULL,
            remote_folder TEXT NOT NULL,
            domain_user TEXT NOT NULL,
            start TEXT DEFAULT CURRENT_TIMESTAMP,
            duration FLOAT NOT NULL,
            usage_bytes INTEGER,
            remark TEXT)""")
        q("""CREATE TABLE IF NOT EXISTS net_drive_usage_history (
            config_id INTEGER REFERENCES config ( _rowid_ ),
            drive_letter TEXT NOT NULL,
            remote_folder TEXT NOT NULL,
            domain_user TEXT NOT NULL,
            start TEXT NOT NULL,
            duration FLOAT NOT NULL,
            usage_bytes INTEGER,
            remark TEXT)""")

    def generateDailyReport(self, config_id, computer_id, report_date, remove=True):
        q = self._db.execute
        report = etree.Element("consumption")
        for r in q("SELECT domain_user, remote_folder, duration, usage_bytes  "
                   "FROM net_drive_usage "
                   "WHERE config_id=? AND strftime('%Y-%m-%d', start)=?",
                   (config_id, report_date)):
            movement = etree.Element('movement')

            element = etree.Element("resource")
            element.text = r[0]
            movement.append(element)

            element = etree.Element("title")
            element.text = 'NetDrive Usage %s' % report_date
            movement.append(element)

            element = etree.Element("reference")
            element.text = etree.Element("domain_user"),
            movement.append(element)

            element = etree.Element("reference")
            element.text = report_date
            movement.append(element)

            element = etree.Element("quantity")
            element.text = str(r[1] * r[2])
            movement.append(element)

            element = etree.Element("price")
            element.text = '0.0'
            movement.append(element)

            element = etree.Element("VAT")
            movement.append(element)

            element = etree.Element("category")
            element.text = "NetDrive"
            movement.append(element)

            report.append(movement)

        if remove:
            q("INSERT INTO net_drive_usage_history "
              "SELECT * FROM net_drive_usage "
              "WHERE config_id=? AND strftime('%Y-%m-%d', start)=?",
              (config_id, report_date))
            q("DELETE FROM net_drive_usage "
              "WHERE config_id=? AND strftime('%Y-%m-%d', start)=?",
              (config_id, report_date))
        return report

def main():
    reporter = NetDriveUsageReporter(parseArgumentTuple())
    reporter.run()

if __name__ == '__main__':
    main()
