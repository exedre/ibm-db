import os
import sys
import unittest
import StringIO
import re
import glob
import inspect

import ibm_db
import config

class IbmDbTestFunctions(unittest.TestCase):
  
  # See the tests.py comments for this function.
  def setUp(self):
    pass
 
  # This function captures the output of the current test file.
  def capture(self, func):
    buffer = StringIO.StringIO()
    sys.stdout = buffer
    func()
    sys.stdout = sys.__stdout__
    var = buffer.getvalue()
    var = var.replace('\n', '').replace('\r', '')
    return var
  
  # This function grabs the expected output of the current test function,
  #   located at the bottom of the current test file.
  def expected_LUW(self, fileName):
    fileHandle = open(fileName, 'r')
    fileInput = fileHandle.read().split('#__LUW_EXPECTED__')[-1].split('#__ZOS_EXPECTED__')[0].replace('\n', '').replace('#', '')
    fileHandle.close()
    return fileInput
    
  # This function compares the captured outout with the expected out of
  #   the current test file.
  def assert_expect(self, testFuncName):
    callstack = inspect.stack(0)
    try:
      prepconn = ibm_db.connect(config.database, config.user, config.password)
      server = ibm_db.server_info(prepconn)
      if (server.DBMS_NAME == "AS"):
          self.fail("OS i/5 not supported yet")
      elif (server.DBMS_NAME == "DB2"):
          self.fail("z/OS not supported yet")
      elif (server.DBMS_NAME == "IDS"):
          self.fail("IDS not supported yet")
      else:
          self.assertEqual(self.capture(testFuncName), self.expected_LUW(callstack[1][1]))
          ibm_db.close
    finally:
      del callstack

  # This function will compare using Regular Expressions
  # based on the servre
  def assert_expectf(self, testFuncName):
    callstack = inspect.stack(0)
    try:
      prepconn = ibm_db.connect(config.database, config.user, config.password)
      server = ibm_db.server_info(prepconn)
      if (server.DBMS_NAME == "AS"):
          self.fail("OS i/5 not supported yet")
      elif (server.DBMS_NAME == "DB2"):
          self.fail("z/OS not supported yet")
      elif (server.DBMS_NAME == "IDS"):
          self.fail("IDS not supported yet")
      else:
          pattern = self.expected_LUW(callstack[1][1])
      
      sym = ['\[','\]']
      for chr in sym:
          pattern = re.sub(chr, '\\' + chr, pattern)
      
      pattern = re.sub('%s', '.*', pattern)
      pattern = re.sub('(%d)', '\\(\\d+\\)', pattern)
      result = re.match(pattern, self.capture(testFuncName))
      
      self.assertNotEqual(result, None)
      ibm_db.close
    finally:
      del callstack
      
  #def assert_throw_blocks(self, testFuncName):
  #  callstack = inspect.stack(0)
  #  try:

  # This function needs to be declared here, regardless of if there 
  #   is any body to this function
  def runTest(self):
    pass
