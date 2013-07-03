from AccessControl import ClassSecurityInfo
from AccessControl import Unauthorized
from AccessControl.Permissions import access_contents_information
from AccessControl import getSecurityManager
from Products.ERP5Type.UnrestrictedMethod import UnrestrictedMethod
from OFS.Traversable import NotFound
from Products.DCWorkflow.DCWorkflow import ValidationFailed
from Products.ERP5Type.Globals import InitializeClass
from Products.ERP5Type.Tool.BaseTool import BaseTool
from Products.ERP5Type import Permissions
from Products.ERP5Type.Cache import DEFAULT_CACHE_SCOPE
from Products.ERP5Type.Cache import CachingMethod
from lxml import etree
import time
from Products.ERP5Type.tests.utils import DummyMailHostMixin
try:
  from slapos.slap.slap import Computer
  from slapos.slap.slap import ComputerPartition as SlapComputerPartition
  from slapos.slap.slap import SoftwareInstance
  from slapos.slap.slap import SoftwareRelease
except ImportError:
  # Do no prevent instance from starting
  # if libs are not installed
  class Computer:
    def __init__(self):
      raise ImportError
  class SlapComputerPartition:
    def __init__(self):
      raise ImportError
  class SoftwareInstance:
    def __init__(self):
      raise ImportError
  class SoftwareRelease:
    def __init__(self):
      raise ImportError

from zLOG import LOG, INFO
import xml_marshaller
import StringIO
import pkg_resources
from Products.Vifib.Conduit import VifibConduit
import json
from DateTime import DateTime
from App.Common import rfc1123_date
class SoftwareInstanceNotReady(Exception):
  pass

class TestNetDriveUsageReport(unittest.TestCase):

  def _validateXML(self, to_be_validated, xsd_model):
    """Will validate the xml file"""
    #We parse the XSD model
    xsd_model = StringIO.StringIO(xsd_model)
    xmlschema_doc = etree.parse(xsd_model)
    xmlschema = etree.XMLSchema(xmlschema_doc)

    string_to_validate = StringIO.StringIO(to_be_validated)

    try:
      document = etree.parse(string_to_validate)
    except (etree.XMLSyntaxError, etree.DocumentInvalid) as e:
      LOG('SlapTool::_validateXML', INFO, 
        'Failed to parse this XML reports : %s\n%s' % \
          (to_be_validated, e))
      return False

    if xmlschema.validate(document):
      return True

    return False

  def reportNetDriveUsageFromXML(self, xml):
    "Generate sale packing list from net drive usage report"
    #We retrieve XSD model
    netdrive_usage_model = """
<?xml version="1.0" encoding="UTF-8"?>
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" elementFormDefault="qualified">
  <xs:element name="report">
    <xs:complexType>
      <xs:sequence>
        <xs:element ref="item"  minOccurs="0" maxOccurs="unbounded"/>
      </xs:sequence>
    </xs:complexType>
  </xs:element>
  <xs:element name="item">
    <xs:complexType>
      <xs:sequence>
        <xs:element name="computer" type="xs:string"/>
        <xs:element name="account" type="xs:string"/>
        <xs:element name="date" type="xs:date"/>
        <xs:element name="usage" type="xs:decimal"/>
        <xs:element ref="details"/>
      </xs:sequence>
    </xs:complexType>
  </xs:element>
  <xs:element name="details">
    <xs:complexType>
      <xs:element ref="line" minOccurs="0" maxOccurs="unbounded"/>
    </xs:complexType>
  </xs:element>
  <xs:element name="line">
    <xs:complexType>
      <xs:all>
        <xs:element name="drive" type="xs:string"/>
        <xs:element name="remote" type="xs:string"/>
        <xs:element name="start" type="xs:time"/>
        <xs:element name="duration" type="xs:integer"/>
        <xs:element name="bytes" type="xs:integer"/>
      </xs:all>
    </xs:complexType>
  </xs:element>
</xs:schema>
"""
    if not self._validateXML(xml, netdrive_usage_model):
      raise NotImplementedError('XML file sent by the node is not valid !')
  
    splm = self.getPortalObject().getDefaultModule('Sale Packing List')
    computer_id = ''
  
    # Insert one line
    root = etree.XML(xml)
    for element in root.iter('item'):
      if not computer_id == element[0].text:
        computer_id = element[0].text
        computer = self._getComputerDocument(computer_id)
      domain_account = element[1].text
      report_date = element[2].text
      usage_amount = element[3].text
  
      # Search sale packing list by domain_account and the month of report_date
      year, month, day = report_date.split('-')
      reference="NetDriveUsage-%s-%d.%d" % (domain_account, year, month)
      spl = splm.searchFolder(reference=reference)
      if spl is None:
        spl = splm.newContent( portal_type='Sale Packing List',
                               title='Net Drive Usage - %s' % domain_account,
                               reference=reference,
                               source_value = 'Net Drive',
                               destination_value = domain_account,
                               start_date=DateTime(year, month, 1),
                               )
      spl.newContent( portal_type='Sale Packing List Line',
                      resource_value=sequence.get('resource'),
                      delivery_date=report_date,
                      resource_value=computer_id,
                      quantity=usage_amount)
  
      # step through all steps of packing list workflow
      # spl.confirm()
      # spl.setReady()
      # spl.start()
      # spl.stop()
      # spl.deliver()
    return 'Content properly posted.'
  
  def testUsageReport(self):
    xml = ""
    self.reportNetDriveUsageFromXML(xml)
