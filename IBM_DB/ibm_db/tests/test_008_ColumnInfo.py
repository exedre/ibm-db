# 
#  Licensed Materials - Property of IBM
#
#  (c) Copyright IBM Corp. 2007-2008
#

import unittest, sys
import ibm_db
import config
from testfunctions import IbmDbTestFunctions

class IbmDbTestCase(unittest.TestCase):

  def test_008_ColumnInfo(self):
    obj = IbmDbTestFunctions()
    obj.assert_expect(self.run_test_008)

  def run_test_008(self):
    op = {ibm_db.ATTR_CASE: ibm_db.CASE_NATURAL}
    conn = ibm_db.connect(config.database, config.user, config.password, op)
    server = ibm_db.server_info( conn )
    if (server.DBMS_NAME[0:3] == 'IDS'):
      result = ibm_db.columns(conn,None,None,"employee")
    else:
      result = ibm_db.columns(conn,None,None,"EMPLOYEE")
    row = ibm_db.fetch_both(result)
    value1 = None
    value2 = None
    value3 = None
    value4 = None
    if (row.has_key('TABLE_NAME')):
      value1 = row['TABLE_NAME']
    if (row.has_key('COLUMN_NAME')):
      value2 = row['COLUMN_NAME']
    if (row.has_key('table_name')):
      value3 = row['table_name']
    if (row.has_key('column_name')):
      value4 = row['column_name']
    print value1
    print value2
    print value3
    print value4

    op = {ibm_db.ATTR_CASE: ibm_db.CASE_UPPER}
    ibm_db.set_option(conn, op, 0)
    if (server.DBMS_NAME[0:3] == 'IDS'):
      result = ibm_db.columns(conn,None,None,"employee")
    else:
      result = ibm_db.columns(conn,None,None,"EMPLOYEE")
    row = ibm_db.fetch_both(result)
    value1 = None
    value2 = None
    value3 = None
    value4 = None
    if (row.has_key('TABLE_NAME')):
      value1 = row['TABLE_NAME']
    if (row.has_key('COLUMN_NAME')):
      value2 = row['COLUMN_NAME']
    if (row.has_key('table_name')):
      value3 = row['table_name']
    if (row.has_key('column_name')):
      value4 = row['column_name']
    print value1
    print value2
    print value3
    print value4
    
    op = {ibm_db.ATTR_CASE: ibm_db.CASE_LOWER}
    ibm_db.set_option(conn, op, 0)
    if (server.DBMS_NAME[0:3] == 'IDS'):
      result = ibm_db.columns(conn,None,None,"employee")
    else:
      result = ibm_db.columns(conn,None,None,"EMPLOYEE")
    row = ibm_db.fetch_both(result)
    value1 = None
    value2 = None
    value3 = None
    value4 = None
    if (row.has_key('TABLE_NAME')):
      value1 = row['TABLE_NAME']
    if (row.has_key('COLUMN_NAME')):
      value2 = row['COLUMN_NAME']
    if (row.has_key('table_name')):
      value3 = row['table_name']
    if (row.has_key('column_name')):
      value4 = row['column_name']
    print value1
    print value2
    print value3
    print value4

#__END__
#__LUW_EXPECTED__
#EMPLOYEE
#EMPNO
#None
#None
#EMPLOYEE
#EMPNO
#None
#None
#None
#None
#EMPLOYEE
#EMPNO
#__ZOS_EXPECTED__
#EMPLOYEE
#EMPNO
#None
#None
#EMPLOYEE
#EMPNO
#None
#None
#None
#None
#EMPLOYEE
#EMPNO
#__SYSTEMI_EXPECTED__
#EMPLOYEE
#EMPNO
#None
#None
#EMPLOYEE
#EMPNO
#None
#None
#None
#None
#EMPLOYEE
#EMPNO
#__IDS_EXPECTED__
#None
#None
#employee
#empno
#employee
#empno
#None
#None
#None
#None
#employee
#empno
