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

  def test_060_Tables_01(self):
    obj = IbmDbTestFunctions()
    obj.assert_expectf(self.run_test_060)

  def run_test_060(self):
    conn = ibm_db.connect(config.database, config.user, config.password)

    create = 'CREATE SCHEMA AUTHORIZATION t'
    try:
      result = ibm_db.exec_immediate(conn, create)
    except:
      pass
    
    create = 'CREATE TABLE t.t1( c1 INTEGER, c2 VARCHAR(40))'
    try:
      result = ibm_db.exec_immediate(conn, create)
    except:
      pass
    
    create = 'CREATE TABLE t.t2( c1 INTEGER, c2 VARCHAR(40))'
    try:
      result = ibm_db.exec_immediate(conn, create) 
    except:
      pass
    
    create = 'CREATE TABLE t.t3( c1 INTEGER, c2 VARCHAR(40))'
    try:
      result = ibm_db.exec_immediate(conn, create) 
    except:
      pass
    
    create = 'CREATE TABLE t.t4( c1 INTEGER, c2 VARCHAR(40))'
    try:
      result = ibm_db.exec_immediate(conn, create) 
    except:
      pass
    
    if conn:
      result = ibm_db.tables(conn, None, 'T')
      i = 0
      row = ibm_db.fetch_both(result)
      while ( row ):
        if (i < 4):
          print "/%s/%s" % (row[1], row[2])
        i = i + 1
	row = ibm_db.fetch_both(result)

      ibm_db.exec_immediate(conn, 'DROP TABLE t.t1')
      ibm_db.exec_immediate(conn, 'DROP TABLE t.t2')
      ibm_db.exec_immediate(conn, 'DROP TABLE t.t3')
      ibm_db.exec_immediate(conn, 'DROP TABLE t.t4')

      print "done!"
    else:
      print "no connection: #{ibm_db.conn_errormsg}";    

#__END__
#__LUW_EXPECTED__
#/T/T1
#/T/T2
#/T/T3
#/T/T4
#done!
#__ZOS_EXPECTED__
#/T/T1
#/T/T2
#/T/T3
#/T/T4
#done!
#__SYSTEMI_EXPECTED__
#/T/T1
#/T/T2
#/T/T3
#/T/T4
#done!
#__IDS_EXPECTED__
#/T%s/t1
#/T%s/t2
#/T%s/t3
#/T%s/t4
#done!
