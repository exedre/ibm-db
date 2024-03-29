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

  def test_151_FetchAssocSelect_02(self):
    obj = IbmDbTestFunctions()
    obj.assert_expect(self.run_test_151)

  def run_test_151(self):
    conn = ibm_db.connect(config.database, config.user, config.password)

    server = ibm_db.server_info( conn )
    if (server.DBMS_NAME[0:3] == 'IDS'):
      op = {ibm_db.ATTR_CASE: ibm_db.CASE_UPPER}
      ibm_db.set_option(conn, op, 0)
    
    result = ibm_db.exec_immediate(conn, "select * from sales")
    
    row = ibm_db.fetch_assoc(result)
    while ( row ):
      #printf("%-10s ",row['SALES_DATE'])
      #printf("%-15s ",row['SALES_PERSON'])
      #printf("%-15s ",row['REGION'])
      #printf("%4s",row['SALES'])
      #puts ""
      if (row['SALES'] == None):
        row['SALES'] = ''
      print "%-10s %-15s %-15s %4s" % (row['SALES_DATE'], row['SALES_PERSON'], row['REGION'], row['SALES'])
      row = ibm_db.fetch_assoc(result)

#__END__
#__LUW_EXPECTED__
#
#1995-12-31 LUCCHESSI       Ontario-South      1
#1995-12-31 LEE             Ontario-South      3
#1995-12-31 LEE             Quebec             1
#1995-12-31 LEE             Manitoba           2
#1995-12-31 GOUNOT          Quebec             1
#1996-03-29 LUCCHESSI       Ontario-South      3
#1996-03-29 LUCCHESSI       Quebec             1
#1996-03-29 LEE             Ontario-South      2
#1996-03-29 LEE             Ontario-North      2
#1996-03-29 LEE             Quebec             3
#1996-03-29 LEE             Manitoba           5
#1996-03-29 GOUNOT          Ontario-South      3
#1996-03-29 GOUNOT          Quebec             1
#1996-03-29 GOUNOT          Manitoba           7
#1996-03-30 LUCCHESSI       Ontario-South      1
#1996-03-30 LUCCHESSI       Quebec             2
#1996-03-30 LUCCHESSI       Manitoba           1
#1996-03-30 LEE             Ontario-South      7
#1996-03-30 LEE             Ontario-North      3
#1996-03-30 LEE             Quebec             7
#1996-03-30 LEE             Manitoba           4
#1996-03-30 GOUNOT          Ontario-South      2
#1996-03-30 GOUNOT          Quebec            18
#1996-03-30 GOUNOT          Manitoba           1
#1996-03-31 LUCCHESSI       Manitoba           1
#1996-03-31 LEE             Ontario-South     14
#1996-03-31 LEE             Ontario-North      3
#1996-03-31 LEE             Quebec             7
#1996-03-31 LEE             Manitoba           3
#1996-03-31 GOUNOT          Ontario-South      2
#1996-03-31 GOUNOT          Quebec             1
#1996-04-01 LUCCHESSI       Ontario-South      3
#1996-04-01 LUCCHESSI       Manitoba           1
#1996-04-01 LEE             Ontario-South      8
#1996-04-01 LEE             Ontario-North       
#1996-04-01 LEE             Quebec             8
#1996-04-01 LEE             Manitoba           9
#1996-04-01 GOUNOT          Ontario-South      3
#1996-04-01 GOUNOT          Ontario-North      1
#1996-04-01 GOUNOT          Quebec             3
#1996-04-01 GOUNOT          Manitoba           7
#__ZOS_EXPECTED__
#
#1995-12-31 LUCCHESSI       Ontario-South      1
#1995-12-31 LEE             Ontario-South      3
#1995-12-31 LEE             Quebec             1
#1995-12-31 LEE             Manitoba           2
#1995-12-31 GOUNOT          Quebec             1
#1996-03-29 LUCCHESSI       Ontario-South      3
#1996-03-29 LUCCHESSI       Quebec             1
#1996-03-29 LEE             Ontario-South      2
#1996-03-29 LEE             Ontario-North      2
#1996-03-29 LEE             Quebec             3
#1996-03-29 LEE             Manitoba           5
#1996-03-29 GOUNOT          Ontario-South      3
#1996-03-29 GOUNOT          Quebec             1
#1996-03-29 GOUNOT          Manitoba           7
#1996-03-30 LUCCHESSI       Ontario-South      1
#1996-03-30 LUCCHESSI       Quebec             2
#1996-03-30 LUCCHESSI       Manitoba           1
#1996-03-30 LEE             Ontario-South      7
#1996-03-30 LEE             Ontario-North      3
#1996-03-30 LEE             Quebec             7
#1996-03-30 LEE             Manitoba           4
#1996-03-30 GOUNOT          Ontario-South      2
#1996-03-30 GOUNOT          Quebec            18
#1996-03-30 GOUNOT          Manitoba           1
#1996-03-31 LUCCHESSI       Manitoba           1
#1996-03-31 LEE             Ontario-South     14
#1996-03-31 LEE             Ontario-North      3
#1996-03-31 LEE             Quebec             7
#1996-03-31 LEE             Manitoba           3
#1996-03-31 GOUNOT          Ontario-South      2
#1996-03-31 GOUNOT          Quebec             1
#1996-04-01 LUCCHESSI       Ontario-South      3
#1996-04-01 LUCCHESSI       Manitoba           1
#1996-04-01 LEE             Ontario-South      8
#1996-04-01 LEE             Ontario-North       
#1996-04-01 LEE             Quebec             8
#1996-04-01 LEE             Manitoba           9
#1996-04-01 GOUNOT          Ontario-South      3
#1996-04-01 GOUNOT          Ontario-North      1
#1996-04-01 GOUNOT          Quebec             3
#1996-04-01 GOUNOT          Manitoba           7
#__SYSTEMI_EXPECTED__
#
#1995-12-31 LUCCHESSI       Ontario-South      1
#1995-12-31 LEE             Ontario-South      3
#1995-12-31 LEE             Quebec             1
#1995-12-31 LEE             Manitoba           2
#1995-12-31 GOUNOT          Quebec             1
#1996-03-29 LUCCHESSI       Ontario-South      3
#1996-03-29 LUCCHESSI       Quebec             1
#1996-03-29 LEE             Ontario-South      2
#1996-03-29 LEE             Ontario-North      2
#1996-03-29 LEE             Quebec             3
#1996-03-29 LEE             Manitoba           5
#1996-03-29 GOUNOT          Ontario-South      3
#1996-03-29 GOUNOT          Quebec             1
#1996-03-29 GOUNOT          Manitoba           7
#1996-03-30 LUCCHESSI       Ontario-South      1
#1996-03-30 LUCCHESSI       Quebec             2
#1996-03-30 LUCCHESSI       Manitoba           1
#1996-03-30 LEE             Ontario-South      7
#1996-03-30 LEE             Ontario-North      3
#1996-03-30 LEE             Quebec             7
#1996-03-30 LEE             Manitoba           4
#1996-03-30 GOUNOT          Ontario-South      2
#1996-03-30 GOUNOT          Quebec            18
#1996-03-30 GOUNOT          Manitoba           1
#1996-03-31 LUCCHESSI       Manitoba           1
#1996-03-31 LEE             Ontario-South     14
#1996-03-31 LEE             Ontario-North      3
#1996-03-31 LEE             Quebec             7
#1996-03-31 LEE             Manitoba           3
#1996-03-31 GOUNOT          Ontario-South      2
#1996-03-31 GOUNOT          Quebec             1
#1996-04-01 LUCCHESSI       Ontario-South      3
#1996-04-01 LUCCHESSI       Manitoba           1
#1996-04-01 LEE             Ontario-South      8
#1996-04-01 LEE             Ontario-North       
#1996-04-01 LEE             Quebec             8
#1996-04-01 LEE             Manitoba           9
#1996-04-01 GOUNOT          Ontario-South      3
#1996-04-01 GOUNOT          Ontario-North      1
#1996-04-01 GOUNOT          Quebec             3
#1996-04-01 GOUNOT          Manitoba           7
#__IDS_EXPECTED__
#
#1995-12-31 LUCCHESSI       Ontario-South      1
#1995-12-31 LEE             Ontario-South      3
#1995-12-31 LEE             Quebec             1
#1995-12-31 LEE             Manitoba           2
#1995-12-31 GOUNOT          Quebec             1
#1996-03-29 LUCCHESSI       Ontario-South      3
#1996-03-29 LUCCHESSI       Quebec             1
#1996-03-29 LEE             Ontario-South      2
#1996-03-29 LEE             Ontario-North      2
#1996-03-29 LEE             Quebec             3
#1996-03-29 LEE             Manitoba           5
#1996-03-29 GOUNOT          Ontario-South      3
#1996-03-29 GOUNOT          Quebec             1
#1996-03-29 GOUNOT          Manitoba           7
#1996-03-30 LUCCHESSI       Ontario-South      1
#1996-03-30 LUCCHESSI       Quebec             2
#1996-03-30 LUCCHESSI       Manitoba           1
#1996-03-30 LEE             Ontario-South      7
#1996-03-30 LEE             Ontario-North      3
#1996-03-30 LEE             Quebec             7
#1996-03-30 LEE             Manitoba           4
#1996-03-30 GOUNOT          Ontario-South      2
#1996-03-30 GOUNOT          Quebec            18
#1996-03-30 GOUNOT          Manitoba           1
#1996-03-31 LUCCHESSI       Manitoba           1
#1996-03-31 LEE             Ontario-South     14
#1996-03-31 LEE             Ontario-North      3
#1996-03-31 LEE             Quebec             7
#1996-03-31 LEE             Manitoba           3
#1996-03-31 GOUNOT          Ontario-South      2
#1996-03-31 GOUNOT          Quebec             1
#1996-04-01 LUCCHESSI       Ontario-South      3
#1996-04-01 LUCCHESSI       Manitoba           1
#1996-04-01 LEE             Ontario-South      8
#1996-04-01 LEE             Ontario-North       
#1996-04-01 LEE             Quebec             8
#1996-04-01 LEE             Manitoba           9
#1996-04-01 GOUNOT          Ontario-South      3
#1996-04-01 GOUNOT          Ontario-North      1
#1996-04-01 GOUNOT          Quebec             3
#1996-04-01 GOUNOT          Manitoba           7
