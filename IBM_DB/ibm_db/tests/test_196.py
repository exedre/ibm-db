# 
#  Licensed Materials - Property of IBM
#
#  (c) Copyright IBM Corp. 2007
#
# NOTE: IDS does not support XML as a native datatype (test is invalid for IDS)

import unittest, sys
import ibm_db
import config
from testfunctions import IbmDbTestFunctions

class IbmDbTestCase(unittest.TestCase):

  def test_196(self):
    obj = IbmDbTestFunctions()
    obj.assert_expectf(self.run_test_196)

  def run_test_196(self):
    conn = ibm_db.connect(config.database, config.user, config.password)
    server = ibm_db.server_info( conn )

    if ((server.DBMS_NAME[0:3] != 'IDS') and (server.DBMS_NAME[0:2] != "AS")):
      try:
          rc = ibm_db.exec_immediate(conn, "DROP TABLE xml_test")
      except:
          pass
      rc = ibm_db.exec_immediate(conn, "CREATE TABLE xml_test (id INTEGER, data VARCHAR(50), xmlcol XML)")
      rc = ibm_db.exec_immediate(conn, "INSERT INTO xml_test (id, data, xmlcol) values (1, 'xml test 1', '<address><street>12485 S Pine St.</street><city>Olathe</city><state>KS</state><zip>66061</zip></address>')")

      sql =  "SELECT * FROM xml_test"
      stmt = ibm_db.prepare(conn, sql)
      ibm_db.execute(stmt)
      result = ibm_db.fetch_both(stmt)
      while( result ):
        print "Result ID:", result[0]
        print "Result DATA:", result[1]
        print "Result XMLCOL:", result[2]
        result = ibm_db.fetch_both(stmt)

      sql = "SELECT XMLSERIALIZE(XMLQUERY('for $i in $t/address where $i/city = \"Olathe\" return <zip>{$i/zip/text()}</zip>' passing c.xmlcol as \"t\") AS CLOB(32k)) FROM xml_test c WHERE id = 1"
      stmt = ibm_db.prepare(conn, sql)
      ibm_db.execute(stmt)
      result = ibm_db.fetch_both(stmt)
      while( result ):
        print "Result from XMLSerialize and XMLQuery:", result[0]
        result = ibm_db.fetch_both(stmt)

      sql = "select xmlquery('for $i in $t/address where $i/city = \"Olathe\" return <zip>{$i/zip/text()}</zip>' passing c.xmlcol as \"t\") from xml_test c where id = 1"
      stmt = ibm_db.prepare(conn, sql)
      ibm_db.execute(stmt)
      result = ibm_db.fetch_both(stmt)
      while( result ):
        print "Result from only XMLQuery:", result[0]
        result = ibm_db.fetch_both(stmt)
    else:
      print 'Native XML datatype is not supported.'

#__END__
#__LUW_EXPECTED__
#Result ID: 1
#Result DATA: xml test 1
#Result XMLCOL:%s<address><street>12485 S Pine St.</street><city>Olathe</city><state>KS</state><zip>66061</zip></address>
#Result from XMLSerialize and XMLQuery: <zip>66061</zip>
#Result from only XMLQuery:%s<zip>66061</zip>
#__ZOS_EXPECTED__
#Result ID: 1
#Result DATA: xml test 1
#Result XMLCOL:%s<address><street>12485 S Pine St.</street><city>Olathe</city><state>KS</state><zip>66061</zip></address>
#Result from XMLSerialize and XMLQuery: <zip>66061</zip>
#Result from only XMLQuery:%s<zip>66061</zip>
#__SYSTEMI_EXPECTED__
#Native XML datatype is not supported.
#__IDS_EXPECTED__
#Native XML datatype is not supported.
