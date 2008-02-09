/*
+--------------------------------------------------------------------------+
| Licensed Materials - Property of IBM                                     |
|                                                                          |
| (C) Copyright IBM Corporation 2006, 2007, 2008                           |
+--------------------------------------------------------------------------+
| This module complies with SQLAlchemy 0.4 and is                          |
| Licensed under the Apache License, Version 2.0 (the "License");          |
| you may not use this file except in compliance with the License.         |
| You may obtain a copy of the License at                                  |
| http://www.apache.org/licenses/LICENSE-2.0 Unless required by applicable |
| law or agreed to in writing, software distributed under the License is   |
| distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY |
| KIND, either express or implied. See the License for the specific        |
| language governing permissions and limitations under the License.        |
+--------------------------------------------------------------------------+
| Authors: Manas Dadarkar, Salvador Ledezma, Sushant Koduru,               |
|          Lynh Nguyen, Kanchana Padmanabhan, Dan Scott, Helmut Tessarek,  |
|          Sam Ruby, Kellen Bombardier, Tony Cairns                        |
+--------------------------------------------------------------------------+
*/

#define MODULE_RELEASE "0.2.5"

#include <Python.h>
#include "ibm_db.h"
#include <ctype.h>

/* True global resources - no need for thread safety here */
static struct _ibm_db_globals *ibm_db_globals;

static void _python_ibm_db_check_sql_errors( SQLHANDLE handle, SQLSMALLINT hType, int rc, int cpy_to_global, char* ret_str, int API, SQLSMALLINT recno );
static int _python_ibm_db_assign_options( void* handle, int type, long opt_key, PyObject *data );

static int is_systemi, is_informix;      /* 1 == TRUE; 0 == FALSE; */

/* Defines a linked list structure for caching param data */
typedef struct _param_cache_node {
   SQLSMALLINT data_type;        /* Datatype */
   SQLUINTEGER param_size;       /* param size */
   SQLSMALLINT nullable;         /* is Nullable */
   SQLSMALLINT scale;            /* Decimal scale */
   SQLUINTEGER file_options;     /* File options if PARAM_FILE */
   SQLINTEGER   bind_indicator;  /* indicator variable for SQLBindParameter */
   int       param_num;          /* param number in stmt */
   int       param_type;         /* Type of param - INP/OUT/INP-OUT/FILE */
   int       size;               /* Size of param */
   PyObject  *var_pyvalue;       /* bound variable value */
   long      ivalue;             /* Temp storage value */
   double   fvalue;              /* Temp storage value */
   char      *svalue;            /* Temp storage value */
   struct _param_cache_node *next; /* Pointer to next node */
} param_node;

typedef struct _conn_handle_struct {
   PyObject_HEAD
   SQLHANDLE henv;
   SQLHANDLE hdbc;
   long auto_commit;
   long c_bin_mode;
   long c_case_mode;
   long c_cursor_type;
   int handle_active;
   SQLSMALLINT error_recno_tracker;
   SQLSMALLINT errormsg_recno_tracker;
   int flag_pconnect; /* Indicates that this connection is persistent */
} conn_handle;

static void _python_ibm_db_free_conn_struct(conn_handle *handle);

static PyTypeObject conn_handleType = {
      PyObject_HEAD_INIT(NULL)
      0,                                     /*ob_size*/
      "ibm_db.IBM_DBConnection",             /*tp_name*/
      sizeof(conn_handle),                   /*tp_basicsize*/
      0,                                     /*tp_itemsize*/
      (destructor)_python_ibm_db_free_conn_struct, /*tp_dealloc*/
      0,                                     /*tp_print*/
      0,                                     /*tp_getattr*/
      0,                                     /*tp_setattr*/
      0,                                     /*tp_compare*/
      0,                                     /*tp_repr*/
      0,                                     /*tp_as_number*/
      0,                                     /*tp_as_sequence*/
      0,                                     /*tp_as_mapping*/
      0,                                     /*tp_hash */
      0,                                     /*tp_call*/
      0,                                     /*tp_str*/
      0,                                     /*tp_getattro*/
      0,                                     /*tp_setattro*/
      0,                                     /*tp_as_buffer*/
      Py_TPFLAGS_DEFAULT,                    /*tp_flags*/
      "IBM DataServer connection object",    /* tp_doc */
      0,                                     /* tp_traverse */
      0,                                     /* tp_clear */
      0,                                     /* tp_richcompare */
      0,                                     /* tp_weaklistoffset */
      0,                                     /* tp_iter */
      0,                                     /* tp_iternext */
      0,                                     /* tp_methods */
      0,                                     /* tp_members */
      0,                                     /* tp_getset */
      0,                                     /* tp_base */
      0,                                     /* tp_dict */
      0,                                     /* tp_descr_get */
      0,                                     /* tp_descr_set */
      0,                                     /* tp_dictoffset */
      0,                                     /* tp_init */
};



typedef union {
   SQLINTEGER i_val;
   SQLDOUBLE d_val;
   SQLFLOAT f_val;
   SQLSMALLINT s_val;
   SQLCHAR *str_val;
} ibm_db_row_data_type;

typedef struct {
   SQLINTEGER out_length;
   ibm_db_row_data_type data;
} ibm_db_row_type;

typedef struct _ibm_db_result_set_info_struct {
   SQLCHAR    *name;
   SQLSMALLINT type;
   SQLUINTEGER size;
   SQLSMALLINT scale;
   SQLSMALLINT nullable;
   SQLINTEGER lob_loc;
   SQLINTEGER loc_ind;
   SQLSMALLINT loc_type;
} ibm_db_result_set_info;

typedef struct _row_hash_struct {
   PyObject *hash;
} row_hash_struct;

typedef struct _stmt_handle_struct {
	PyObject_HEAD
   SQLHANDLE hdbc;
   SQLHANDLE hstmt;
   long s_bin_mode;
   long cursor_type;
   long s_case_mode;
   SQLSMALLINT error_recno_tracker;
   SQLSMALLINT errormsg_recno_tracker;

   /* Parameter Caching variables */
   param_node *head_cache_list;
   param_node *current_node;

   int num_params;          /* Number of Params */
   int file_param;          /* if option passed in is FILE_PARAM */
   int num_columns;
   ibm_db_result_set_info *column_info;
   ibm_db_row_type *row_data;
} stmt_handle;

static void _python_ibm_db_free_stmt_struct(stmt_handle *handle);

static PyTypeObject stmt_handleType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size            */
    "ibm_db.IBM_DBStatement", /*tp_name             */
    sizeof(stmt_handle), /*tp_basicsize             */
    0,                         /*tp_itemsize        */
    (destructor)_python_ibm_db_free_stmt_struct, /*tp_dealloc   */
    0,                         /*tp_print           */
    0,                         /*tp_getattr         */
    0,                         /*tp_setattr         */
    0,                         /*tp_compare         */
    0,                         /*tp_repr            */
    0,                         /*tp_as_number       */
    0,                         /*tp_as_sequence     */
    0,                         /*tp_as_mapping      */
    0,                         /*tp_hash            */
    0,                         /*tp_call            */
    0,                         /*tp_str             */
    0,                         /*tp_getattro        */
    0,                         /*tp_setattro        */
    0,                         /*tp_as_buffer       */
    Py_TPFLAGS_DEFAULT,        /*tp_flags           */
    "IBM DataServer cursor object", /* tp_doc       */
    0,                         /* tp_traverse       */
    0,                         /* tp_clear          */
    0,                         /* tp_richcompare    */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter           */
    0,                         /* tp_iternext       */
    0,                         /* tp_methods        */
    0,                         /* tp_members        */
    0,                         /* tp_getset         */
    0,                         /* tp_base           */
    0,                         /* tp_dict           */
    0,                         /* tp_descr_get      */
    0,                         /* tp_descr_set      */
    0,                         /* tp_dictoffset     */
    0,                         /* tp_init           */
};

/* equivalent functions on different platforms */
#ifdef _WIN32
#define STRCASECMP stricmp
#else
#define STRCASECMP strcasecmp
#endif

static void python_ibm_db_init_globals(struct _ibm_db_globals *ibm_db_globals) {
   /* env handle */
   ibm_db_globals->bin_mode = 1;

   memset(ibm_db_globals->__python_conn_err_msg, 0, DB2_MAX_ERR_MSG_LEN);
   memset(ibm_db_globals->__python_stmt_err_msg, 0, DB2_MAX_ERR_MSG_LEN);
   memset(ibm_db_globals->__python_conn_err_state, 0, SQL_SQLSTATE_SIZE + 1);
   memset(ibm_db_globals->__python_stmt_err_state, 0, SQL_SQLSTATE_SIZE + 1);
}

static PyObject *persistent_list;

char *estrdup(char *data) {
   int len = strlen(data);
   char *dup = ALLOC_N(char, len+1);
   if ( dup == NULL ) {
      PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
      return NULL;
   }
   strcpy(dup, data);
   return dup;
} 
   
char *estrndup(char *data, int max) {
   int len = strlen(data);
   char *dup;
   if (len > max) len = max;
      dup = ALLOC_N(char, len+1);
   if ( dup == NULL ) {
      PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
      return NULL;
   }
   strcpy(dup, data);
   return dup;
} 
   
char *strtolower(char *data, int max) {
   while (max--) data[max] = tolower(data[max]);
   return data;
} 
   
char *strtoupper(char *data, int max) {
   while (max--) data[max] = toupper(data[max]);
   return data;
} 

/*   static void _python_ibm_db_free_conn_struct */
static void _python_ibm_db_free_conn_struct(conn_handle *handle) {
   int rc;

   /* Disconnect from DB. If stmt is allocated, it is freed automatically */
   if ( handle->handle_active && !handle->flag_pconnect) {
      if(handle->auto_commit == 0){
        rc = SQLEndTran(SQL_HANDLE_DBC, (SQLHDBC)handle->hdbc, SQL_ROLLBACK);
      }
      rc = SQLDisconnect((SQLHDBC)handle->hdbc);
      rc = SQLFreeHandle(SQL_HANDLE_DBC, handle->hdbc);
      rc = SQLFreeHandle(SQL_HANDLE_ENV, handle->henv);
   }
	handle->ob_type->tp_free((PyObject*)handle);
}

/*   static void _python_ibm_db_free_row_struct */
/*
 * static void _python_ibm_db_free_row_struct(row_hash_struct *handle) {
 *  free(handle);
 * }
 */

/*   static void _python_ibm_db_free_result_struct(stmt_handle* handle) */
static void _python_ibm_db_free_result_struct(stmt_handle* handle) {
   int i;
   param_node *curr_ptr = NULL, *prev_ptr = NULL;

   if ( handle != NULL ) {
      /* Free param cache list */
      curr_ptr = handle->head_cache_list;
      prev_ptr = handle->head_cache_list;

      while (curr_ptr != NULL) {
         curr_ptr = curr_ptr->next;
    
         if (prev_ptr->svalue) PyMem_Del(prev_ptr->svalue);
         PyMem_Del(prev_ptr);

         prev_ptr = curr_ptr;
      }
      /* free row data cache */
      if (handle->row_data) {
         for (i=0; i<handle->num_columns;i++) {
            switch (handle->column_info[i].type) {
            case SQL_CHAR:
            case SQL_VARCHAR:
            case SQL_LONGVARCHAR:
            case SQL_TYPE_DATE:
            case SQL_TYPE_TIME:
            case SQL_TYPE_TIMESTAMP:
            case SQL_BIGINT:
            case SQL_DECIMAL:
            case SQL_NUMERIC:
            case SQL_XML:
               if ( handle->row_data[i].data.str_val != NULL ) {
                  PyMem_Del(handle->row_data[i].data.str_val);
                  handle->row_data[i].data.str_val = NULL;
               }
            }
         }
         PyMem_Del(handle->row_data);
         handle->row_data = NULL;
      }

      /* free column info cache */
      if ( handle->column_info ) {
         for (i=0; i<handle->num_columns; i++) {
            PyMem_Del(handle->column_info[i].name);
         }
         PyMem_Del(handle->column_info);
         handle->column_info = NULL;
         handle->num_columns = 0;
      }
   }
}

/* static stmt_handle *_ibm_db_new_stmt_struct(conn_handle* conn_res) */   
static stmt_handle *_ibm_db_new_stmt_struct(conn_handle* conn_res) {
   stmt_handle *stmt_res;

   stmt_res = PyObject_NEW(stmt_handle, &stmt_handleType);
   /* memset(stmt_res, 0, sizeof(stmt_handle)); */

   /* Initialize stmt resource so parsing assigns updated options if needed */
   stmt_res->hdbc = conn_res->hdbc;
   stmt_res->s_bin_mode = conn_res->c_bin_mode;
   stmt_res->cursor_type = conn_res->c_cursor_type;
   stmt_res->s_case_mode = conn_res->c_case_mode;

   stmt_res->head_cache_list = NULL;
   stmt_res->current_node = NULL;

   stmt_res->num_params = 0;
   stmt_res->file_param = 0;

   stmt_res->column_info = NULL;
   stmt_res->num_columns = 0;

   stmt_res->error_recno_tracker = 1;
   stmt_res->errormsg_recno_tracker = 1;

   stmt_res->row_data = NULL;

   return stmt_res;
}

/*   static _python_ibm_db_free_stmt_struct */
static void _python_ibm_db_free_stmt_struct(stmt_handle *handle) {
   int rc;

   rc = SQLFreeHandle( SQL_HANDLE_STMT, handle->hstmt);

   if ( handle ) {
      _python_ibm_db_free_result_struct(handle);
   }
	handle->ob_type->tp_free((PyObject*)handle);
}

/*   static void _python_ibm_db_init_error_info(stmt_handle *stmt_res) */
static void _python_ibm_db_init_error_info(stmt_handle *stmt_res) {
   stmt_res->error_recno_tracker = 1;
   stmt_res->errormsg_recno_tracker = 1;
}

/*   static void _python_ibm_db_check_sql_errors( SQLHANDLE handle, SQLSMALLINT hType, int rc, int cpy_to_global, char* ret_str, int API SQLSMALLINT recno)
*/
static void _python_ibm_db_check_sql_errors( SQLHANDLE handle, SQLSMALLINT hType, int rc, int cpy_to_global, char* ret_str, int API, SQLSMALLINT recno )
{
   SQLCHAR msg[SQL_MAX_MESSAGE_LENGTH + 1];
   SQLCHAR sqlstate[SQL_SQLSTATE_SIZE + 1];
   SQLCHAR errMsg[DB2_MAX_ERR_MSG_LEN];
   SQLINTEGER sqlcode;
   SQLSMALLINT length;
   char *p;

   memset(errMsg, '\0', DB2_MAX_ERR_MSG_LEN);
   memset(msg, '\0', SQL_MAX_MESSAGE_LENGTH + 1);
   if ( SQLGetDiagRec(hType, handle, recno, sqlstate, &sqlcode, msg,
         SQL_MAX_MESSAGE_LENGTH + 1, &length ) == SQL_SUCCESS) {

      while ((p = strchr( (char *)msg, '\n' ))) {
            *p = '\0';
      }
      sprintf((char*)errMsg, "%s SQLCODE=%d", (char*)msg, (int)sqlcode);
      if (cpy_to_global != 0) {
         PyErr_SetString(PyExc_Exception, errMsg);
      }

      switch (rc) {
         case SQL_ERROR:
         /* Need to copy the error msg and sqlstate into the symbol Table 
          * to cache these results */
            if ( cpy_to_global ) {
               switch (hType) {
                  case SQL_HANDLE_DBC:
                     strncpy(IBM_DB_G(__python_conn_err_state), (char*)sqlstate,                             SQL_SQLSTATE_SIZE+1);
                     strncpy(IBM_DB_G(__python_conn_err_msg), (char*)errMsg, 
                             DB2_MAX_ERR_MSG_LEN);
                     break;

                  case SQL_HANDLE_STMT:
                     strncpy(IBM_DB_G(__python_stmt_err_state), (char*)sqlstate,                             SQL_SQLSTATE_SIZE+1);
                     strncpy(IBM_DB_G(__python_stmt_err_msg), (char*)errMsg, 
                             DB2_MAX_ERR_MSG_LEN);
                     break;
               }
            }
            /* This call was made from ibm_db_errmsg or ibm_db_error */
            /* Check for error and return */
            switch (API) {
               case DB2_ERR:
                  if ( ret_str != NULL ) {
                     strncpy(ret_str, (char*)sqlstate, SQL_SQLSTATE_SIZE+1);
                  }
                  return;
               case DB2_ERRMSG:
                  if ( ret_str != NULL ) {
                     strncpy(ret_str, (char*)errMsg, DB2_MAX_ERR_MSG_LEN);
                  }
                  return;
               default:
                  break;
            }
            break;
         default:
            break;
      }
   }
}

/*   static int _python_ibm_db_assign_options( void *handle, int type, long opt_key, PyObject *data ) */
static int _python_ibm_db_assign_options( void *handle, int type, long opt_key, PyObject *data )
{
   int rc = 0;
   long option_num = 0;
   char *option_str = NULL;

   /* First check to see if it is a non-cli attribut */
   if (opt_key == ATTR_CASE) {
      option_num = NUM2LONG(data);
      if (type == SQL_HANDLE_STMT) {
         switch (option_num) {
            case CASE_LOWER:
               ((stmt_handle*)handle)->s_case_mode = CASE_LOWER;
               break;
            case CASE_UPPER:
               ((stmt_handle*)handle)->s_case_mode = CASE_UPPER;
               break;
            case CASE_NATURAL:
               ((stmt_handle*)handle)->s_case_mode = CASE_NATURAL;
               break;
            default:
               PyErr_SetString(PyExc_Exception, "ATTR_CASE attribute must be one of CASE_LOWER, CASE_UPPER, or CASE_NATURAL");
					return -1;
         }
      } else if (type == SQL_HANDLE_DBC) {
         switch (option_num) {
            case CASE_LOWER:
               ((conn_handle*)handle)->c_case_mode = CASE_LOWER;
               break;
            case CASE_UPPER:
               ((conn_handle*)handle)->c_case_mode = CASE_UPPER;
               break;
            case CASE_NATURAL:
               ((conn_handle*)handle)->c_case_mode = CASE_NATURAL;
               break;
            default:
               PyErr_SetString(PyExc_Exception, "ATTR_CASE attribute must be one of CASE_LOWER, CASE_UPPER, or CASE_NATURAL");
					return -1;
         }
      } else {
         PyErr_SetString(PyExc_Exception, "Connection or statement handle must be passed in.");
			return -1;
      }
   } else if (type == SQL_HANDLE_STMT) {
      if (PyString_Check(data)) {
         option_str = STR2CSTR(data);
         rc = SQLSetStmtAttr((SQLHSTMT)((stmt_handle *)handle)->hstmt, opt_key, (SQLPOINTER)option_str, SQL_IS_INTEGER );
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors((SQLHSTMT)((stmt_handle *)handle)->hstmt, SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
         }
      } else {
         option_num = NUM2LONG(data);
         if (opt_key == SQL_ATTR_AUTOCOMMIT && option_num == SQL_AUTOCOMMIT_OFF) ((conn_handle*)handle)->auto_commit = 0;
         else if (opt_key == SQL_ATTR_AUTOCOMMIT && option_num == SQL_AUTOCOMMIT_ON) ((conn_handle*)handle)->auto_commit = 1;
         rc = SQLSetStmtAttr((SQLHSTMT)((stmt_handle *)handle)->hstmt, opt_key, (SQLPOINTER)option_num, SQL_IS_INTEGER );
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors((SQLHSTMT)((stmt_handle *)handle)->hstmt, SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
         }
      }
   } else if (type == SQL_HANDLE_DBC) {
      if (PyString_Check(data)) {
         option_str = STR2CSTR(data);
         rc = SQLSetConnectAttr((SQLHSTMT)((conn_handle*)handle)->hdbc, opt_key, (SQLPOINTER)option_str, SQL_NTS);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors((SQLHSTMT)((stmt_handle *)handle)->hstmt, SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
         }
      } else {
         option_num = NUM2LONG(data);
         if (opt_key == SQL_ATTR_AUTOCOMMIT && option_num == SQL_AUTOCOMMIT_OFF) ((conn_handle*)handle)->auto_commit = 0;
         else if (opt_key == SQL_ATTR_AUTOCOMMIT && option_num == SQL_AUTOCOMMIT_ON) ((conn_handle*)handle)->auto_commit = 1;
         rc = SQLSetConnectAttr((SQLHSTMT)((conn_handle*)handle)->hdbc, opt_key, (SQLPOINTER)option_num, SQL_IS_INTEGER);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors((SQLHSTMT)((stmt_handle *)handle)->hstmt, SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
         }
      }
   } else {
      PyErr_SetString(PyExc_Exception, "Connection or statement handle must be passed in.");
		return -1;
   }
	return 0;
}

/*   static int _python_ibm_db_parse_options( PyObject *options, int type, void *handle)
*/
static int _python_ibm_db_parse_options ( PyObject *options, int type, void *handle )
{
   int numOpts = 0, i = 0;
   PyObject *keys = NULL;
   PyObject *key = NULL; /* Holds the Option Index Key */
   PyObject *data = NULL;
	int rc = 0;

   if ( !NIL_P(options) ) {
      keys = PyDict_Keys(options);
      numOpts = PyList_Size(keys);

      for ( i = 0; i < numOpts; i++) {
         key = PyList_GetItem(keys,i);
         data = PyDict_GetItem(options,key);

         /* Assign options to handle. */
         /* Sets the options in the handle with CLI/ODBC calls */
         rc = _python_ibm_db_assign_options(handle, type, NUM2LONG(key), data);
			if (rc)
			   return SQL_ERROR;
      }
   }
   return SQL_SUCCESS;
}

/*   static int _python_ibm_db_get_result_set_info(stmt_handle *stmt_res)
initialize the result set information of each column. This must be done once
*/
static int _python_ibm_db_get_result_set_info(stmt_handle *stmt_res)
{
   int rc = -1, i;
   SQLSMALLINT nResultCols = 0, name_length;
   SQLCHAR tmp_name[BUFSIZ];
   rc = SQLNumResultCols((SQLHSTMT)stmt_res->hstmt, &nResultCols);
   if ( rc == SQL_ERROR || nResultCols == 0) {
      _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                      SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
      return -1;
   }
   stmt_res->num_columns = nResultCols;
   stmt_res->column_info = ALLOC_N(ibm_db_result_set_info, nResultCols);
   if ( stmt_res->column_info == NULL ) {
      PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
      return -1;
   }
   memset(stmt_res->column_info, 0, sizeof(ibm_db_result_set_info)*nResultCols);
   /* return a set of attributes for a column */
   for (i = 0 ; i < nResultCols; i++) {
      stmt_res->column_info[i].lob_loc = 0;
      stmt_res->column_info[i].loc_ind = 0;
      stmt_res->column_info[i].loc_type = 0;
      rc = SQLDescribeCol((SQLHSTMT)stmt_res->hstmt, (SQLSMALLINT)(i + 1 ),
                          (SQLCHAR *)&tmp_name, BUFSIZ, &name_length, 
                          &stmt_res->column_info[i].type,
                          &stmt_res->column_info[i].size, 
                          &stmt_res->column_info[i].scale,
                          &stmt_res->column_info[i].nullable);
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                         SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
         return -1;
      }
      if ( name_length <= 0 ) {
         stmt_res->column_info[i].name = (SQLCHAR *)estrdup("");
         if ( stmt_res->column_info[i].name == NULL ) {
            PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
             return -1;
         }

      } else if (name_length >= BUFSIZ ) {
         /* column name is longer than BUFSIZ */
         stmt_res->column_info[i].name = (SQLCHAR*)ALLOC_N(char, name_length+1);
         if ( stmt_res->column_info[i].name == NULL ) {
            PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
             return -1;
         }
         rc = SQLDescribeCol((SQLHSTMT)stmt_res->hstmt, (SQLSMALLINT)(i + 1 ),
                             stmt_res->column_info[i].name, name_length, 
                             &name_length, &stmt_res->column_info[i].type, 
                             &stmt_res->column_info[i].size, 
                             &stmt_res->column_info[i].scale, 
                             &stmt_res->column_info[i].nullable);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                           SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
            return -1;
         }
      } else {
         stmt_res->column_info[i].name = (SQLCHAR*)estrdup((char*)tmp_name);
         if ( stmt_res->column_info[i].name == NULL ) {
            PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
             return -1;
         }

      }
   }
   return 0;
}

/*   static int _python_ibn_bind_column_helper(stmt_handle *stmt_res)
   bind columns to data, this must be done once
*/
static int _python_ibm_db_bind_column_helper(stmt_handle *stmt_res)
{
   SQLINTEGER in_length = 0;
   SQLSMALLINT column_type;
   ibm_db_row_data_type *row_data;
   int i, rc = SQL_SUCCESS;

   stmt_res->row_data = ALLOC_N(ibm_db_row_type, stmt_res->num_columns);
   if ( stmt_res->row_data == NULL ) {
      PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
      return -1;
   }
   memset(stmt_res->row_data,0,sizeof(ibm_db_row_type)*stmt_res->num_columns);

   for (i=0; i<stmt_res->num_columns; i++) {
      column_type = stmt_res->column_info[i].type;
      row_data = &stmt_res->row_data[i].data;
      switch(column_type) {
         case SQL_CHAR:
         case SQL_VARCHAR:
         case SQL_LONGVARCHAR:
            in_length = stmt_res->column_info[i].size+1;
            row_data->str_val = (SQLCHAR *) ALLOC_N(char, in_length);
            if ( row_data->str_val == NULL ) {
               PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
               return -1;
            }
            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            SQL_C_DEFAULT, row_data->str_val, in_length,
                            (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, 
                                               -1, 1);
            }
            break;

         case SQL_BINARY:
         case SQL_LONGVARBINARY:
         case SQL_VARBINARY:
            if ( stmt_res->s_bin_mode == CONVERT ) {
               in_length = 2*(stmt_res->column_info[i].size)+1;
               row_data->str_val = (SQLCHAR *)ALLOC_N(char, in_length);
               if ( row_data->str_val == NULL ) {
                  PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
                  return -1;
               }        
               rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                             SQL_C_CHAR, row_data->str_val, in_length,
                             (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
               if ( rc == SQL_ERROR ) {
                  _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                                  SQL_HANDLE_STMT, rc, 1, NULL,
                                                  -1, 1);
               }
            } else {
               in_length = stmt_res->column_info[i].size+1;
               row_data->str_val = (SQLCHAR *)ALLOC_N(char, in_length);
               if ( row_data->str_val == NULL ) {
                  PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
                  return -1;
               }

               rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                             SQL_C_DEFAULT, row_data->str_val, in_length,
                             (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
               if ( rc == SQL_ERROR ) {
                  _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                                  SQL_HANDLE_STMT, rc, 1, NULL,
                                                  -1, 1);
               }
            }
            break;

         case SQL_TYPE_DATE:
         case SQL_TYPE_TIME:
         case SQL_TYPE_TIMESTAMP:
         case SQL_BIGINT:
            in_length = stmt_res->column_info[i].size+1;
            row_data->str_val = (SQLCHAR *)ALLOC_N(char, in_length);
            if ( row_data->str_val == NULL ) {
               PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
               return -1;
            }

            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            SQL_C_CHAR, row_data->str_val, in_length,
                            (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,                                               1);
            }
            break;

         case SQL_SMALLINT:
            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            SQL_C_DEFAULT, &row_data->s_val, 
                            sizeof(row_data->s_val),
                            (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,
                                               1);
            }
            break;

         case SQL_INTEGER:
            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            SQL_C_DEFAULT, &row_data->i_val, 
                            sizeof(row_data->i_val),
                            (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,
                                               1);
            }
            break;

         case SQL_REAL:
         case SQL_FLOAT:
            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            SQL_C_DEFAULT, &row_data->f_val, 
                            sizeof(row_data->f_val),
                            (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,
                                               1);
            }
            break;

         case SQL_DOUBLE:
            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            SQL_C_DEFAULT, &row_data->d_val, 
                            sizeof(row_data->d_val),
                            (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,
                                               1);
            }
            break;

         case SQL_DECIMAL:
         case SQL_NUMERIC:
            in_length = stmt_res->column_info[i].size +
                        stmt_res->column_info[i].scale + 2 + 1;
            row_data->str_val = (SQLCHAR *)ALLOC_N(char, in_length);
            if ( row_data->str_val == NULL ) {
               PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
               return -1;
            }

            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            SQL_C_CHAR, row_data->str_val, in_length,
                            (SQLINTEGER *)(&stmt_res->row_data[i].out_length));
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,
														     1);
            }
            break;

         case SQL_CLOB:
            stmt_res->row_data[i].out_length = 0;
            stmt_res->column_info[i].loc_type = SQL_CLOB_LOCATOR;
            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            stmt_res->column_info[i].loc_type, 
                            &stmt_res->column_info[i].lob_loc, 4,
                            &stmt_res->column_info[i].loc_ind);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,
															  1);
            }
            break;
         case SQL_BLOB:
            stmt_res->row_data[i].out_length = 0;
            stmt_res->column_info[i].loc_type = SQL_BLOB_LOCATOR;
            rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(i+1),
                            stmt_res->column_info[i].loc_type, 
                            &stmt_res->column_info[i].lob_loc, 4,
                            &stmt_res->column_info[i].loc_ind);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                               SQL_HANDLE_STMT, rc, 1, NULL, -1,
                                               1);
            }
            break;
         case SQL_XML:
            stmt_res->row_data[i].out_length = 0;
            break;

         default:
            break;
         }
      }
   return rc;
}

/*   static void _python_ibm_db_clear_stmt_err_cache () */
static void _python_ibm_db_clear_stmt_err_cache(void)
{
   memset(IBM_DB_G(__python_stmt_err_msg), 0, DB2_MAX_ERR_MSG_LEN);
   memset(IBM_DB_G(__python_stmt_err_state), 0, SQL_SQLSTATE_SIZE + 1);
}

/*   static int _python_ibm_db_connect_helper( argc, argv, isPersistent ) */
static PyObject *_python_ibm_db_connect_helper( PyObject *self, PyObject *args, int isPersistent )
{
   char *database = NULL;
   char *uid = NULL;
   char *password = NULL;
   PyObject *options = NULL;
   int rc = 0;
   SQLINTEGER conn_alive;
   SQLINTEGER enable_numeric_literals = 1; /* Enable CLI numeric literals */
   conn_handle *conn_res = NULL;
   int reused = 0;
   int hKeyLen = 0;
   char *hKey = NULL;
   PyObject *entry = NULL;
   char server[2048];
   conn_alive = 1;

	if (!PyArg_ParseTuple(args, "sss|O", &database, &uid, &password, &options))
      return NULL;
   do {
      /* Check if we already have a connection for this userID & database 
       * combination
       */ 
      if (isPersistent) {
         hKeyLen = strlen(database) + strlen(uid) + strlen(password) + 12;
         hKey = ALLOC_N(char, hKeyLen);
         sprintf(hKey, "__ibm_db_%s.%s.%s", uid, database, password);

         entry = PyDict_GetItemString(persistent_list, hKey);

         if (entry != NULL) {
            Py_INCREF(entry);
            conn_res = (conn_handle *)entry;
#ifndef PASE /* i5/OS server mode is persistant */
            /* Need to reinitialize connection? */
            rc = SQLGetConnectAttr(conn_res->hdbc, SQL_ATTR_PING_DB, 
                                   (SQLPOINTER)&conn_alive, 0, NULL);
            if ( (rc == SQL_SUCCESS) && conn_alive ) {
               _python_ibm_db_check_sql_errors( conn_res->hdbc, SQL_HANDLE_DBC, 
                                              rc, 1, NULL, -1, 1);
               reused = 1;
            } /* else will re-connect since connection is dead */
#endif /* PASE */
            reused = 1;
         }
      } else {
      /* Need to check for max pconnections? */
    }

      if (conn_res == NULL) {
		   conn_res = PyObject_NEW(conn_handle, &conn_handleType);
         conn_res->henv = 0;
         conn_res->hdbc = 0;
      }

      /* We need to set this early, in case we get an error below,
         so we know how to free the connection */
      conn_res->flag_pconnect = isPersistent;
      /* Allocate ENV handles if not present */
      if ( !conn_res->henv ) {
         rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &(conn_res->henv));
         if (rc != SQL_SUCCESS) {
            _python_ibm_db_check_sql_errors( conn_res->henv, SQL_HANDLE_ENV, rc,
                                             1, NULL, -1, 1);
            break;
         }
         rc = SQLSetEnvAttr((SQLHENV)conn_res->henv, SQL_ATTR_ODBC_VERSION, 
                            (void *)SQL_OV_ODBC3, 0);
      }

      if (!reused) {
         /* Alloc CONNECT Handle */
         rc = SQLAllocHandle(SQL_HANDLE_DBC, conn_res->henv, &(conn_res->hdbc));
         if (rc != SQL_SUCCESS) {
            _python_ibm_db_check_sql_errors(conn_res->henv, SQL_HANDLE_ENV, rc, 
                                            1, NULL, -1, 1);
            break;
         }
      }

      /* Set this after the connection handle has been allocated to avoid
      unnecessary network flows. Initialize the structure to default values */
      conn_res->auto_commit = SQL_AUTOCOMMIT_ON;
      rc = SQLSetConnectAttr((SQLHDBC)conn_res->hdbc, SQL_ATTR_AUTOCOMMIT, 
                             (SQLPOINTER)(conn_res->auto_commit), SQL_NTS);

      conn_res->c_bin_mode = IBM_DB_G(bin_mode);
      conn_res->c_case_mode = CASE_NATURAL;
      conn_res->c_cursor_type = SQL_SCROLL_FORWARD_ONLY;

      conn_res->error_recno_tracker = 1;
      conn_res->errormsg_recno_tracker = 1;

      /* handle not active as of yet */
      conn_res->handle_active = 0;

      /* Set Options */
      if ( !NIL_P(options) ) {
         rc = _python_ibm_db_parse_options( options, SQL_HANDLE_DBC, conn_res );
         if (rc != SQL_SUCCESS) {
			   return NULL;
         }
      }

      if (! reused) {
         /* Connect */
         /* If the string contains a =, use SQLDriverConnect */
         if ( strstr(database, "=") != NULL ) {
            rc = SQLDriverConnect((SQLHDBC)conn_res->hdbc, (SQLHWND)NULL,
                                  (SQLCHAR*)database, SQL_NTS, NULL, 0, NULL, 
                                  SQL_DRIVER_NOPROMPT );
         } else {
            rc = SQLConnect((SQLHDBC)conn_res->hdbc,
                            (SQLCHAR *)database,
                            (SQLSMALLINT)(strlen(database)),
                            (SQLCHAR *)uid, (SQLSMALLINT)(strlen(uid)),
                            (SQLCHAR *)password,
                            (SQLSMALLINT)(strlen(password)));
         }

         if ( rc != SQL_SUCCESS ) {
            _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 
                                            1, NULL, -1, 1);
            SQLFreeHandle( SQL_HANDLE_DBC, conn_res->hdbc );
            SQLFreeHandle(SQL_HANDLE_ENV, conn_res->henv);
            break;
         }

         /* Get the server name */
         memset(server, 0, sizeof(server));
         rc = SQLGetInfo(conn_res->hdbc, SQL_DBMS_NAME, (SQLPOINTER)server, 
                         2048, NULL);
         if (!strcmp(server, "AS")) is_systemi = 1;
         if (!strncmp(server, "IDS", 3)) is_informix = 1;

      /* Set SQL_ATTR_REPLACE_QUOTED_LITERALS connection attribute to
       * enable CLI numeric literal feature. This is equivalent to
       * PATCH2=71 in the db2cli.ini file
       * Note, for backward compatibility with older CLI drivers having a
       * different value for SQL_ATTR_REPLACE_QUOTED_LITERALS, we call
       * SQLSetConnectAttr() with both the old and new value
       */
      /* Only enable this feature if we are not connected to an Informix data 
       * server 
       */
         if (!is_informix) {
            rc = SQLSetConnectAttr((SQLHDBC)conn_res->hdbc, 
                                   SQL_ATTR_REPLACE_QUOTED_LITERALS, 
                                   (SQLPOINTER)(enable_numeric_literals), 
                                   SQL_IS_INTEGER);
				if (rc != SQL_SUCCESS)
               rc = SQLSetConnectAttr((SQLHDBC)conn_res->hdbc,
                                      SQL_ATTR_REPLACE_QUOTED_LITERALS_OLDVALUE,
                                      (SQLPOINTER)(enable_numeric_literals), 
                                      SQL_IS_INTEGER);
         }
         if (rc != SQL_SUCCESS) {
            _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 
                                            1, NULL, -1, 1);
         }
      }
      conn_res->handle_active = 1;
   } while (0);

   if (hKey != NULL) {
      if (! reused && rc == SQL_SUCCESS) {
         /* If we created a new persistent connection, add it to the 
          * persistent_list
          */
         PyDict_SetItemString(persistent_list, hKey, conn_res);
		}
	   PyMem_Del(hKey);
   }

   if ( rc != SQL_SUCCESS ) {
      if (conn_res != NULL && conn_res->handle_active) {
         rc = SQLFreeHandle( SQL_HANDLE_DBC, conn_res->hdbc);
      }

	 /* TODO: We need to free memory here */
    /* free memory 
     * if (conn_res != NULL) {
     *   free(conn_res);
     * }
	  */
   return NULL;                          
   } 
   return (PyObject *)conn_res;
}

/* static void _python_ibm_db_clear_conn_err_cache () */
static void _python_ibm_db_clear_conn_err_cache(void)
{
   /* Clear out the cached conn messages */
   memset(IBM_DB_G(__python_conn_err_msg), 0, DB2_MAX_ERR_MSG_LEN);
   memset(IBM_DB_G(__python_conn_err_state), 0, SQL_SQLSTATE_SIZE + 1);
}


/*!#
 * ibm_db.connect
 * ibm_db.pconnect
 * ibm_db.autocommit
 * ibm_db.bind_param
 * ibm_db.close
 * ibm_db.column_privileges
 * ibm_db.columns
 * ibm_db.foreign_keys
 * ibm_db.primary_keys
 * ibm_db.procedure_columns
 * ibm_db.procedures
 * ibm_db.special_columns
 * ibm_db.statistics
 * ibm_db.table_privileges
 * ibm_db.tables
 * ibm_db.commit
 * ibm_db.exec
 * ibm_db.free_result
 * ibm_db.prepare
 * ibm_db.execute
 * ibm_db.conn_errormsg
 * ibm_db.stmt_errormsg
 * ibm_db.conn_error
 * ibm_db.stmt_error
 * ibm_db.next_result
 * ibm_db.num_fields
 * ibm_db.num_rows
 * ibm_db.field_name
 * ibm_db.field_display_size
 * ibm_db.field_num
 * ibm_db.field_precision
 * ibm_db.field_scale
 * ibm_db.field_type
 * ibm_db.field_width
 * ibm_db.cursor_type
 * ibm_db.rollback
 * ibm_db.free_stmt
 * ibm_db.result
 * ibm_db.fetch_row
 * ibm_db.fetch_assoc
 * ibm_db.fetch_array
 * ibm_db.fetch_both
 * ibm_db.set_option
 * ibm_db.server_info
 * ibm_db.client_info
 * ibm_db.active
 * ibm_db.get_option
 */



/*!# ibm_db.connect
 *
 * ===Description
 *
 *  --   Returns a connection to a database
 * IBM_DBConnection ibm_db.connect (dsn=<..>, user=<..>, password=<..>,
 *                                  host=<..>, database=<..>, options=<..>)
 *
 * Creates a new connection to an IBM DB2 Universal Database, IBM Cloudscape,
 * or Apache Derby database.
 *
 * ===Parameters
 *
 * ====dsn
 *
 * For an uncataloged connection to a database, database represents a complete
 * connection string in the following format:
 * DRIVER={IBM DB2 ODBC DRIVER};DATABASE=database;HOSTNAME=hostname;PORT=port;
 * PROTOCOL=TCPIP;UID=username;PWD=password;
 *      where the parameters represent the following values:
 *        hostname
 *           The hostname or IP address of the database server.
 *        port
 *           The TCP/IP port on which the database is listening for requests.
 *        username
 *           The username with which you are connecting to the database.
 *        password
 *           The password with which you are connecting to the database.
 *
 * ====user
 *
 * The username with which you are connecting to the database.
 * This is optional if username is specified in the "dsn" string
 *
 * ====password
 *
 * The password with which you are connecting to the database.
 * This is optional if password is specified in the "dsn" string
 *
 * ====host
 *
 * The hostname or IP address of the database server.
 * This is optional if hostname is specified in the "dsn" string
 *
 * ====database
 *
 * For a cataloged connection to a database, database represents the database
 * alias in the DB2 client catalog.
 * This is optional if database is specified in the "dsn" string
 *
 * ====options
 *
 *      An dictionary of connection options that affect the behavior of the
 *      connection,
 *      where valid array keys include:
 *        SQL_ATTR_AUTOCOMMIT
 *           Passing the SQL_AUTOCOMMIT_ON value turns autocommit on for this
 *           connection handle.
 *           Passing the SQL_AUTOCOMMIT_OFF value turns autocommit off for this
 *           connection handle.
 *        ATTR_CASE
 *           Passing the CASE_NATURAL value specifies that column names are
 *           returned in natural case.
 *           Passing the CASE_LOWER value specifies that column names are
 *           returned in lower case.
 *           Passing the CASE_UPPER value specifies that column names are
 *           returned in upper case.
 *        SQL_ATTR_CURSOR_TYPE
 *           Passing the SQL_SCROLL_FORWARD_ONLY value specifies a forward-only
 *           cursor for a statement resource.
 *           This is the default cursor type and is supported on all database
 *           servers.
 *           Passing the SQL_CURSOR_KEYSET_DRIVEN value specifies a scrollable
 *           cursor for a statement resource.
 *           This mode enables random access to rows in a result set, but
 *           currently is supported only by IBM DB2 Universal Database.
 *
 * ===Return Values
 *
 *
 * Returns a IBM_DBConnection connection object if the connection attempt is
 * successful.
 * If the connection attempt fails, ibm_db.connect() returns None.
 *
 */ 
static PyObject *ibm_db_connect(PyObject *self, PyObject *args)
{
   _python_ibm_db_clear_conn_err_cache();
   return _python_ibm_db_connect_helper( self, args, 0 );
}

/*!# ibm_db.pconnect
 *
 * ===Description
 *  --   Returns a persistent connection to a database
 * resource ibm_db.pconnect ( string database, string username, string password
 * [, array options] )
 *
 * Returns a persistent connection to an IBM DB2 Universal Database,
 * IBM Cloudscape, Apache Derby or Informix Dynamic Server database.
 *
 * Calling ibm_db.close() on a persistent connection always returns TRUE, but
 * the underlying DB2 client connection remains open and waiting to serve the
 * next matching ibm_db.pconnect() request.
 *
 * ===Parameters
 *
 * ====database
 *       The database alias in the DB2 client catalog.
 *
 * ====username
 *       The username with which you are connecting to the database.
 *
 * ====password
 *       The password with which you are connecting to the database.
 *
 * ====options
 *       An associative array of connection options that affect the behavior of
 * the connection,
 *       where valid array keys include:
 *
 *       autocommit
 *             Passing the DB2_AUTOCOMMIT_ON value turns autocommit on for this
 * connection handle.
 *             Passing the DB2_AUTOCOMMIT_OFF value turns autocommit off for
 * this connection handle.
 *
 *       DB2_ATTR_CASE
 *             Passing the DB2_CASE_NATURAL value specifies that column names
 * are returned in natural case.
 *             Passing the DB2_CASE_LOWER value specifies that column names are
 * returned in lower case.
 *             Passing the DB2_CASE_UPPER value specifies that column names are
 * returned in upper case.
 *
 *       CURSOR
 *             Passing the SQL_SCROLL_FORWARD_ONLY value specifies a
 * forward-only cursor for a statement resource.  This is the default cursor
 * type and is supported on all database servers.
 *             Passing the SQL_CURSOR_KEYSET_DRIVEN value specifies a scrollable
 * cursor for a statement resource. This mode enables random access to rows in a
 * result set, but currently is supported only by IBM DB2 Universal Database.
 *
 * ===Return Values
 *
 * Returns a connection handle resource if the connection attempt is successful.
 * ibm_db.pconnect() tries to reuse an existing connection resource that exactly
 * matches the database, username, and password parameters. If the connection
 * attempt fails, ibm_db.pconnect() returns FALSE.
 */
static PyObject *ibm_db_pconnect(PyObject *self, PyObject *args)
{
   _python_ibm_db_clear_conn_err_cache();
   return _python_ibm_db_connect_helper( self, args, 1);
}

/*!# ibm_db.autocommit
 *
 * ===Description
 *
 * mixed ibm_db.autocommit ( resource connection [, bool value] )
 *
 * Returns or sets the AUTOCOMMIT behavior of the specified connection resource.
 *
 * ===Parameters
 *
 * ====connection
 *    A valid database connection resource variable as returned from connect()
 * or pconnect().
 *
 * ====value
 *    One of the following constants:
 *    SQL_AUTOCOMMIT_OFF
 *          Turns AUTOCOMMIT off.
 *    SQL_AUTOCOMMIT_ON
 *          Turns AUTOCOMMIT on.
 *
 * ===Return Values
 *
 * When ibm_db.autocommit() receives only the connection parameter, it returns
 * the current state of AUTOCOMMIT for the requested connection as an integer
 * value. A value of 0 indicates that AUTOCOMMIT is off, while a value of 1
 * indicates that AUTOCOMMIT is on.
 *
 * When ibm_db.autocommit() receives both the connection parameter and
 * autocommit parameter, it attempts to set the AUTOCOMMIT state of the
 * requested connection to the corresponding state.
 *
 * Returns TRUE on success or FALSE on failure.
 */ 
static PyObject *ibm_db_autocommit(PyObject *self, PyObject *args)           
{
   conn_handle *conn_res = NULL;
   int rc;
   SQLINTEGER autocommit;

	if (!PyArg_ParseTuple(args, "O|i", &conn_res, &autocommit))
      return NULL;

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      /* If value in handle is different from value passed in */
      if (PyTuple_Size(args) == 2) {
         if(autocommit != (conn_res->auto_commit)) {
#ifndef PASE
            rc = SQLSetConnectAttr((SQLHDBC)conn_res->hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)autocommit, SQL_IS_INTEGER);
#else
            rc = SQLSetConnectAttr((SQLHDBC)conn_res->hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)&autocommit, SQL_IS_INTEGER);
#endif
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, 
                                               rc, 1, NULL, -1, 1);
            }
            conn_res->auto_commit = autocommit;
         }
			Py_INCREF(Py_True);
         return Py_True;
      } else {
         return PyInt_FromLong(conn_res->auto_commit);
      }
   }
   return NULL;
}

/*   static void _python_ibm_db_add_param_cache( stmt_handle *stmt_res, int param_no, PyObject *var_pyvalue, int param_type, int size, SQLSMALLINT data_type, SQLSMALLINT precision, SQLSMALLINT scale, SQLSMALLINT nullable )
*/
static void _python_ibm_db_add_param_cache( stmt_handle *stmt_res, int param_no, PyObject *var_pyvalue, int param_type, int size, SQLSMALLINT data_type, SQLUINTEGER precision, SQLSMALLINT scale, SQLSMALLINT nullable )
{
   param_node *tmp_curr = NULL, *prev = stmt_res->head_cache_list, *curr = stmt_res->head_cache_list;

   while ( (curr != NULL) && (curr->param_num < param_no) ) {
      prev = curr;
      curr = curr->next;
   }

   if ( curr == NULL || curr->param_num != param_no ) {
	   /* Allocate memory and make new node to be added */
      tmp_curr = ALLOC(param_node);
      memset(tmp_curr, 0, sizeof(param_node));

      /* assign values */
      tmp_curr->data_type = data_type;
      tmp_curr->param_size = precision;
      tmp_curr->nullable = nullable;
      tmp_curr->scale = scale;
      tmp_curr->param_num = param_no;
      tmp_curr->file_options = SQL_FILE_READ;
      tmp_curr->param_type = param_type;
      tmp_curr->size = size;

      /* Set this flag in stmt_res if a FILE INPUT is present */
      if ( param_type == PARAM_FILE) {
         stmt_res->file_param = 1;
      }

      if ( var_pyvalue != NULL) {
         tmp_curr->var_pyvalue = var_pyvalue;
      }

      /* link pointers for the list */
      if ( prev == NULL ) {
         stmt_res->head_cache_list = tmp_curr;
      } else {
         prev->next = tmp_curr;
      }
      tmp_curr->next = curr;

      /* Increment num params added */
      stmt_res->num_params++;
   } else {
      /* Both the nodes are for the same param no */
      /* Replace Information */
      curr->data_type = data_type;
      curr->param_size = precision;
      curr->nullable = nullable;
      curr->scale = scale;
      curr->param_num = param_no;
      curr->file_options = SQL_FILE_READ;
      curr->param_type = param_type;
      curr->size = size;

      /* Set this flag in stmt_res if a FILE INPUT is present */
      if ( param_type == PARAM_FILE) {
         stmt_res->file_param = 1;
      }

      if ( var_pyvalue != NULL) {
         curr->var_pyvalue = var_pyvalue;
      }

   }
}

/*!# ibm_db.bind_param
 *
 * ===Description
 * Py_True/Py_None ibm_db.bind_param (resource stmt, int parameter-number,
 *                                    string variable [, int parameter-type
 *                                    [, int data-type [, int precision
 *                                    [, int scale [, int size[]]]]]] )
 *
 * Binds a Python variable to an SQL statement parameter in a IBM_DBStatement
 * resource returned by ibm_db.prepare().
 * This function gives you more control over the parameter type, data type,
 * precision, and scale for the parameter than simply passing the variable as
 * part of the optional input array to ibm_db.execute().
 *
 * ===Parameters
 *
 * ====stmt
 *
 *    A prepared statement returned from ibm_db.prepare().
 *
 * ====parameter-number
 *
 *    Specifies the 1-indexed position of the parameter in the prepared
 * statement.
 *
 * ====variable
 *
 *    A Python variable to bind to the parameter specified by parameter-number.
 *
 * ====parameter-type
 *
 *    A constant specifying whether the Python variable should be bound to the
 * SQL parameter as an input parameter (SQL_PARAM_INPUT), an output parameter
 * (SQL_PARAM_OUTPUT), or as a parameter that accepts input and returns output
 * (SQL_PARAM_INPUT_OUTPUT). To avoid memory overhead, you can also specify
 * PARAM_FILE to bind the Python variable to the name of a file that contains
 * large object (BLOB, CLOB, or DBCLOB) data.
 *
 * ====data-type
 *
 *    A constant specifying the SQL data type that the Python variable should be
 * bound as: one of SQL_BINARY, DB2_CHAR, DB2_DOUBLE, or DB2_LONG .
 *
 * ====precision
 *
 *    Specifies the precision that the variable should be bound to the database. *
 * ====scale
 *
 *      Specifies the scale that the variable should be bound to the database.
 *
 * ====size
 *
 *      Specifies the size that should be retreived from an INOUT/OUT parameter.
 *
 * ===Return Values
 *
 *    Returns Py_True on success or NULL on failure.
 */
static PyObject *ibm_db_bind_param(PyObject *self, PyObject *args)           
{
   PyObject *var_pyvalue = NULL;
	PyObject *py_param_type = NULL;
	PyObject *py_data_type = NULL;
	PyObject *py_precision = NULL;
	PyObject *py_scale = NULL;
	PyObject *py_size = NULL;
   char error[DB2_MAX_ERR_MSG_LEN];
   long param_type = SQL_PARAM_INPUT;
   /* LONG types used for data being passed in */
   SQLUSMALLINT param_no = 0;
   long data_type = 0;
   long precision = 0;
   long scale = 0;
   long size = 0;
   SQLSMALLINT sql_data_type = 0;
   SQLUINTEGER sql_precision = 0;
   SQLSMALLINT sql_scale = 0;
   SQLSMALLINT sql_nullable = SQL_NO_NULLS;

   stmt_handle *stmt_res;
   int rc = 0;

   if (!PyArg_ParseTuple(args, "OiO|OOOOO", &stmt_res, &param_no, 
                                             &var_pyvalue, &py_param_type, 
                                             &py_data_type, &py_precision, 
                                             &py_scale, &py_size))
       return NULL;

	if (py_param_type != NULL && py_param_type != Py_None && 
       TYPE(py_param_type) == PYTHON_FIXNUM)
		param_type = PyInt_AS_LONG(py_param_type);

	if (py_data_type != NULL && py_data_type != Py_None && 
       TYPE(py_data_type) == PYTHON_FIXNUM)
		data_type = PyInt_AS_LONG(py_data_type);

	if (py_precision != NULL && py_precision != Py_None && 
       TYPE(py_precision) == PYTHON_FIXNUM)
		precision = PyInt_AS_LONG(py_precision);

	if (py_scale != NULL && py_scale != Py_None && 
       TYPE(py_scale) == PYTHON_FIXNUM)
		scale = PyInt_AS_LONG(py_scale);

	if (py_size != NULL && py_size != Py_None && 
       TYPE(py_size) == PYTHON_FIXNUM)
		size = PyInt_AS_LONG(py_size);

   if (!NIL_P(stmt_res)) {
      /* Check for Param options */
      switch (PyTuple_Size(args)) {
         /* if argc == 3, then the default value for param_type will be used */
         case 3:
            param_type = SQL_PARAM_INPUT;
            rc = SQLDescribeParam((SQLHSTMT)stmt_res->hstmt, 
			    (SQLUSMALLINT)param_no, &sql_data_type, 
                                  &sql_precision, &sql_scale, &sql_nullable);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT,
                                               rc, 1, NULL, -1, 1);
               sprintf(error, "Describe Param Failed: %s", 
                       IBM_DB_G(__python_stmt_err_msg));
               PyErr_SetString(PyExc_Exception, error);
               return NULL;
            }
            /* Add to cache */
            _python_ibm_db_add_param_cache(stmt_res, param_no, var_pyvalue, 
                                           param_type, size, 
                                           sql_data_type, sql_precision, 
                                           sql_scale, sql_nullable );
            break;

         case 4:
            rc = SQLDescribeParam((SQLHSTMT)stmt_res->hstmt, 
                                  (SQLUSMALLINT)param_no, &sql_data_type, 
                                  &sql_precision, &sql_scale, &sql_nullable);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT,
                                               rc, 1, NULL, -1, 1);
               sprintf(error, "Describe Param Failed: %s", 
                       IBM_DB_G(__python_stmt_err_msg));
               PyErr_SetString(PyExc_Exception, error);
               return NULL;
            }
            /* Add to cache */
            _python_ibm_db_add_param_cache(stmt_res, param_no, var_pyvalue,
                                           param_type, size, 
                                           sql_data_type, sql_precision, 
                                           sql_scale, sql_nullable );
            break;

         case 5:
            rc = SQLDescribeParam((SQLHSTMT)stmt_res->hstmt, 
                                  (SQLUSMALLINT)param_no, &sql_data_type, 
                                  &sql_precision, &sql_scale, &sql_nullable);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT,                                               rc, 1, NULL, -1, 1);
               sprintf(error, "Describe Param Failed: %s", 
                       IBM_DB_G(__python_stmt_err_msg));
               PyErr_SetString(PyExc_Exception, error);
               return NULL;
            }
            sql_data_type = (SQLSMALLINT)data_type;
            /* Add to cache */
            _python_ibm_db_add_param_cache(stmt_res, param_no, var_pyvalue,
                                           param_type, size, 
                                           sql_data_type, sql_precision, 
                                           sql_scale, sql_nullable );
            break;

         case 6:
            rc = SQLDescribeParam((SQLHSTMT)stmt_res->hstmt, 
                                  (SQLUSMALLINT)param_no, &sql_data_type, 
                                  &sql_precision, &sql_scale, &sql_nullable);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT,
                                               rc, 1, NULL, -1, 1);
               sprintf(error, "Describe Param Failed: %s", 
                       IBM_DB_G(__python_stmt_err_msg));
               PyErr_SetString(PyExc_Exception, error);
               return NULL;
            }
            sql_data_type = (SQLSMALLINT)data_type;
            sql_precision = (SQLUINTEGER)precision;
            /* Add to cache */
            _python_ibm_db_add_param_cache(stmt_res, param_no, var_pyvalue,
                                           param_type, size, 
                                           sql_data_type, sql_precision, 
                                           sql_scale, sql_nullable );
            break;

         case 7:
         case 8:
            /* Cache param data passed 
             * I am using a linked list of nodes here because I don't know 
             * before hand how many params are being passed in/bound. 
             * To determine this, a call to SQLNumParams is necessary. 
             * This is take away any advantages an array would have over 
             * linked list access 
             * Data is being copied over to the correct types for subsequent 
             * CLI call because this might cause problems on other platforms 
             * such as AIX 
             */
            sql_data_type = (SQLSMALLINT)data_type;
            sql_precision = (SQLUINTEGER)precision;
            sql_scale = (SQLSMALLINT)scale;
            _python_ibm_db_add_param_cache(stmt_res, param_no, var_pyvalue,
                                           param_type, size, 
                                           sql_data_type, sql_precision, 
                                           sql_scale, sql_nullable );
            break;

         default:
            /* WRONG_PARAM_COUNT; */
            return NULL;
      }
      /* end Switch */

      /* We bind data with DB2 CLI in ibm_db.execute() */
      /* This will save network flow if we need to override params in it */

		Py_INCREF(Py_True);
      return Py_True;
   } else {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
      return NULL;
   }
}

/*!# ibm_db.close
 *
 * ===Description
 *
 * bool ibm_db.close ( resource connection )
 *
 * This function closes a DB2 client connection created with ibm_db.connect()
 * and returns the corresponding resources to the database server.
 *
 * If you attempt to close a persistent DB2 client connection created with
 * ibm_db.pconnect(), the close request returns TRUE and the persistent IBM Data
 * Server client connection remains available for the next caller.
 *
 * ===Parameters
 *
 * ====connection
 *    Specifies an active DB2 client connection.
 *
 * ===Return Values
 * Returns TRUE on success or FALSE on failure.
 */
static PyObject *ibm_db_close(PyObject *self, PyObject *args)            
{
   conn_handle *conn_res = NULL;
   int rc;

   if (!PyArg_ParseTuple(args, "O", &conn_res))
		return NULL;

   if (!NIL_P(conn_res)) {
      /* Check to see if it's a persistent connection; 
       * if so, just return true 
       */

      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      if ( conn_res->handle_active && !conn_res->flag_pconnect ) {
         /* Disconnect from DB. If stmt is allocated, 
          * it is freed automatically 
          */
         if (conn_res->auto_commit == 0) {
            rc = SQLEndTran(SQL_HANDLE_DBC, (SQLHDBC)conn_res->hdbc, 
                            SQL_ROLLBACK);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, 
                                               rc, 1, NULL, -1, 1);
					
               return NULL;
            }
         }
         rc = SQLDisconnect((SQLHDBC)conn_res->hdbc);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 
                                            1, NULL, -1, 1);
            return NULL;
         }

         rc = SQLFreeHandle( SQL_HANDLE_DBC, conn_res->hdbc);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 
                                            1, NULL, -1, 1);
            return NULL;
         }
         conn_res->handle_active = 0;
			Py_INCREF(Py_True);
         return Py_True;
      } else if ( conn_res->flag_pconnect ) {
         /* Do we need to call FreeStmt or something to close cursors? */
			Py_INCREF(Py_True);
         return Py_True;
      } else {
         return NULL;
      }
   } else {
      return NULL;
   }
}

/*!# ibm_db.column_privileges
 *
 * ===Description
 * resource ibm_db.column_privileges ( resource connection [, string qualifier
 * [, string schema [, string table-name [, string column-name]]]] )
 *
 * Returns a result set listing the columns and associated privileges for a
 * table.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the tables. To match all schemas, pass NULL
 * or an empty string.
 *
 * ====table-name
 *       The name of the table or view. To match all tables in the database,
 * pass NULL or an empty string.
 *
 * ====column-name
 *       The name of the column. To match all columns in the table, pass NULL
 * or an empty string.
 *
 * ===Return Values
 * Returns a statement resource with a result set containing rows describing
 * the column privileges for columns matching the specified parameters. The rows
 * are composed of the following columns:
 *
 * TABLE_CAT:: Name of the catalog. The value is NULL if this table does not
 * have catalogs.
 * TABLE_SCHEM:: Name of the schema.
 * TABLE_NAME:: Name of the table or view.
 * COLUMN_NAME:: Name of the column.
 * GRANTOR:: Authorization ID of the user who granted the privilege.
 * GRANTEE:: Authorization ID of the user to whom the privilege was granted.
 * PRIVILEGE:: The privilege for the column.
 * IS_GRANTABLE:: Whether the GRANTEE is permitted to grant this privilege to
 * other users.
 */
static PyObject *ibm_db_column_privileges(PyObject *self, PyObject *args)
{
   PyObject *py_qualifier = NULL;
   SQLCHAR  *qualifier = NULL;
   PyObject *py_owner = NULL;
   SQLCHAR  *owner = NULL;
   PyObject *py_table_name = NULL;
   SQLCHAR  *table_name = NULL;
   PyObject *py_column_name = NULL;
   SQLCHAR  *column_name = NULL;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   int rc;

	if (!PyArg_ParseTuple(args, "O|OOOO", &conn_res, &py_qualifier, &py_owner,
                         &py_table_name, &py_column_name))
      return NULL;

	if (py_qualifier != NULL && py_qualifier != Py_None) {
		if (PyString_Check(py_qualifier))
			qualifier = PyString_AsString(py_qualifier);
		else {
			PyErr_SetString(PyExc_Exception, "qualifier must be a string");
			return NULL;
		}
	}

   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_table_name != NULL && py_table_name != Py_None) {
      if (PyString_Check(py_table_name))
         table_name = PyString_AsString(py_table_name);
      else {
         PyErr_SetString(PyExc_Exception, "table_name must be a string");
         return NULL;
      }
   }

   if (py_column_name != NULL && py_column_name != Py_None) {
      if (PyString_Check(py_column_name))
         column_name = PyString_AsString(py_column_name);
      else {
         PyErr_SetString(PyExc_Exception, "column_name must be a string");
         return NULL;
      }
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      if (!conn_res) {
         PyErr_SetString(PyExc_Exception,"Connection Resource cannot be found");
         return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLColumnPrivileges((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS,
                               owner,SQL_NTS, table_name,SQL_NTS, column_name,
                               SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                         SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
			Py_INCREF(Py_False);
			return Py_False;
      }
		return (PyObject *)stmt_res;
   } else {
      Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.columns
 * ===Description
 * resource ibm_db.columns ( resource connection [, string qualifier
 * [, string schema [, string table-name [, string column-name]]]] )
 *
 * Returns a result set listing the columns and associated metadata for a table.
 *
 * ===Parameters
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the tables. To match all schemas, pass '%'.
 *
 * ====table-name
 *       The name of the table or view. To match all tables in the database,
 * pass NULL or an empty string.
 *
 * ====column-name
 *       The name of the column. To match all columns in the table, pass NULL or
 * an empty string.
 *
 * ===Return Values
 * Returns a statement resource with a result set containing rows describing the
 * columns matching the specified parameters.
 * The rows are composed of the following columns:
 *
 * TABLE_CAT:: Name of the catalog. The value is NULL if this table does not
 * have catalogs.
 * TABLE_SCHEM:: Name of the schema.
 * TABLE_NAME:: Name of the table or view.
 * COLUMN_NAME:: Name of the column.
 * DATA_TYPE:: The SQL data type for the column represented as an integer value.
 * TYPE_NAME:: A string representing the data type for the column.
 * COLUMN_SIZE:: An integer value representing the size of the column.
 * BUFFER_LENGTH:: Maximum number of bytes necessary to store data from this
 * column.
 * DECIMAL_DIGITS:: The scale of the column, or NULL where scale is not
 * applicable.
 * NUM_PREC_RADIX:: An integer value of either 10 (representing an exact numeric
 * data type), 2 (representing an approximate numeric data type), or NULL
 * (representing a data type for which radix is not applicable).
 * NULLABLE:: An integer value representing whether the column is nullable or
 * not.
 * REMARKS:: Description of the column.
 * COLUMN_DEF:: Default value for the column.
 * SQL_DATA_TYPE:: An integer value representing the size of the column.
 * SQL_DATETIME_SUB:: Returns an integer value representing a datetime subtype
 * code, or NULL for SQL data types to which this does not apply.
 * CHAR_OCTET_LENGTH::   Maximum length in octets for a character data type
 * column, which matches COLUMN_SIZE for single-byte character set data, or
 * NULL for non-character data types.
 * ORDINAL_POSITION:: The 1-indexed position of the column in the table.
 * IS_NULLABLE:: A string value where 'YES' means that the column is nullable
 * and 'NO' means that the column is not nullable.
 */
static PyObject *ibm_db_columns(PyObject *self, PyObject *args)           
{
   SQLCHAR *qualifier = NULL;
   SQLCHAR *owner = NULL;
   SQLCHAR *table_name = NULL;
   SQLCHAR *column_name = NULL;
   PyObject *py_qualifier = NULL;
   PyObject *py_owner = NULL;
   PyObject *py_table_name = NULL;
   PyObject *py_column_name = NULL;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   int rc;

   if (!PyArg_ParseTuple(args, "O|OOOO", &conn_res, &py_qualifier, &py_owner,
                         &py_table_name, &py_column_name))
      return NULL;

	if (py_qualifier != NULL && py_qualifier == Py_None) {
		qualifier = NULL;
	} else if (py_qualifier != NULL && PyString_Check(py_qualifier)) {
		qualifier = PyString_AsString(py_qualifier);
	} else {
		PyErr_SetString(PyExc_Exception, "qualifier must be a string or None");
		return NULL;
	}

	if (py_owner != NULL && py_owner == Py_None) {
		owner = NULL;
	} else if (py_owner != NULL && PyString_Check(py_owner)) {
		owner = PyString_AsString(py_owner);
	} else {
		PyErr_SetString(PyExc_Exception, "owner must be a string or None");
		return NULL;
	}

   if (py_table_name != NULL && py_table_name != Py_None) {
      if (PyString_Check(py_table_name))
         table_name = PyString_AsString(py_table_name);
      else {
         PyErr_SetString(PyExc_Exception, "table_name must be a string");
         return NULL;
      }
   }

   if (py_column_name != NULL && py_column_name != Py_None) {
      if (PyString_Check(py_column_name))
         column_name = PyString_AsString(py_column_name);
      else {
         PyErr_SetString(PyExc_Exception, "column_name must be a string");
         return NULL;
      }
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }
      
      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
         return NULL;
      }
      rc = SQLColumns((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS,
                      owner,SQL_NTS, table_name,SQL_NTS, column_name,SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors((SQLHSTMT)stmt_res->hstmt, 
                                         SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
         return NULL;
      }
      return (PyObject *)stmt_res;                  
   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.foreign_keys
 *
 * ===Description
 * resource ibm_db.foreign_keys ( resource connection, string pk_qualifier,
 * string pk_schema, string pk_table-name, string fk_qualifier
 * string fk_schema, string fk_table-name )
 *
 * Returns a result set listing the foreign keys for a table.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====pk_qualifier
 *       A qualifier for the pk_table-name argument for the DB2 databases 
 * running on OS/390 or z/OS servers. For other databases, pass NULL or an empty 
 * string. 
 *
 * ====pk_schema
 *       The schema for the pk_table-name argument which contains the tables. If 
 * schema is NULL, ibm_db.foreign_keys() matches the schema for the current 
 * connection. 
 *
 * ====pk_table-name
 *       The name of the table which contains the primary key.
 *
 * ====fk_qualifier
 *       A qualifier for the fk_table-name argument for the DB2 databases
 * running on OS/390 or z/OS servers. For other databases, pass NULL or an empty
 * string.
 *
 * ====fk_schema
 *       The schema for the fk_table-name argument which contains the tables. If
 * schema is NULL, ibm_db.foreign_keys() matches the schema for the current
 * connection.
 *
 * ====fk_table-name
 *       The name of the table which contains the foreign key.
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows describing the
 * foreign keys for the specified table. The result set is composed of the
 * following columns:
 *
 * Column name::   Description
 * PKTABLE_CAT:: Name of the catalog for the table containing the primary key.
 * The value is NULL if this table does not have catalogs.
 * PKTABLE_SCHEM:: Name of the schema for the table containing the primary key.
 * PKTABLE_NAME:: Name of the table containing the primary key.
 * PKCOLUMN_NAME:: Name of the column containing the primary key.
 * FKTABLE_CAT:: Name of the catalog for the table containing the foreign key.
 * The value is NULL if this table does not have catalogs.
 * FKTABLE_SCHEM:: Name of the schema for the table containing the foreign key.
 * FKTABLE_NAME:: Name of the table containing the foreign key.
 * FKCOLUMN_NAME:: Name of the column containing the foreign key.
 * KEY_SEQ:: 1-indexed position of the column in the key.
 * UPDATE_RULE:: Integer value representing the action applied to the foreign
 * key when the SQL operation is UPDATE.
 * DELETE_RULE:: Integer value representing the action applied to the foreign
 * key when the SQL operation is DELETE.
 * FK_NAME:: The name of the foreign key.
 * PK_NAME:: The name of the primary key.
 * DEFERRABILITY:: An integer value representing whether the foreign key
 * deferrability is SQL_INITIALLY_DEFERRED, SQL_INITIALLY_IMMEDIATE, or
 * SQL_NOT_DEFERRABLE.
 */
static PyObject *ibm_db_foreign_keys(PyObject *self, PyObject *args)
{
	PyObject *py_pk_qualifier = NULL;
   SQLCHAR *pk_qualifier = NULL;
	PyObject *py_pk_owner = NULL;
   SQLCHAR *pk_owner = NULL;
	PyObject *py_pk_table_name = NULL;
   SQLCHAR *pk_table_name = NULL;
   PyObject *py_fk_qualifier = NULL;
   SQLCHAR *fk_qualifier = NULL;
   PyObject *py_fk_owner = NULL;
   SQLCHAR *fk_owner = NULL;
   PyObject *py_fk_table_name = NULL;
   SQLCHAR *fk_table_name = NULL;
   conn_handle *conn_res = NULL;
   stmt_handle *stmt_res;
   int rc;

	if (!PyArg_ParseTuple(args, "OOOO|OOO", &conn_res, &py_pk_qualifier, 
                         &py_pk_owner, &py_pk_table_name, &py_fk_qualifier,
                         &py_fk_owner, &py_fk_table_name))
      return NULL;

   if (py_pk_qualifier != NULL && py_pk_qualifier != Py_None) {
      if (PyString_Check(py_pk_qualifier))
         pk_qualifier = PyString_AsString(py_pk_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, 
           "qualifier for table containing primary key must be a string");
         return NULL;
      }
   }

   if (py_pk_owner != NULL && py_pk_owner != Py_None) {
      if (PyString_Check(py_pk_owner))
         pk_owner = PyString_AsString(py_pk_owner);
      else {
         PyErr_SetString(PyExc_Exception,  
          "owner of table containing primary key must be a string");
         return NULL;
      }
   }

   if (py_pk_table_name != NULL && py_pk_table_name != Py_None) {
      if (PyString_Check(py_pk_table_name))
         pk_table_name = PyString_AsString(py_pk_table_name);
      else {
         PyErr_SetString(PyExc_Exception, 
          "name of the table that contains primary key must be a string");
         return NULL;
      }
   }

   if (py_fk_qualifier != NULL && py_fk_qualifier != Py_None) {
      if (PyString_Check(py_fk_qualifier))
         fk_qualifier = PyString_AsString(py_fk_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, 
          "qualifier for table containing the foreign key must be a string");
         return NULL;
      }
   }

   if (py_fk_owner != NULL && py_fk_owner != Py_None) {
      if (PyString_Check(py_fk_owner))
         fk_owner = PyString_AsString(py_fk_owner);
      else {
         PyErr_SetString(PyExc_Exception, 
          "owner of table containing the foreign key must be a string");
         return NULL;
      }
   }

   if (py_fk_table_name != NULL && py_fk_table_name != Py_None) {
      if (PyString_Check(py_fk_table_name))
         fk_table_name = PyString_AsString(py_fk_table_name);
      else {
         PyErr_SetString(PyExc_Exception, 
          "name of the table that contains foreign key must be a string");
         return NULL;
      }
   }


   if (conn_res) {

      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLForeignKeys((SQLHSTMT)stmt_res->hstmt, pk_qualifier, SQL_NTS,
                          pk_owner, SQL_NTS, pk_table_name ,SQL_NTS, fk_qualifier, SQL_NTS,
                          fk_owner, SQL_NTS, fk_table_name, SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      return (PyObject *)stmt_res;                  

   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.primary_keys
 *
 * ===Description
 * resource ibm_db.primary_keys ( resource connection, string qualifier,
 * string schema, string table-name )
 *
 * Returns a result set listing the primary keys for a table.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the tables. If schema is NULL,
 * ibm_db.primary_keys() matches the schema for the current connection.
 *
 * ====table-name
 *       The name of the table.
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows describing the
 * primary keys for the specified table.
 * The result set is composed of the following columns:
 *
 * Column name:: Description
 * TABLE_CAT:: Name of the catalog for the table containing the primary key.
 * The value is NULL if this table does not have catalogs.
 * TABLE_SCHEM:: Name of the schema for the table containing the primary key.
 * TABLE_NAME:: Name of the table containing the primary key.
 * COLUMN_NAME:: Name of the column containing the primary key.
 * KEY_SEQ:: 1-indexed position of the column in the key.
 * PK_NAME:: The name of the primary key.
 */
static PyObject *ibm_db_primary_keys(PyObject *self, PyObject *args)
{
	PyObject *py_qualifier = NULL;
   SQLCHAR *qualifier = NULL;
	PyObject *py_owner = NULL;
   SQLCHAR *owner = NULL;
	PyObject *py_table_name = NULL;
   SQLCHAR *table_name = NULL;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   int rc;

	if (!PyArg_ParseTuple(args, "OOOO", &conn_res, &py_qualifier, &py_owner,
                         &py_table_name))
      return NULL;

   if (py_qualifier != NULL && py_qualifier != Py_None) {
      if (PyString_Check(py_qualifier))
         qualifier = PyString_AsString(py_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, "qualifier must be a string");
         return NULL;
      } 
   }
   
   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_table_name != NULL && py_table_name != Py_None) {
      if (PyString_Check(py_table_name))
         table_name = PyString_AsString(py_table_name);
      else {
         PyErr_SetString(PyExc_Exception, "table_name must be a string");
         return NULL;
      }
   }

   if (conn_res) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLPrimaryKeys((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS,
                          owner, SQL_NTS, table_name,SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      return (PyObject *)stmt_res;             

   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.procedure_columns
 *
 * ===Description
 * resource ibm_db.procedure_columns ( resource connection, string qualifier,
 * string schema, string procedure, string parameter )
 *
 * Returns a result set listing the parameters for one or more stored procedures
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the procedures. This parameter accepts a
 * search pattern containing _ and % as wildcards.
 *
 * ====procedure
 *       The name of the procedure. This parameter accepts a search pattern
 * containing _ and % as wildcards.
 *
 * ====parameter
 *       The name of the parameter. This parameter accepts a search pattern
 * containing _ and % as wildcards.
 *       If this parameter is NULL, all parameters for the specified stored
 * procedures are returned.
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows describing the
 * parameters for the stored procedures matching the specified parameters. The
 * rows are composed of the following columns:
 *
 * Column name::   Description
 * PROCEDURE_CAT:: The catalog that contains the procedure. The value is NULL
 * if this table does not have catalogs.
 * PROCEDURE_SCHEM:: Name of the schema that contains the stored procedure.
 * PROCEDURE_NAME:: Name of the procedure.
 * COLUMN_NAME:: Name of the parameter.
 * COLUMN_TYPE:: An integer value representing the type of the parameter:
 *                      Return value:: Parameter type
 *                      1:: (SQL_PARAM_INPUT)   Input (IN) parameter.
 *                      2:: (SQL_PARAM_INPUT_OUTPUT) Input/output (INOUT)
 *                          parameter.
 *                      3:: (SQL_PARAM_OUTPUT) Output (OUT) parameter.
 * DATA_TYPE:: The SQL data type for the parameter represented as an integer
 * value.
 * TYPE_NAME:: A string representing the data type for the parameter.
 * COLUMN_SIZE:: An integer value representing the size of the parameter.
 * BUFFER_LENGTH:: Maximum number of bytes necessary to store data for this
 * parameter.
 * DECIMAL_DIGITS:: The scale of the parameter, or NULL where scale is not
 * applicable.
 * NUM_PREC_RADIX:: An integer value of either 10 (representing an exact numeric
 * data type), 2 (representing anapproximate numeric data type), or NULL
 * (representing a data type for which radix is not applicable).
 * NULLABLE:: An integer value representing whether the parameter is nullable or
 * not.
 * REMARKS:: Description of the parameter.
 * COLUMN_DEF:: Default value for the parameter.
 * SQL_DATA_TYPE:: An integer value representing the size of the parameter.
 * SQL_DATETIME_SUB:: Returns an integer value representing a datetime subtype
 * code, or NULL for SQL data types to which this does not apply.
 * CHAR_OCTET_LENGTH:: Maximum length in octets for a character data type
 * parameter, which matches COLUMN_SIZE for single-byte character set data, or
 * NULL for non-character data types.
 * ORDINAL_POSITION:: The 1-indexed position of the parameter in the CALL
 * statement.
 * IS_NULLABLE:: A string value where 'YES' means that the parameter accepts or
 * returns NULL values and 'NO' means that the parameter does not accept or
 * return NULL values.
 */
static PyObject *ibm_db_procedure_columns(PyObject *self, PyObject *args)
{
   SQLCHAR *qualifier = NULL;
   SQLCHAR *owner = NULL;
   SQLCHAR *proc_name = NULL;
   SQLCHAR *column_name = NULL;
   PyObject *py_qualifier = NULL;
   PyObject *py_owner = NULL;
   PyObject *py_proc_name = NULL;
   PyObject *py_column_name = NULL;
   int rc = 0;
   conn_handle *conn_res;
   stmt_handle *stmt_res;

	if (!PyArg_ParseTuple(args, "O|OOOO", &conn_res, &py_qualifier, &py_owner,
                         &py_proc_name, &py_column_name))
		return NULL;

   if (py_qualifier != NULL && py_qualifier != Py_None) {
      if (PyString_Check(py_qualifier))
         qualifier = PyString_AsString(py_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, "qualifier must be a string");
         return NULL;
      }
   }
   
   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_proc_name != NULL && py_proc_name != Py_None) {
      if (PyString_Check(py_proc_name))
         proc_name = PyString_AsString(py_proc_name);
      else {
         PyErr_SetString(PyExc_Exception, "procedure name must be a string");
         return NULL;
      }
   }

   if (py_column_name != NULL && py_column_name != Py_None) {
      if (PyString_Check(py_column_name))
         column_name = PyString_AsString(py_column_name);
      else {
         PyErr_SetString(PyExc_Exception, "column_name must be a string");
         return NULL;
      }
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False); 
         return Py_False;
      }
      rc = SQLProcedureColumns((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS, 
                               owner, SQL_NTS, proc_name, SQL_NTS, column_name, 
                               SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      return (PyObject *)stmt_res;            
   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.procedures
 *
 * ===Description
 * resource ibm_db.procedures ( resource connection, string qualifier,
 * string schema, string procedure )
 *
 * Returns a result set listing the stored procedures registered in a database.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the procedures. This parameter accepts a
 * search pattern containing _ and % as wildcards.
 *
 * ====procedure
 *       The name of the procedure. This parameter accepts a search pattern
 * containing _ and % as wildcards.
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows describing the
 * stored procedures matching the specified parameters. The rows are composed of
 * the following columns:
 *
 * Column name:: Description
 * PROCEDURE_CAT:: The catalog that contains the procedure. The value is NULL if
 * this table does not have catalogs.
 * PROCEDURE_SCHEM:: Name of the schema that contains the stored procedure.
 * PROCEDURE_NAME:: Name of the procedure.
 * NUM_INPUT_PARAMS:: Number of input (IN) parameters for the stored procedure.
 * NUM_OUTPUT_PARAMS:: Number of output (OUT) parameters for the stored
 * procedure.
 * NUM_RESULT_SETS:: Number of result sets returned by the stored procedure.
 * REMARKS:: Any comments about the stored procedure.
 * PROCEDURE_TYPE:: Always returns 1, indicating that the stored procedure does
 * not return a return value.
 */
static PyObject *ibm_db_procedures(PyObject *self, PyObject *args)           
{
   SQLCHAR *qualifier = NULL;
   SQLCHAR *owner = NULL;
   SQLCHAR *proc_name = NULL;
   int rc = 0;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   PyObject *py_qualifier = NULL;
   PyObject *py_owner = NULL;
   PyObject *py_proc_name = NULL;

   if (!PyArg_ParseTuple(args, "OOOO", &conn_res, &py_qualifier, &py_owner,
                         &py_proc_name))
      return NULL;

   if (py_qualifier != NULL && py_qualifier != Py_None) {
      if (PyString_Check(py_qualifier))
         qualifier = PyString_AsString(py_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, "qualifier must be a string");
         return NULL;
      }
   }

   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_proc_name != NULL && py_proc_name != Py_None) {
      if (PyString_Check(py_proc_name))
         proc_name = PyString_AsString(py_proc_name);
      else {
         PyErr_SetString(PyExc_Exception, "procedure name must be a string");
         return NULL;
      }
   }


   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLProcedures((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS, owner,
                         SQL_NTS, proc_name, SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      return (PyObject *)stmt_res;                
   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.special_columns
 *
 * ===Description
 * resource ibm_db.special_columns ( resource connection, string qualifier,
 * string schema, string table_name, int scope )
 *
 * Returns a result set listing the unique row identifier columns for a table.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the tables.
 *
 * ====table_name
 *       The name of the table.
 *
 * ====scope
 *       Integer value representing the minimum duration for which the unique
 * row identifier is valid. This can be one of the following values:
 *
 *       0: Row identifier is valid only while the cursor is positioned on the
 * row. (SQL_SCOPE_CURROW)
 *       1: Row identifier is valid for the duration of the transaction.
 * (SQL_SCOPE_TRANSACTION)
 *       2: Row identifier is valid for the duration of the connection.
 * (SQL_SCOPE_SESSION)
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows with unique
 * row identifier information for a table.
 * The rows are composed of the following columns:
 *
 * Column name:: Description
 *
 * SCOPE:: Integer value representing the minimum duration for which the unique
 * row identifier is valid.
 *
 *             0: Row identifier is valid only while the cursor is positioned on
 * the row. (SQL_SCOPE_CURROW)
 *
 *             1: Row identifier is valid for the duration of the transaction.
 * (SQL_SCOPE_TRANSACTION)
 *
 *             2: Row identifier is valid for the duration of the connection.
 * (SQL_SCOPE_SESSION)
 *
 * COLUMN_NAME:: Name of the unique column.
 *
 * DATA_TYPE:: SQL data type for the column.
 *
 * TYPE_NAME:: Character string representation of the SQL data type for the
 * column.
 *
 * COLUMN_SIZE:: An integer value representing the size of the column.
 *
 * BUFFER_LENGTH:: Maximum number of bytes necessary to store data from this
 * column.
 *
 * DECIMAL_DIGITS:: The scale of the column, or NULL where scale is not
 * applicable.
 *
 * NUM_PREC_RADIX:: An integer value of either 10 (representing an exact numeric
 * data type),2 (representing an approximate numeric data type), or NULL
 * (representing a data type for which radix is not applicable).
 *
 * PSEUDO_COLUMN:: Always returns 1.
 */
static PyObject *ibm_db_special_columns(PyObject *self, PyObject *args)
{
   SQLCHAR *qualifier = NULL;
   SQLCHAR *owner = NULL;
   SQLCHAR *table_name = NULL;
   int scope = 0;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   int rc;
   PyObject *py_qualifier = NULL;
   PyObject *py_owner = NULL;
   PyObject *py_table_name = NULL;

	if (!PyArg_ParseTuple(args, "OOOOi", &conn_res, &py_qualifier, &py_owner,
                         &py_table_name, &scope))
      return NULL;

   if (py_qualifier != NULL && py_qualifier != Py_None) {
      if (PyString_Check(py_qualifier))
         qualifier = PyString_AsString(py_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, "qualifier must be a string");
         return NULL;
      }
   }

   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_table_name != NULL && py_table_name != Py_None) {
      if (PyString_Check(py_table_name))
         table_name = PyString_AsString(py_table_name);
      else {
         PyErr_SetString(PyExc_Exception, "table name must be a string");
         return NULL;
      }
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLSpecialColumns((SQLHSTMT)stmt_res->hstmt,SQL_BEST_ROWID, 
                             qualifier, SQL_NTS, owner,SQL_NTS, table_name,
                             SQL_NTS,scope,SQL_NULLABLE);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      return (PyObject *)stmt_res;
   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.statistics
 *
 * ===Description
 * resource ibm_db.statistics ( resource connection, string qualifier,
 * string schema, string table-name, bool unique )
 *
 * Returns a result set listing the index and statistics for a table.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema that contains the targeted table. If this parameter is NULL,
 * the statistics and indexes are returned for the schema of the current user.
 *
 * ====table_name
 *       The name of the table.
 *
 * ====unique
 *       A boolean value representing the type of index information to return.
 *
 *       False     Return only the information for unique indexes on the table.
 *
 *       True      Return the information for all indexes on the table.
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows describing the
 * statistics and indexes for the base tables matching the specified parameters.
 * The rows are composed of the following columns:
 *
 * Column name:: Description
 * TABLE_CAT:: The catalog that contains the table. The value is NULL if this
 * table does not have catalogs.
 * TABLE_SCHEM:: Name of the schema that contains the table.
 * TABLE_NAME:: Name of the table.
 * NON_UNIQUE:: An integer value representing whether the index prohibits unique
 * values, or whether the row represents statistics on the table itself:
 *
 *                     Return value:: Parameter type
 *                     0 (SQL_FALSE):: The index allows duplicate values.
 *                     1 (SQL_TRUE):: The index values must be unique.
 *                     NULL:: This row is statistics information for the table
 *                     itself.
 *
 * INDEX_QUALIFIER:: A string value representing the qualifier that would have
 * to be prepended to INDEX_NAME to fully qualify the index.
 * INDEX_NAME:: A string representing the name of the index.
 * TYPE:: An integer value representing the type of information contained in
 * this row of the result set:
 *
 *            Return value:: Parameter type
 *            0 (SQL_TABLE_STAT):: The row contains statistics about the table
 *                                 itself.
 *            1 (SQL_INDEX_CLUSTERED):: The row contains information about a
 *                                      clustered index.
 *            2 (SQL_INDEX_HASH):: The row contains information about a hashed
 *                                 index.
 *            3 (SQL_INDEX_OTHER):: The row contains information about a type of
 * index that is neither clustered nor hashed.
 *
 * ORDINAL_POSITION:: The 1-indexed position of the column in the index. NULL if
 * the row contains statistics information about the table itself.
 * COLUMN_NAME:: The name of the column in the index. NULL if the row contains
 * statistics information about the table itself.
 * ASC_OR_DESC:: A if the column is sorted in ascending order, D if the column
 * is sorted in descending order, NULL if the row contains statistics
 * information about the table itself.
 * CARDINALITY:: If the row contains information about an index, this column
 * contains an integer value representing the number of unique values in the
 * index. If the row contains information about the table itself, this column
 * contains an integer value representing the number of rows in the table.
 * PAGES:: If the row contains information about an index, this column contains
 * an integer value representing the number of pages used to store the index. If
 * the row contains information about the table itself, this column contains an
 * integer value representing the number of pages used to store the table.
 * FILTER_CONDITION:: Always returns NULL.
 */ 
static PyObject *ibm_db_statistics(PyObject *self, PyObject *args)
{
   SQLCHAR *qualifier = NULL;
   SQLCHAR *owner = NULL;
   SQLCHAR *table_name = NULL;
   int unique = 0;
   int rc = 0;
   SQLUSMALLINT sql_unique;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   PyObject *py_qualifier = NULL;
   PyObject *py_owner = NULL;
   PyObject *py_table_name = NULL;
   PyObject *py_unique = NULL;

	if (!PyArg_ParseTuple(args, "OOOOO", &conn_res, &py_qualifier, &py_owner,
                         &py_table_name, &py_unique))
      return NULL;

   if (py_qualifier != NULL && py_qualifier != Py_None) {
      if (PyString_Check(py_qualifier))
         qualifier = PyString_AsString(py_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, "qualifier must be a string");
         return NULL;
      }
   }

   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_table_name != NULL && py_table_name != Py_None) {
      if (PyString_Check(py_table_name))
         table_name = PyString_AsString(py_table_name);
      else {
         PyErr_SetString(PyExc_Exception, "table name must be a string");
         return NULL;
      }
   }

   if (py_unique != NULL && py_unique != Py_None) {
      if (PyBool_Check(py_unique)) {
			if (py_unique == Py_True)
				unique = 1;
			else
				unique = 0;
		}
      else {
         PyErr_SetString(PyExc_Exception, "unique must be a boolean");
         return NULL;
      }
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);
      sql_unique = unique;

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLStatistics((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS, owner,
                         SQL_NTS, table_name, SQL_NTS, sql_unique, SQL_QUICK);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
         Py_INCREF(Py_False);
         return Py_False;
      }
      return (PyObject *)stmt_res;
   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.table_privileges
 *
 * ===Description
 * resource ibm_db.table_privileges ( resource connection [, string qualifier
 * [, string schema [, string table_name]]] )
 *
 * Returns a result set listing the tables and associated privileges in a
 * database.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the tables. This parameter accepts a search
 * pattern containing _ and % as wildcards.
 *
 * ====table_name
 *       The name of the table. This parameter accepts a search pattern
 * containing _ and % as wildcards.
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows describing
 * the privileges for the tables that match the specified parameters. The rows
 * are composed of the following columns:
 *
 * Column name:: Description
 * TABLE_CAT:: The catalog that contains the table. The value is NULL if this
 * table does not have catalogs.
 * TABLE_SCHEM:: Name of the schema that contains the table.
 * TABLE_NAME:: Name of the table.
 * GRANTOR:: Authorization ID of the user who granted the privilege.
 * GRANTEE:: Authorization ID of the user to whom the privilege was granted.
 * PRIVILEGE:: The privilege that has been granted. This can be one of ALTER,
 * CONTROL, DELETE, INDEX, INSERT, REFERENCES, SELECT, or UPDATE.
 * IS_GRANTABLE:: A string value of "YES" or "NO" indicating whether the grantee
 * can grant the privilege to other users.
 */
static PyObject *ibm_db_table_privileges(PyObject *self, PyObject *args)
{
   SQLCHAR *qualifier = NULL;
   SQLCHAR *owner = NULL;
   SQLCHAR *table_name = NULL;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   int rc;
   PyObject *py_qualifier = NULL;
   PyObject *py_owner = NULL;
   PyObject *py_table_name = NULL;

	if (!PyArg_ParseTuple(args, "O|OOO", &conn_res, &py_qualifier, &py_owner,
                         &py_table_name))
      return NULL;

   if (py_qualifier != NULL && py_qualifier != Py_None) {
      if (PyString_Check(py_qualifier))
         qualifier = PyString_AsString(py_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, "qualifier must be a string");
         return NULL;
      }
   }
   
   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_table_name != NULL && py_table_name != Py_None) {
      if (PyString_Check(py_table_name))
         table_name = PyString_AsString(py_table_name);
      else {
         PyErr_SetString(PyExc_Exception, "table_name must be a string");
         return NULL;
      }
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      if (!conn_res) {
         PyErr_SetString(PyExc_Exception,"Connection Resource cannot be found");
         Py_RETURN_FALSE;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
         Py_RETURN_FALSE;
      }
      rc = SQLTablePrivileges((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS, 
                              owner, SQL_NTS, table_name, SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
         Py_RETURN_FALSE;
      }
      return (PyObject *)stmt_res;
   } else {
      Py_RETURN_FALSE;
   }
}

/*!# ibm_db.tables
 *
 * ===Description
 * resource ibm_db.tables ( resource connection [, string qualifier [, string
 * schema [, string table-name [, string table-type]]]] )
 *
 * Returns a result set listing the tables and associated metadata in a database
 *
 * ===Parameters
 *
 * ====connection
 *       A valid connection to an IBM DB2, Cloudscape, or Apache Derby database.
 *
 * ====qualifier
 *       A qualifier for DB2 databases running on OS/390 or z/OS servers. For
 * other databases, pass NULL or an empty string.
 *
 * ====schema
 *       The schema which contains the tables. This parameter accepts a search
 * pattern containing _ and % as wildcards.
 *
 * ====table-name
 *       The name of the table. This parameter accepts a search pattern
 * containing _ and % as wildcards.
 *
 * ====table-type
 *       A list of comma-delimited table type identifiers. To match all table
 * types, pass NULL or an empty string.
 *       Valid table type identifiers include: ALIAS, HIERARCHY TABLE,
 * INOPERATIVE VIEW, NICKNAME, MATERIALIZED QUERY TABLE, SYSTEM TABLE, TABLE,
 * TYPED TABLE, TYPED VIEW, and VIEW.
 *
 * ===Return Values
 *
 * Returns a statement resource with a result set containing rows describing
 * the tables that match the specified parameters.
 * The rows are composed of the following columns:
 *
 * Column name:: Description
 * TABLE_CAT:: The catalog that contains the table. The value is NULL if this
 * table does not have catalogs.
 * TABLE_SCHEMA:: Name of the schema that contains the table.
 * TABLE_NAME:: Name of the table.
 * TABLE_TYPE:: Table type identifier for the table.
 * REMARKS:: Description of the table.
 */
static PyObject *ibm_db_tables(PyObject *self, PyObject *args)
{
   SQLCHAR *qualifier = NULL;
   SQLCHAR *owner = NULL;
   SQLCHAR *table_name = NULL;
   SQLCHAR *table_type = NULL;
   PyObject *py_qualifier = NULL;
   PyObject *py_owner = NULL;
   PyObject *py_table_name = NULL;
   PyObject *py_table_type = NULL;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   int rc;

	if (!PyArg_ParseTuple(args, "O|OOOO", &conn_res, &py_qualifier, &py_owner,
                         &py_table_name, &py_table_type))
      return NULL;

   if (py_qualifier != NULL && py_qualifier != Py_None) {
      if (PyString_Check(py_qualifier))
         qualifier = PyString_AsString(py_qualifier);
      else {
         PyErr_SetString(PyExc_Exception, "qualifier must be a string");
         return NULL;
      }
   }
   
   if (py_owner != NULL && py_owner != Py_None) {
      if (PyString_Check(py_owner))
         owner = PyString_AsString(py_owner);
      else {
         PyErr_SetString(PyExc_Exception, "owner must be a string");
         return NULL;
      }
   }

   if (py_table_name != NULL && py_table_name != Py_None) {
      if (PyString_Check(py_table_name))
         table_name = PyString_AsString(py_table_name);
      else {
         PyErr_SetString(PyExc_Exception, "table_name must be a string");
         return NULL;
      }
   }

   if (py_table_type != NULL && py_table_type != Py_None) {
      if (PyString_Check(py_table_type))
         table_type = PyString_AsString(py_table_type);
      else {
         PyErr_SetString(PyExc_Exception, "table type must be a string");
         return NULL;
      }
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if (rc == SQL_ERROR) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLTables((SQLHSTMT)stmt_res->hstmt, qualifier, SQL_NTS, owner,
                     SQL_NTS, table_name, SQL_NTS, table_type, SQL_NTS);
      if (rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      return (PyObject *)stmt_res;
   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.commit
 * ===Description
 * bool ibm_db.commit ( resource connection )
 *
 * Commits an in-progress transaction on the specified connection resource and
 * begins a new transaction.
 * Python applications normally default to AUTOCOMMIT mode, so ibm_db.commit()
 * is not necessary unless AUTOCOMMIT has been turned off for the connection
 * resource.
 *
 * Note: If the specified connection resource is a persistent connection, all
 * transactions in progress for all applications using that persistent
 * connection will be committed. For this reason, persistent connections are
 * not recommended for use in applications that require transactions.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid database connection resource variable as returned from
 * ibm_db.connect() or ibm_db.pconnect().
 *
 * ===Return Values
 *
 * Returns TRUE on success or FALSE on failure.
 */
static PyObject *ibm_db_commit(PyObject *self, PyObject *args)           
{
   conn_handle *conn_res;
   int rc;

   if (!PyArg_ParseTuple(args, "O", &conn_res))
      return NULL;

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      rc = SQLEndTran(SQL_HANDLE_DBC, conn_res->hdbc, SQL_COMMIT);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
         Py_INCREF(Py_False);
			return Py_False;
      } else {
			Py_INCREF(Py_True);
         return Py_True;
      }
   }
   Py_INCREF(Py_False);
	return Py_False;
}

/* static int _python_ibm_db_do_prepare(SQLHANDLE hdbc, char *stmt_string, stmt_handle *stmt_res, PyObject *options)
*/
static int _python_ibm_db_do_prepare(SQLHANDLE hdbc, char *stmt_string, stmt_handle *stmt_res, PyObject *options)
{
   int rc;

   /* alloc handle and return only if it errors */
   rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &(stmt_res->hstmt));
   if ( rc == SQL_ERROR ) {
      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                      1, NULL, -1, 1);
		return rc;
   }

   /* get the string and its length */
   if (NIL_P(stmt_string)) {
      PyErr_SetString(PyExc_Exception, 
                      "Supplied statement parameter is invalid");
		return rc;
   }

   if ( rc < SQL_SUCCESS ) {
      _python_ibm_db_check_sql_errors(hdbc, SQL_HANDLE_DBC, rc, 1, NULL, -1, 1);
      return rc;
   }

   if (!NIL_P(options)) {
      rc = _python_ibm_db_parse_options( options, SQL_HANDLE_STMT, stmt_res );
      if ( rc == SQL_ERROR ) {
			return rc;
      }
   }

   /* Prepare the stmt. The cursor type requested has already been set in 
    * _python_ibm_db_assign_options 
    */
   rc = SQLPrepare((SQLHSTMT)stmt_res->hstmt, stmt_string, 
                   (SQLINTEGER)strlen(stmt_string));
   if ( rc == SQL_ERROR ) {
      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                      1, NULL, -1, 1);
   }
   return rc;
}

/*!# ibm_db.exec
 *
 * ===Description
 * stmt_handle ibm_db.exec ( IBM_DBConnection connection, string statement
 *                               [, array options] )
 *
 * Prepares and executes an SQL statement.
 *
 * If you plan to interpolate Python variables into the SQL statement,
 * understand that this is one of the more common security exposures. Consider
 * calling ibm_db.prepare() to prepare an SQL statement with parameter markers   * for input values. Then you can call ibm_db.execute() to pass in the input
 * values and avoid SQL injection attacks.
 *
 * If you plan to repeatedly issue the same SQL statement with different
 * parameters, consider calling ibm_db.:prepare() and ibm_db.execute() to
 * enable the database server to reuse its access plan and increase the
 * efficiency of your database access.
 *
 * ===Parameters
 *
 * ====connection
 *
 *       A valid database connection resource variable as returned from
 * ibm_db.connect() or ibm_db.pconnect().
 *
 * ====statement
 *
 *       An SQL statement. The statement cannot contain any parameter markers.
 *
 * ====options
 *
 *       An dictionary containing statement options. You can use this parameter  * to request a scrollable cursor on database servers that support this
 * functionality.
 *
 *       SQL_ATTR_CURSOR_TYPE
 *             Passing the SQL_SCROLL_FORWARD_ONLY value requests a forward-only
 *             cursor for this SQL statement. This is the default type of
 *             cursor, and it is supported by all database servers. It is also
 *             much faster than a scrollable cursor.
 *
 *             Passing the SQL_CURSOR_KEYSET_DRIVEN value requests a scrollable  *             cursor for this SQL statement. This type of cursor enables you to
 *             fetch rows non-sequentially from the database server. However, it
 *             is only supported by DB2 servers, and is much slower than
 *             forward-only cursors.
 *
 * ===Return Values
 *
 * Returns a stmt_handle resource if the SQL statement was issued
 * successfully, or FALSE if the database failed to execute the SQL statement.
 */
static PyObject *ibm_db_exec(PyObject *self, PyObject *args)           
{
   PyObject *options = NULL;
   stmt_handle *stmt_res;
   conn_handle *conn_res;
   int rc;
   SQLCHAR *stmt_string = NULL;
   char* return_str = NULL; /* This variable is used by 
                             * _python_ibm_db_check_sql_errors to return err 
                             * strings 
                             */

   /* This function basically is a wrap of the _python_ibm_db_do_prepare and 
    * _python_ibm_db_Execute_stmt 
    * After completing statement execution, it returns the statement resource 
    */
 
	if (!PyArg_ParseTuple(args, "Os|O", &conn_res, &stmt_string, &options))
       return NULL;

   if (NIL_P(stmt_string)) {
      PyErr_SetString(PyExc_Exception, 
                      "Supplied statement parameter is invalid");
		return NULL;
   }

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      return_str = ALLOC_N(char, DB2_MAX_ERR_MSG_LEN);
      if ( return_str == NULL ) {
         PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
         return NULL;
      }

      memset(return_str, 0, DB2_MAX_ERR_MSG_LEN);

      _python_ibm_db_clear_stmt_err_cache();

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      /* Allocates the stmt handle */
      /* returns the stat_handle back to the calling function */
      rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1,
                                         NULL, -1, 1);
         PyMem_Del(return_str);
         return NULL;
      }

      if (!NIL_P(options)) {
         rc = _python_ibm_db_parse_options(options, SQL_HANDLE_STMT, stmt_res);
         if ( rc == SQL_ERROR ) {
            return NULL;
         }
      }

      rc = SQLExecDirect((SQLHSTMT)stmt_res->hstmt, stmt_string, 
                         (SQLINTEGER)strlen(stmt_string));
      if ( rc < SQL_SUCCESS ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, -1, 
                                         1, return_str, DB2_ERRMSG, 
                                         stmt_res->errormsg_recno_tracker);
         SQLFreeHandle( SQL_HANDLE_STMT, stmt_res->hstmt );
			/* TODO: Object freeing */
         /* free(stmt_res); */
         PyMem_Del(return_str);
         return NULL;
      }
      PyMem_Del(return_str);
      return (PyObject *)stmt_res;                 
   }
   return NULL;
}

/*!# ibm_db.free_result
 *
 * ===Description
 * bool ibm_db.free_result ( resource stmt )
 *
 * Frees the system and database resources that are associated with a result
 * set. These resources are freed implicitly when a script finishes, but you
 * can call ibm_db.free_result() to explicitly free the result set resources
 * before the end of the script.
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid statement resource.
 *
 * ===Return Values
 *
 * Returns TRUE on success or FALSE on failure.
 */
static PyObject *ibm_db_free_result(PyObject *self, PyObject *args)
{
   stmt_handle *stmt_res;
   int rc = 0;

   if (!PyArg_ParseTuple(args, "O", &stmt_res))
      return NULL;

   if (!NIL_P(stmt_res)) {
      if ( stmt_res->hstmt ) {
         /* Free any cursors that might have been allocated in a previous call 
          * to SQLExecute 
          */
         rc = SQLFreeHandle( SQL_HANDLE_STMT, stmt_res->hstmt);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         stmt_res->hstmt = 0;
      }
      _python_ibm_db_free_result_struct(stmt_res);
   } else {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
		Py_INCREF(Py_False);
		return Py_False;
   }
	Py_INCREF(Py_True);
   return Py_True;
}

/*!# ibm_db.prepare
 *
 * ===Description
 * IBMDB_Statement ibm_db.prepare ( IBM_DBConnection connection,
 *                                  string statement [, array options] )
 *
 * ibm_db.prepare() creates a prepared SQL statement which can include 0 or
 * more parameter markers (? characters) representing parameters for input,
 * output, or input/output. You can pass parameters to the prepared statement
 * using ibm_db.bind_param(), or for input values only, as an array passed to
 * ibm_db.execute().
 *
 * There are three main advantages to using prepared statements in your
 * application:
 *       * Performance: when you prepare a statement, the database server
 *         creates an optimized access plan for retrieving data with that
 *         statement. Subsequently issuing the prepared statement with
 *         ibm_db.execute() enables the statements to reuse that access plan
 *         and avoids the overhead of dynamically creating a new access plan
 *         for every statement you issue.
 *       * Security: when you prepare a statement, you can include parameter
 *         markers for input values. When you execute a prepared statement
 *         with input values for placeholders, the database server checks each
 *         input value to ensure that the type matches the column definition or
 *         parameter definition.
 *       * Advanced functionality: Parameter markers not only enable you to
 *         pass input values to prepared SQL statements, they also enable you
 *         to retrieve OUT and INOUT parameters from stored procedures using
 *         ibm_db.bind_param().
 *
 * ===Parameters
 * ====connection
 *
 *       A valid database connection resource variable as returned from
 *       ibm_db.connect() or ibm_db.pconnect().
 *
 * ====statement
 *
 *       An SQL statement, optionally containing one or more parameter markers.
 *
 * ====options
 *
 *       An dictionary containing statement options. You can use this parameter
 *       to request a scrollable cursor on database servers that support this
 *       functionality.
 *
 *       SQL_ATTR_CURSOR_TYPE
 *             Passing the SQL_SCROLL_FORWARD_ONLY value requests a forward-only
 *             cursor for this SQL statement. This is the default type of
 *             cursor, and it is supported by all database servers. It is also
 *             much faster than a scrollable cursor.
 *             Passing the SQL_CURSOR_KEYSET_DRIVEN value requests a scrollable  
 *             cursor for this SQL statement. This type of cursor enables you
 *             to fetch rows non-sequentially from the database server. However, 
 *             it is only supported by DB2 servers, and is much slower than
 *             forward-only cursors.
 *
 * ===Return Values
 * Returns a IBM_DBStatement object if the SQL statement was successfully
 * parsed and prepared by the database server. Returns FALSE if the database
 * server returned an error. You can determine which error was returned by
 * calling ibm_db.stmt_error() or ibm_db.stmt_errormsg().
 */					 
static PyObject *ibm_db_prepare(PyObject *self, PyObject *args)            
{
   SQLCHAR *stmt_string = NULL;
   PyObject *options = NULL;
   conn_handle *conn_res;
   stmt_handle *stmt_res;
   int rc;
   char error[DB2_MAX_ERR_MSG_LEN];

   if (!PyArg_ParseTuple(args, "Os|O", &conn_res, &stmt_string, &options))
		return NULL;

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      _python_ibm_db_clear_stmt_err_cache();

      /* Initialize stmt resource members with default values. */
      /* Parsing will update options if needed */

      stmt_res = _ibm_db_new_stmt_struct(conn_res);

      /* Allocates the stmt handle */
      /* Prepares the statement */
      /* returns the stat_handle back to the calling function */
      rc = _python_ibm_db_do_prepare(conn_res->hdbc, stmt_string, stmt_res, 
                                     options);
      if ( rc < SQL_SUCCESS ) {
         sprintf(error, "Statement Prepare Failed: %s", 
                 IBM_DB_G(__python_stmt_err_msg));
         return NULL;
      }
      return (PyObject *)stmt_res;                 
   }

   return NULL;
}

/*   static param_node* build_list( stmt_res, param_no, data_type, precision, scale, nullable )
*/
static param_node* build_list( stmt_handle *stmt_res, int param_no, SQLSMALLINT data_type, SQLUINTEGER precision, SQLSMALLINT scale, SQLSMALLINT nullable )
{
   param_node *tmp_curr = NULL, *curr = stmt_res->head_cache_list, *prev = NULL;

   /* Allocate memory and make new node to be added */
   tmp_curr = ALLOC(param_node);
   memset(tmp_curr,0,sizeof(param_node));
   /* assign values */
   tmp_curr->data_type = data_type;
   tmp_curr->param_size = precision;
   tmp_curr->nullable = nullable;
   tmp_curr->scale = scale;
   tmp_curr->param_num = param_no;
   tmp_curr->file_options = SQL_FILE_READ;
   tmp_curr->param_type = SQL_PARAM_INPUT;

   while ( curr != NULL ) {
      prev = curr;
      curr = curr->next;
   }

   if (stmt_res->head_cache_list == NULL) {
      stmt_res->head_cache_list = tmp_curr;
   } else {
      prev->next = tmp_curr;
   }

   tmp_curr->next = curr;

   return tmp_curr;
}

/*   static int _python_ibm_db_bind_data( stmt_handle *stmt_res, param_node *curr, PyObject *bind_data )
*/
static int _python_ibm_db_bind_data( stmt_handle *stmt_res, param_node *curr, PyObject *bind_data)
{
   int rc;
   SQLSMALLINT valueType;
   SQLPOINTER   paramValuePtr;
   Py_ssize_t buffer_len = 0;

   /* Have to use SQLBindFileToParam if PARAM is type PARAM_FILE */
   if ( curr->param_type == PARAM_FILE) {
      /* Only string types can be bound */
      if (!PyString_Check(bind_data)) {
         return SQL_ERROR;
      }
      curr->bind_indicator = 0;
		curr->svalue = PyString_AsString(bind_data);
		curr->ivalue = strlen(curr->svalue);
      valueType = curr->ivalue;
      /* Bind file name string */
      rc = SQLBindFileToParam((SQLHSTMT)stmt_res->hstmt, curr->param_num,
         curr->data_type, (SQLCHAR*)curr->svalue,
         (SQLSMALLINT*)&(curr->ivalue), &(curr->file_options), 
         curr->ivalue, &(curr->bind_indicator));
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                         rc, 1, NULL, -1, 1);
      }
      return rc;
   }

   switch(TYPE(bind_data)) {
      case PYTHON_FIXNUM:
         curr->ivalue = PyLong_AsLong(bind_data);
         rc = SQLBindParameter(stmt_res->hstmt, curr->param_num,
            curr->param_type, SQL_C_LONG, curr->data_type,
            curr->param_size, curr->scale, &curr->ivalue, 0, NULL);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         curr->data_type = SQL_C_LONG;
         break;

      /* Convert BOOLEAN types to LONG for DB2 / Cloudscape */
      case PYTHON_FALSE:
         curr->ivalue = 0;
         rc = SQLBindParameter(stmt_res->hstmt, curr->param_num,
            curr->param_type, SQL_C_LONG, curr->data_type, curr->param_size,
            curr->scale, &curr->ivalue, 0, NULL);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         curr->data_type = SQL_C_LONG;
         break;

      case PYTHON_TRUE:
         curr->ivalue = 1;
         rc = SQLBindParameter(stmt_res->hstmt, curr->param_num,
            curr->param_type, SQL_C_LONG, curr->data_type, curr->param_size,
            curr->scale, &curr->ivalue, 0, NULL);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         curr->data_type = SQL_C_LONG;
         break;

      case PYTHON_FLOAT:
         curr->fvalue = PyFloat_AsDouble(bind_data);
         rc = SQLBindParameter(stmt_res->hstmt, curr->param_num,
            curr->param_type, SQL_C_DOUBLE, curr->data_type, curr->param_size,
            curr->scale, &curr->fvalue, 0, NULL);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         curr->data_type = SQL_C_DOUBLE;
         break;

      case PYTHON_STRING:
         if (curr->data_type == SQL_BLOB || curr->data_type == SQL_BINARY
                                         || curr->data_type == SQL_VARBINARY ) {
            PyObject_AsReadBuffer(bind_data, &(curr->svalue), &buffer_len);
            curr->ivalue = buffer_len;
         } else {
            curr->svalue = PyString_AsString(bind_data);
            curr->ivalue = strlen(curr->svalue);
         }
         /*
          * An extra parameter is given by the client to pick the size of the 
          * string returned. The string is then truncate past that size.   
          * If no size is given then use BUFSIZ to return the string.
          */
         if (curr->size != 0) {
            curr->ivalue = curr->size;
         }
         curr->svalue = memcpy(ALLOC_N(char, curr->ivalue+1), curr->svalue, 
                               curr->ivalue);

         curr->svalue[curr->ivalue] = '\0';

         switch ( curr->data_type ) {
            case SQL_CLOB:
               if (curr->param_type == SQL_PARAM_OUTPUT || 
                   curr->param_type == SQL_PARAM_INPUT_OUTPUT) {
                  curr->bind_indicator = curr->ivalue;
                  valueType = SQL_C_CHAR;
                  paramValuePtr = (SQLPOINTER)curr->svalue;
               } else {
                  curr->bind_indicator = SQL_DATA_AT_EXEC;
                  valueType = SQL_C_CHAR;
                  /* The correct dataPtr will be set during SQLPutData with 
                   * the len from this struct 
						 */
#ifndef PASE
                  paramValuePtr = (SQLPOINTER)(curr);
#else
                  paramValuePtr = (SQLPOINTER)&(curr);
#endif
               }
               break;

            case SQL_BLOB:
               if (curr->param_type == SQL_PARAM_OUTPUT || 
					    curr->param_type == SQL_PARAM_INPUT_OUTPUT) {
                  curr->bind_indicator = curr->ivalue;
                  valueType = SQL_C_BINARY;
                  paramValuePtr = (SQLPOINTER)curr;
               } else {
                  curr->bind_indicator = SQL_DATA_AT_EXEC;
                  valueType = SQL_C_BINARY;
#ifndef PASE
                  paramValuePtr = (SQLPOINTER)(curr);
#else
                  paramValuePtr = (SQLPOINTER)&(curr);
#endif
               }
               break;

            case SQL_BINARY:
#ifndef PASE /* i5/OS SQL_LONGVARBINARY is SQL_VARBINARY */
            case SQL_LONGVARBINARY:
#endif /* PASE */
            case SQL_VARBINARY:
            case SQL_XML:
               /* account for bin_mode settings as well */
               curr->bind_indicator = curr->ivalue;
               valueType = SQL_C_BINARY;
               paramValuePtr = (SQLPOINTER)curr->svalue;
               break;

            /* This option should handle most other types such as DATE, 
             * VARCHAR etc 
             */
            default:
               valueType = SQL_C_CHAR;
               curr->bind_indicator = curr->ivalue;
               paramValuePtr = (SQLPOINTER)(curr->svalue);
         }
         rc = SQLBindParameter(stmt_res->hstmt, curr->param_num,
            curr->param_type, valueType, curr->data_type, curr->param_size,
            curr->scale, paramValuePtr, curr->ivalue, &(curr->bind_indicator));
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         curr->data_type = valueType;
         break;

      case PYTHON_NIL:
         curr->ivalue = SQL_NULL_DATA;
         rc = SQLBindParameter(stmt_res->hstmt, curr->param_num,
            curr->param_type, SQL_C_DEFAULT, curr->data_type, curr->param_size,
            curr->scale, &curr->ivalue, 0, &curr->ivalue);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         break;

      default:
         return SQL_ERROR;
   }
   return rc;
}

/* static int _python_ibm_db_execute_helper(stmt_res, data, int bind_cmp_list)
   */
static int _python_ibm_db_execute_helper(stmt_handle *stmt_res, PyObject *data, int bind_cmp_list, int bind_params)
{
   int rc=SQL_SUCCESS;
   param_node *curr = NULL;   /* To traverse the list */
   PyObject *bind_data;         /* Data value from symbol table */
   char error[DB2_MAX_ERR_MSG_LEN];

   /* Used in call to SQLDescribeParam if needed */
   SQLSMALLINT param_no;
   SQLSMALLINT data_type;
   SQLUINTEGER precision;
   SQLSMALLINT scale;
   SQLSMALLINT nullable;

   /* This variable means that we bind the complete list of params cached */
   /* The values used are fetched from the active symbol table */
   /* TODO: Enhance this part to check for stmt_res->file_param */
   /* If this flag is set, then use SQLBindParam, else use SQLExtendedBind */
   if ( bind_cmp_list ) {
      /* Bind the complete list sequentially */
      /* Used when no parameters array is passed in */
      curr = stmt_res->head_cache_list;

      while (curr != NULL ) {
         /* Fetch data from symbol table */
			if (curr->param_type == PARAM_FILE)
				bind_data = curr->var_pyvalue;
			else {
				bind_data = curr->var_pyvalue;
			}
			if (bind_data == NULL)
            return -1;
         rc = _python_ibm_db_bind_data( stmt_res, curr, bind_data);
         if ( rc == SQL_ERROR ) {
            sprintf(error, "Binding Error 1: %s", 
                    IBM_DB_G(__python_stmt_err_msg));
            PyErr_SetString(PyExc_Exception, error);
            return rc;
         }
         curr = curr->next;
      }
      return 0;
   } else {
      /* Bind only the data value passed in to the Current Node */
      if ( data != NULL ) {
         if ( bind_params ) {
            /* This condition applies if the parameter has not been
             * bound using ibm_db.bind_param. Need to describe the
             * parameter and then bind it.
             */
            param_no = ++stmt_res->num_params;
            rc = SQLDescribeParam((SQLHSTMT)stmt_res->hstmt, param_no,
               (SQLSMALLINT*)&data_type, &precision, (SQLSMALLINT*)&scale,
               (SQLSMALLINT*)&nullable);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT,
                                               rc, 1, NULL, -1, 1);
               sprintf(error, "Describe Param Failed: %s", 
                       IBM_DB_G(__python_stmt_err_msg));
               PyErr_SetString(PyExc_Exception, error);
               return rc;
            }

            curr = build_list(stmt_res, param_no, data_type, precision, 
                              scale, nullable);
            rc = _python_ibm_db_bind_data( stmt_res, curr, data);
            if ( rc == SQL_ERROR ) {
               sprintf(error, "Binding Error 2: %s", 
                       IBM_DB_G(__python_stmt_err_msg));
               PyErr_SetString(PyExc_Exception, error);
               return rc;
            }
         } else {
            /* This is always at least the head_cache_node -- assigned in
             * ibm_db.execute(), if params have been bound.
             */
            curr = stmt_res->current_node;
            if ( curr != NULL ) {
               rc = _python_ibm_db_bind_data( stmt_res, curr, data);
               if ( rc == SQL_ERROR ) {
                  sprintf(error, "Binding Error 2: %s", 
                          IBM_DB_G(__python_stmt_err_msg));
                  PyErr_SetString(PyExc_Exception, error);
                  return rc;
               }
               stmt_res->current_node = curr->next;
            }
         }
         return rc;
      }
   }
   return rc;
}

/*!# ibm_db.execute
 *
 * ===Description
 * Py_True/Py_False ibm_db.execute ( IBM_DBStatement stmt [, tuple parameters] )
 *
 * ibm_db.execute() executes an SQL statement that was prepared by
 * ibm_db.prepare().
 *
 * If the SQL statement returns a result set, for example, a SELECT statement
 * or a CALL to a stored procedure that returns one or more result sets, you
 * can retrieve a row as an tuple/dictionary from the stmt resource using
 * ibm_db.fetch_assoc(), ibm_db.fetch_both(), or ibm_db.fetch_tuple().
 * Alternatively, you can use ibm_db.fetch_row() to move the result set pointer
 * to the next row and fetch a column at a time from that row with
 * ibm_db.result().
 *
 * Refer to ibm_db.prepare() for a brief discussion of the advantages of using
 * ibm_db.prepare() and ibm_db.execute() rather than ibm_db.exec().
 *
 * ===Parameters
 * ====stmt
 *
 *       A prepared statement returned from ibm_db.prepare().
 *
 * ====parameters
 *
 *       An tuple of input parameters matching any parameter markers contained
 * in the prepared statement.
 *
 * ===Return Values
 *
 * Returns Py_True on success or Py_False on failure.
 */
static PyObject *ibm_db_execute(PyObject *self, PyObject *args)            
{
   PyObject *parameters_tuple = NULL;
   stmt_handle *stmt_res;
   int rc, numOpts, i, bind_params = 0;
   char error[DB2_MAX_ERR_MSG_LEN];
   SQLSMALLINT num;
   SQLPOINTER valuePtr;

   /* This is used to loop over the param cache */
   param_node *prev_ptr, *curr_ptr;

   PyObject *data;

	if (!PyArg_ParseTuple(args, "O|O", &stmt_res, &parameters_tuple))
      return NULL;

  /* Get values from symbol tables 
    * Assign values into param nodes 
    * Check types/conversions
    * Bind parameters 
    * Execute 
    * Return values back to symbol table for OUT params 
    */

   if (!NIL_P(stmt_res)) {
      
      /* Free any cursors that might have been allocated in a previous call to 
       * SQLExecute 
       */
      SQLFreeStmt((SQLHSTMT)stmt_res->hstmt, SQL_CLOSE);

      /* This ensures that each call to ibm_db.execute start from scratch */
      stmt_res->current_node = stmt_res->head_cache_list;

      rc = SQLNumParams((SQLHSTMT)stmt_res->hstmt, (SQLSMALLINT*)&num);

      if ( num != 0 ) {
         /* Parameter Handling */
         if ( !NIL_P(parameters_tuple) ) {
            /* Make sure ibm_db.bind_param has been called */
            /* If the param list is NULL -- ERROR */
            if ( stmt_res->head_cache_list == NULL ) {
               bind_params = 1;
            }

            if (!PyTuple_Check(parameters_tuple)) {
               PyErr_SetString(PyExc_Exception, "Param is not a tuple");
               return NULL;
            }

            numOpts = PyTuple_Size(parameters_tuple);

            if (numOpts > num) {
               /* More are passed in -- Warning - Use the max number present */
               sprintf(error, "%d params bound not matching %d required", 
                       numOpts, num);
               PyErr_SetString(PyExc_Exception, error);
               numOpts = stmt_res->num_params;
            } else if (numOpts < num) {
               /* If there are less params passed in, than are present 
                * -- Error 
                */
               sprintf(error, "%d params bound not matching %d required", 
                       numOpts, num);
               PyErr_SetString(PyExc_Exception, error);
               return NULL;
            }

            for ( i = 0; i < numOpts; i++) {
               /* Bind values from the parameters_tuple to params */
               data = PyTuple_GetItem(parameters_tuple,i);

               /* The 0 denotes that you work only with the current node.
                * The 4th argument specifies whether the data passed in
                * has been described. So we need to call SQLDescribeParam
                * before binding depending on this.
                */
               rc = _python_ibm_db_execute_helper(stmt_res, data, 0, 
                                                  bind_params);
               if ( rc == SQL_ERROR) {
                  sprintf(error, "Binding Error: %s", 
                          IBM_DB_G(__python_stmt_err_msg));
                  PyErr_SetString(PyExc_Exception, error);
                  return NULL;
               }
            }
         } else {
            /* No additional params passed in. Use values already bound. */
            if ( num > stmt_res->num_params ) {
               /* More parameters than we expected */
               sprintf(error, "%d params bound not matching %d required", 
                       stmt_res->num_params, num);
               PyErr_SetString(PyExc_Exception, error);
            } else if ( num < stmt_res->num_params ) {
               /* Fewer parameters than we expected */
               sprintf(error, "%d params bound not matching %d required", 
                       stmt_res->num_params, num);
               PyErr_SetString(PyExc_Exception, error);
               return NULL;
            }

            /* Param cache node list is empty -- No params bound */
            if ( stmt_res->head_cache_list == NULL ) {
               PyErr_SetString(PyExc_Exception, "Parameters not bound");
               return NULL;
            } else {
              /* The 1 denotes that you work with the whole list 
               * And bind sequentially 
               */
               rc = _python_ibm_db_execute_helper(stmt_res, NULL, 1, 0);
               if ( rc == SQL_ERROR ) {
                  sprintf(error, "Binding Error 3: %s", 
                          IBM_DB_G(__python_stmt_err_msg));
                  PyErr_SetString(PyExc_Exception, error);
                  return NULL;
               }
            }
         }
      } else {
         /* No Parameters 
          * We just execute the statement. No additional work needed. 
          */
         rc = SQLExecute((SQLHSTMT)stmt_res->hstmt);
         if ( rc == SQL_ERROR ) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
            sprintf(error, "Statement Execute Failed: %s", 
                    IBM_DB_G(__python_stmt_err_msg));
            PyErr_SetString(PyExc_Exception, error);
            return NULL;
         }
			Py_INCREF(Py_True);
         return Py_True;
      }

      /* Execute Stmt -- All parameters bound */
      rc = SQLExecute((SQLHSTMT)stmt_res->hstmt);
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                         rc, 1, NULL, -1, 1);
         sprintf(error, "Statement Execute Failed: %s", 
                 IBM_DB_G(__python_stmt_err_msg));
         PyErr_SetString(PyExc_Exception, error);
         return NULL;
      }

      if ( rc == SQL_NEED_DATA ) {
         while ( (SQLParamData((SQLHSTMT)stmt_res->hstmt, 
                               (SQLPOINTER *)&valuePtr)) == SQL_NEED_DATA ) {
            /* passing data value for a parameter */
            rc = SQLPutData((SQLHSTMT)stmt_res->hstmt, 
                            (SQLPOINTER)(((param_node*)valuePtr)->svalue), 
                            ((param_node*)valuePtr)->ivalue);
            if ( rc == SQL_ERROR ) {
               _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT,
                                               rc, 1, NULL, -1, 1);
               sprintf(error, "Sending data failed: %s", 
                       IBM_DB_G(__python_stmt_err_msg));
               PyErr_SetString(PyExc_Exception, error);
               return NULL;
            }
         }
      }

      /* cleanup dynamic bindings if present */
      if ( bind_params == 1 ) {
         /* Free param cache list */
         curr_ptr = stmt_res->head_cache_list;
         prev_ptr = stmt_res->head_cache_list;

         while (curr_ptr != NULL) {
            curr_ptr = curr_ptr->next;

            /* Free Values */
            if ( prev_ptr->svalue) {
               PyMem_Del(prev_ptr->svalue);
            }
            PyMem_Del(prev_ptr);
            prev_ptr = curr_ptr;
         }

         stmt_res->head_cache_list = NULL;
         stmt_res->num_params = 0;
      } 

      if ( rc != SQL_ERROR ) {
			Py_INCREF(Py_True);
         return Py_True;
      }
   } else {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
		return NULL;
   }

   return NULL;
}

/*!# ibm_db.conn_errormsg
 *
 * ===Description
 * string ibm_db.conn_errormsg ( [resource connection] )
 *
 * ibm_db.conn_errormsg() returns an error message and SQLCODE value
 * representing the reason the last database connection attempt failed.
 * As ibm_db.connect() returns FALSE in the event of a failed connection
 * attempt, do not pass any parameters to ibm_db.conn_errormsg() to retrieve
 * the associated error message and SQLCODE value.
 *
 * If, however, the connection was successful but becomes invalid over time,
 * you can pass the connection parameter to retrieve the associated error
 * message and SQLCODE value for a specific connection.
 * ===Parameters
 *
 * ====connection
 *       A connection resource associated with a connection that initially
 * succeeded, but which over time became invalid.
 *
 * ===Return Values
 *
 * Returns a string containing the error message and SQLCODE value resulting
 * from a failed connection attempt. If there is no error associated with the
 * last connection attempt, ibm_db.conn_errormsg() returns an empty string.
 */
static PyObject *ibm_db_conn_errormsg(PyObject *self, PyObject *args)
{
   conn_handle *conn_res = NULL;
   char* return_str = NULL;   /* This variable is used by 
                               * _python_ibm_db_check_sql_errors to return err 
                               * strings 
                               */

	if (!PyArg_ParseTuple(args, "|O", &conn_res))
      return NULL;

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
      }

      return_str = ALLOC_N(char, SQL_SQLSTATE_SIZE + 1);


      _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, -1, 0, 
                                      return_str, DB2_ERRMSG, 
                                      conn_res->errormsg_recno_tracker);
      if(conn_res->errormsg_recno_tracker - conn_res->error_recno_tracker >= 1)
         conn_res->error_recno_tracker = conn_res->errormsg_recno_tracker;
      conn_res->errormsg_recno_tracker++;

      return PyString_FromString(return_str);
   } else {
      return PyString_FromString(IBM_DB_G(__python_conn_err_msg));
   }
}

/*!# ibm_db.stmt_errormsg
 *
 * ===Description
 * string ibm_db.stmt_errormsg ( [resource stmt] )
 *
 * Returns a string containing the last SQL statement error message.
 *
 * If you do not pass a statement resource as an argument to
 * ibm_db.stmt_errormsg(), the driver returns the error message associated with
 * the last attempt to return a statement resource, for example, from
 * ibm_db.prepare() or ibm_db.exec().
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid statement resource.
 *
 * ===Return Values
 *
 * Returns a string containing the error message and SQLCODE value for the last
 * error that occurred issuing an SQL statement.
 */
static PyObject *ibm_db_stmt_errormsg(PyObject *self, PyObject *args)
{
   stmt_handle *stmt_res = NULL;
   char* return_str = NULL; /* This variable is used by 
                             * _python_ibm_db_check_sql_errors to return err 
                             * strings 
                             */

   if (!PyArg_ParseTuple(args, "|O", &stmt_res))
      return NULL;

   if (!NIL_P(stmt_res)) {

      return_str = ALLOC_N(char, DB2_MAX_ERR_MSG_LEN);

      memset(return_str, 0, DB2_MAX_ERR_MSG_LEN);

      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, -1, 0, 
                                      return_str, DB2_ERRMSG, 
                                      stmt_res->errormsg_recno_tracker);
      if(stmt_res->errormsg_recno_tracker - stmt_res->error_recno_tracker >= 1)
         stmt_res->error_recno_tracker = stmt_res->errormsg_recno_tracker;
      stmt_res->errormsg_recno_tracker++;

      return PyString_FromString(return_str);
   } else {
      return PyString_FromString(IBM_DB_G(__python_stmt_err_msg));
   }
}

/*!# ibm_db.conn_error
 * ===Description
 * string ibm_db.conn_error ( [resource connection] )
 *
 * ibm_db.conn_error() returns an SQLSTATE value representing the reason the
 * last attempt to connect to a database failed. As ibm_db.connect() returns
 * FALSE in the event of a failed connection attempt, you do not pass any
 * parameters to ibm_db.conn_error() to retrieve the SQLSTATE value.
 *
 * If, however, the connection was successful but becomes invalid over time, you
 * can pass the connection parameter to retrieve the SQLSTATE value for a
 * specific connection.
 *
 * To learn what the SQLSTATE value means, you can issue the following command
 * at a DB2 Command Line Processor prompt: db2 '? sqlstate-value'. You can also
 * call ibm_db.conn_errormsg() to retrieve an explicit error message and the
 * associated SQLCODE value.
 *
 * ===Parameters
 *
 * ====connection
 *       A connection resource associated with a connection that initially
 * succeeded, but which over time became invalid.
 *
 * ===Return Values
 *
 * Returns the SQLSTATE value resulting from a failed connection attempt.
 * Returns an empty string if there is no error associated with the last
 * connection attempt.
 */
static PyObject *ibm_db_conn_error(PyObject *self, PyObject *args)           
{
   conn_handle *conn_res = NULL;

   char *return_str = NULL; /* This variable is used by 
                             * _python_ibm_db_check_sql_errors to return err 
                             * strings */

   if (!PyArg_ParseTuple(args, "|O", &conn_res))
		return NULL;

   if (!NIL_P(conn_res)) {
      return_str = ALLOC_N(char, SQL_SQLSTATE_SIZE + 1);

      memset(return_str, 0, SQL_SQLSTATE_SIZE + 1);

      _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, -1, 0, 
                                      return_str, DB2_ERR, 
                                      conn_res->error_recno_tracker);
      if (conn_res->error_recno_tracker-conn_res->errormsg_recno_tracker >= 1) {
         conn_res->errormsg_recno_tracker = conn_res->error_recno_tracker;
      }
      conn_res->error_recno_tracker++;

      return PyString_FromString(return_str);
   } else {
      return PyString_FromString(IBM_DB_G(__python_conn_err_state));
   }
}

/*!# ibm_db.stmt_error
 *
 * ===Description
 * string ibm_db.stmt_error ( [resource stmt] )
 *
 * Returns a string containing the SQLSTATE value returned by an SQL statement.
 *
 * If you do not pass a statement resource as an argument to
 * ibm_db.stmt_error(), the driver returns the SQLSTATE value associated with
 * the last attempt to return a statement resource, for example, from
 * ibm_db.prepare() or ibm_db.exec().
 *
 * To learn what the SQLSTATE value means, you can issue the following command
 * at a DB2 Command Line Processor prompt: db2 '? sqlstate-value'. You can also
 * call ibm_db.stmt_errormsg() to retrieve an explicit error message and the
 * associated SQLCODE value.
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid statement resource.
 *
 * ===Return Values
 *
 * Returns a string containing an SQLSTATE value.
 */
static PyObject *ibm_db_stmt_error(PyObject *self, PyObject *args)
{
   stmt_handle *stmt_res = NULL;
   char* return_str = NULL; /* This variable is used by 
                             * _python_ibm_db_check_sql_errors to return err 
                             * strings 
                             */

	if (!PyArg_ParseTuple(args, "|O", &stmt_res))
      return NULL;

   if (!NIL_P(stmt_res)) {
      return_str = ALLOC_N(char, DB2_MAX_ERR_MSG_LEN);

      memset(return_str, 0, DB2_MAX_ERR_MSG_LEN);

      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, -1, 0, 
                                      return_str, DB2_ERR, 
                                      stmt_res->error_recno_tracker);

      if (stmt_res->error_recno_tracker-stmt_res->errormsg_recno_tracker >= 1) {
         stmt_res->errormsg_recno_tracker = stmt_res->error_recno_tracker;
      }
      stmt_res->error_recno_tracker++;

      return PyString_FromString(return_str);
   } else {
      return PyString_FromString(IBM_DB_G(__python_stmt_err_state));
   }
}

/*!# ibm_db.next_result
 *
 * ===Description
 * resource ibm_db.next_result ( resource stmt )
 *
 * Requests the next result set from a stored procedure.
 *
 * A stored procedure can return zero or more result sets. While you handle the
 * first result set in exactly the same way you would handle the results
 * returned by a simple SELECT statement, to fetch the second and subsequent
 * result sets from a stored procedure you must call the ibm_db.next_result()
 * function and return the result to a uniquely named Python variable.
 *
 * ===Parameters
 * ====stmt
 *       A prepared statement returned from ibm_db.exec() or ibm_db.execute().
 *
 * ===Return Values
 *
 * Returns a new statement resource containing the next result set if the stored
 * procedure returned another result set. Returns FALSE if the stored procedure
 * did not return another result set.
 */
static PyObject *ibm_db_next_result(PyObject *self, PyObject *args)           
{
   stmt_handle *stmt_res, *new_stmt_res=NULL;
   int rc = 0;
   SQLHANDLE new_hstmt;

	if (!PyArg_ParseTuple(args, "O", &stmt_res))
      return NULL;

   if (!NIL_P(stmt_res)) {
      _python_ibm_db_clear_stmt_err_cache();

      /* alloc handle and return only if it errors */
      rc = SQLAllocHandle(SQL_HANDLE_STMT, stmt_res->hdbc, &new_hstmt);
      if ( rc < SQL_SUCCESS ) {
         _python_ibm_db_check_sql_errors(stmt_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      }
      rc = SQLNextResult((SQLHSTMT)stmt_res->hstmt, (SQLHSTMT)new_hstmt);
      if( rc != SQL_SUCCESS ) {
         if(rc < SQL_SUCCESS) {
            _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, 
                                            rc, 1, NULL, -1, 1);
         }
         SQLFreeHandle(SQL_HANDLE_STMT, new_hstmt);
			Py_INCREF(Py_False);
         return Py_False;
      }

      /* Initialize stmt resource members with default values. */
      /* Parsing will update options if needed */
      new_stmt_res = PyObject_NEW(stmt_handle, &stmt_handleType);
      new_stmt_res->s_bin_mode = stmt_res->s_bin_mode;
      new_stmt_res->cursor_type = stmt_res->cursor_type;
      new_stmt_res->s_case_mode = stmt_res->s_case_mode;
      new_stmt_res->head_cache_list = NULL;
      new_stmt_res->current_node = NULL;
      new_stmt_res->num_params = 0;
      new_stmt_res->file_param = 0;
      new_stmt_res->column_info = NULL;
      new_stmt_res->num_columns = 0;
      new_stmt_res->row_data = NULL;
      new_stmt_res->hstmt = new_hstmt;
      new_stmt_res->hdbc = stmt_res->hdbc;

      return (PyObject *)new_stmt_res;       
   } else {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
      return NULL;
   }
}

/*!# ibm_db.num_fields
 *
 * ===Description
 * int ibm_db.num_fields ( resource stmt )
 *
 * Returns the number of fields contained in a result set. This is most useful
 * for handling the result sets returned by dynamically generated queries, or
 * for result sets returned by stored procedures, where your application cannot
 * otherwise know how to retrieve and use the results.
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid statement resource containing a result set.
 *
 * ===Return Values
 *
 * Returns an integer value representing the number of fields in the result set
 * associated with the specified statement resource. Returns FALSE if the
 * statement resource is not a valid input value.
 */
static PyObject *ibm_db_num_fields(PyObject *self, PyObject *args)           
{
   stmt_handle *stmt_res;
   int rc = 0;
   SQLSMALLINT indx = 0;
   char error[DB2_MAX_ERR_MSG_LEN];

   if (!PyArg_ParseTuple(args, "O", &stmt_res))
      return NULL;

   if (!NIL_P(stmt_res)) {

      rc = SQLNumResultCols((SQLHSTMT)stmt_res->hstmt, &indx);
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
         sprintf(error, "SQLNumResultCols failed: %s", 
                 IBM_DB_G(__python_stmt_err_msg));
         PyErr_SetString(PyExc_Exception, error);
			Py_INCREF(Py_False);
         return Py_False;
      }
      return PyInt_FromLong(indx);
   } else {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
		return NULL;
   }
	Py_INCREF(Py_False);
   return Py_False;
}

/*!# ibm_db.num_rows
 *
 * ===Description
 * int ibm_db.num_rows ( resource stmt )
 *
 * Returns the number of rows deleted, inserted, or updated by an SQL statement.
 *
 * To determine the number of rows that will be returned by a SELECT statement,
 * issue SELECT COUNT(*) with the same predicates as your intended SELECT
 * statement and retrieve the value. If your application logic checks the number
 * of rows returned by a SELECT statement and branches if the number of rows is
 * 0, consider modifying your application to attempt to return the first row
 * with one of ibm_db.fetch_assoc(), ibm_db.fetch_both(), ibm_db.fetch_array(),
 * or ibm_db.fetch_row(), and branch if the fetch function returns FALSE.
 *
 * Note: If you issue a SELECT statement using a scrollable cursor,
 * ibm_db.num_rows() returns the number of rows returned by the SELECT
 * statement. However, the overhead associated with scrollable cursors
 * significantly degrades the performance of your application, so if this is the
 * only reason you are considering using scrollable cursors, you should use a
 * forward-only cursor and either call SELECT COUNT(*) or rely on the boolean
 * return value of the fetch functions to achieve the equivalent functionality
 * with much better performance.
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid stmt resource containing a result set.
 *
 * ===Return Values
 *
 * Returns the number of rows affected by the last SQL statement issued by the
 * specified statement handle.
 */
static PyObject *ibm_db_num_rows(PyObject *self, PyObject *args)
{
   stmt_handle *stmt_res;
   int rc = 0;
   SQLINTEGER count = 0;
   char error[DB2_MAX_ERR_MSG_LEN];

   if (!PyArg_ParseTuple(args, "O", &stmt_res))
      return NULL;

   if (!NIL_P(stmt_res)) {
      rc = SQLRowCount((SQLHSTMT)stmt_res->hstmt, &count);
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 
                                         1, NULL, -1, 1);
         sprintf(error, "SQLRowCount failed: %s", 
                 IBM_DB_G(__python_stmt_err_msg));
         PyErr_SetString(PyExc_Exception, error);
         Py_INCREF(Py_False);
			return Py_False;
      }
      return PyInt_FromLong(count);
   } else {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
		return NULL;
   }
	Py_INCREF(Py_False);
   return Py_False;
}

/* static int _python_ibm_db_get_column_by_name(stmt_handle *stmt_res, char *col_name, int col)
 */
static int _python_ibm_db_get_column_by_name(stmt_handle *stmt_res, char *col_name, int col)
{
   int i;
   /* get column header info */
   if ( stmt_res->column_info == NULL ) {
      if (_python_ibm_db_get_result_set_info(stmt_res)<0) {
         return -1;
      }
   }
   if ( col_name == NULL ) {
      if ( col >= 0 && col < stmt_res->num_columns) {
         return col;
      } else {
         return -1;
      }
   }
   /* should start from 0 */
   i=0;
   while (i < stmt_res->num_columns) {
      if (strcmp((char*)stmt_res->column_info[i].name,col_name) == 0) {
         return i;
      }
      i++;
   }
   return -1;
}

/*!# ibm_db.field_name
 *
 * ===Description
 * string ibm_db.field_name ( resource stmt, mixed column )
 *
 * Returns the name of the specified column in the result set.
 *
 * ===Parameters
 *
 * ====stmt
 *       Specifies a statement resource containing a result set.
 *
 * ====column
 *       Specifies the column in the result set. This can either be an integer
 * representing the 0-indexed position of the column, or a string containing the
 * name of the column.
 *
 * ===Return Values
 *
 * Returns a string containing the name of the specified column. If the
 * specified column does not exist in the result set, ibm_db.field_name()
 * returns FALSE.
 */
static PyObject *ibm_db_field_name(PyObject *self, PyObject *args)           
{
   PyObject *column = NULL;
   stmt_handle* stmt_res = NULL;
   char *col_name = NULL;
   int col = -1;

   if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
      return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }
   if ( TYPE(column)==PYTHON_FIXNUM ) {
      col = PyInt_AsLong(column);
   } else if (column != Py_None) {
      col_name = PyString_AsString(column);
   }
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   return PyString_FromString((char*)stmt_res->column_info[col].name);
}

/*!# ibm_db.field_display_size
 *
 * ===Description
 * int ibm_db.field_display_size ( resource stmt, mixed column )
 *
 * Returns the maximum number of bytes required to display a column in a result
 * set.
 *
 * ===Parameters
 * ====stmt
 *       Specifies a statement resource containing a result set.
 *
 * ====column
 *       Specifies the column in the result set. This can either be an integer
 * representing the 0-indexed position of the column, or a string containing the
 * name of the column.
 *
 * ===Return Values
 *
 * Returns an integer value with the maximum number of bytes required to display
 * the specified column.
 * If the column does not exist in the result set, ibm_db.field_display_size()
 * returns FALSE.
 */
static PyObject *ibm_db_field_display_size(PyObject *self, PyObject *args) 
{
   PyObject *column = NULL;
   int col =- 1;
   char *col_name = NULL;
   stmt_handle *stmt_res = NULL;
   int rc;
   SQLINTEGER colDataDisplaySize;

	if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
		return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
		Py_INCREF(Py_False);
		return Py_False;
   }
   if ( TYPE(column)==PYTHON_FIXNUM ) {
      col = PyInt_AsLong(column);
   } else if (column != Py_None) {
      col_name = PyString_AsString(column);
   }
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   rc = SQLColAttributes((SQLHSTMT)stmt_res->hstmt,(SQLSMALLINT)col+1,
         SQL_DESC_DISPLAY_SIZE,NULL,0, NULL,&colDataDisplaySize);
   if ( rc < SQL_SUCCESS ) {
      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 1, 
                                      NULL, -1, 1);
		Py_INCREF(Py_False);
      return Py_False;
   }
   return PyInt_FromLong(colDataDisplaySize);
}

/*!# ibm_db.field_num
 *
 * ===Description
 * int ibm_db.field_num ( resource stmt, mixed column )
 *
 * Returns the position of the named column in a result set.
 *
 * ===Parameters
 *
 * ====stmt
 *       Specifies a statement resource containing a result set.
 *
 * ====column
 *       Specifies the column in the result set. This can either be an integer
 * representing the 0-indexed position of the column, or a string containing the
 * name of the column.
 *
 * ===Return Values
 *
 * Returns an integer containing the 0-indexed position of the named column in
 * the result set. If the specified column does not exist in the result set,
 * ibm_db.field_num() returns FALSE.
 */
static PyObject *ibm_db_field_num(PyObject *self, PyObject *args)           
{
   PyObject *column = NULL;
   stmt_handle* stmt_res = NULL;
   char *col_name = NULL;
   int col = -1;

   if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
      return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }
   if ( TYPE(column)==PYTHON_FIXNUM ) {
      col = PyInt_AsLong(column);
   } else if (column != Py_None) {
      col_name = PyString_AsString(column);
   }
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   return PyInt_FromLong(col);
}

/*!# ibm_db.field_precision
 *
 * ===Description
 * int ibm_db.field_precision ( resource stmt, mixed column )
 *
 * Returns the precision of the indicated column in a result set.
 *
 * ===Parameters
 *
 * ====stmt
 *       Specifies a statement resource containing a result set.
 *
 * ====column
 *       Specifies the column in the result set. This can either be an integer
 * representing the 0-indexed position of the column, or a string containing the
 * name of the column.
 *
 * ===Return Values
 *
 * Returns an integer containing the precision of the specified column. If the
 * specified column does not exist in the result set, ibm_db.field_precision()
 * returns FALSE.
 */
static PyObject *ibm_db_field_precision(PyObject *self, PyObject *args)
{
   PyObject *column = NULL;
   stmt_handle* stmt_res = NULL;
   char *col_name = NULL;
   int col = -1;

   if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
      return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }
   if ( TYPE(column)==PYTHON_FIXNUM ) {
      col = PyInt_AsLong(column);
   } else if (column != Py_None) {
      col_name = PyString_AsString(column);
   }
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   return PyInt_FromLong(stmt_res->column_info[col].size);

}

/*!# ibm_db.field_scale
 *
 * ===Description
 * int ibm_db.field_scale ( resource stmt, mixed column )
 *
 * Returns the scale of the indicated column in a result set.
 *
 * ===Parameters
 * ====stmt
 *       Specifies a statement resource containing a result set.
 *
 * ====column
 *       Specifies the column in the result set. This can either be an integer
 * representing the 0-indexed position of the column, or a string containing the
 * name of the column.
 *
 * ===Return Values
 *
 * Returns an integer containing the scale of the specified column. If the
 * specified column does not exist in the result set, ibm_db.field_scale()
 * returns FALSE.
 */
static PyObject *ibm_db_field_scale(PyObject *self, PyObject *args)
{
   PyObject *column = NULL;
   stmt_handle* stmt_res = NULL;
   char *col_name = NULL;
   int col = -1;

   if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
      return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }
   if ( TYPE(column)==PYTHON_FIXNUM ) {
      col = PyInt_AsLong(column);
   } else if (column != Py_None) {
      col_name = PyString_AsString(column);
   }
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   return PyInt_FromLong(stmt_res->column_info[col].scale);
}

/*!# ibm_db.field_type
 *
 * ===Description
 * string ibm_db.field_type ( resource stmt, mixed column )
 *
 * Returns the data type of the indicated column in a result set.
 *
 * ===Parameters
 * ====stmt
 *       Specifies a statement resource containing a result set.
 *
 * ====column
 *       Specifies the column in the result set. This can either be an integer
 * representing the 0-indexed position of the column, or a string containing the
 * name of the column.
 *
 * ====Return Values
 *
 * Returns a string containing the defined data type of the specified column.
 * If the specified column does not exist in the result set, ibm_db.field_type()
 * returns FALSE.
 */
static PyObject *ibm_db_field_type(PyObject *self, PyObject *args)
{
   PyObject *column = NULL;
   stmt_handle* stmt_res = NULL;
   char *col_name = NULL;
   char *str_val = NULL;
   int col = -1;

   if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
      return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }
   if ( TYPE(column)==PYTHON_FIXNUM ) {
      col = PyInt_AsLong(column);
   } else if (TYPE(column) == PYTHON_STRING) {
      col_name = PyString_AsString(column);
   } else {
		/* Column argument has to be either an integer or string */
		Py_INCREF(Py_False);
		return Py_False;
	}
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   switch (stmt_res->column_info[col].type) {
      case SQL_SMALLINT:
      case SQL_INTEGER:
      case SQL_BIGINT:
         str_val = "int";
         break;
      case SQL_REAL:
      case SQL_FLOAT:
      case SQL_DOUBLE:
      case SQL_DECIMAL:
      case SQL_NUMERIC:
         str_val = "real";
         break;
      case SQL_CLOB:
         str_val = "clob";
         break;
      case SQL_BLOB:
         str_val = "blob";
         break;
      case SQL_XML:
         str_val = "xml";
         break;
      case SQL_TYPE_DATE:
         str_val = "date";
         break;
      case SQL_TYPE_TIME:
         str_val = "time";
         break;
      case SQL_TYPE_TIMESTAMP:
         str_val = "timestamp";
         break;
      default:
         str_val = "string";
         break;
   }
   return PyString_FromString(str_val);
}

/*!# ibm_db.field_width
 *
 * ===Description
 * int ibm_db.field_width ( resource stmt, mixed column )
 *
 * Returns the width of the current value of the indicated column in a result
 * set. This is the maximum width of the column for a fixed-length data type, or
 * the actual width of the column for a variable-length data type.
 *
 * ===Parameters
 *
 * ====stmt
 *       Specifies a statement resource containing a result set.
 *
 * ====column
 *       Specifies the column in the result set. This can either be an integer
 * representing the 0-indexed position of the column, or a string containing the
 * name of the column.
 *
 * ===Return Values
 *
 * Returns an integer containing the width of the specified character or binary
 * data type column in a result set. If the specified column does not exist in
 * the result set, ibm_db.field_width() returns FALSE.
 */
static PyObject *ibm_db_field_width(PyObject *self, PyObject *args)
{
   PyObject *column = NULL;
   int col=-1;
   char *col_name = NULL;
   stmt_handle *stmt_res = NULL;
   int rc;
   SQLINTEGER colDataSize;

   if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
      return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }
   if ( TYPE(column)==PYTHON_FIXNUM ) {
      col = PyInt_AsLong(column);
   } else if (column != Py_None) {
      col_name = PyString_AsString(column);
   }
   col = _python_ibm_db_get_column_by_name(stmt_res,col_name, col);
   if ( col < 0 ) {
		Py_INCREF(Py_False);
      return Py_False;
   }
   rc = SQLColAttributes((SQLHSTMT)stmt_res->hstmt,(SQLSMALLINT)col+1,
                         SQL_DESC_LENGTH,NULL,0, NULL,&colDataSize);
   if ( rc != SQL_SUCCESS ) {
      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 1, 
                                      NULL, -1, 1);
		Py_INCREF(Py_False);
      return Py_False;
   }
   return PyInt_FromLong(colDataSize);
}

/*!# ibm_db.cursor_type
 *
 * ===Description
 * int ibm_db.cursor_type ( resource stmt )
 *
 * Returns the cursor type used by a statement resource. Use this to determine
 * if you are working with a forward-only cursor or scrollable cursor.
 *
 * ===Parameters
 * ====stmt
 *       A valid statement resource.
 *
 * ===Return Values
 *
 * Returns either SQL_SCROLL_FORWARD_ONLY if the statement resource uses a
 * forward-only cursor or SQL_CURSOR_KEYSET_DRIVEN if the statement resource
 * uses a scrollable cursor.
 */
static PyObject *ibm_db_cursor_type(PyObject *self, PyObject *args)           
{
   stmt_handle *stmt_res = NULL;

   if (!PyArg_ParseTuple(args, "O", &stmt_res))
		return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }

   return PyInt_FromLong(stmt_res->cursor_type != SQL_SCROLL_FORWARD_ONLY);
}

/*!# ibm_db.rollback
 *
 * ===Description
 * bool ibm_db.rollback ( resource connection )
 *
 * Rolls back an in-progress transaction on the specified connection resource
 * and begins a new transaction. Python applications normally default to
 * AUTOCOMMIT mode, so ibm_db.rollback() normally has no effect unless
 * AUTOCOMMIT has been turned off for the connection resource.
 *
 * Note: If the specified connection resource is a persistent connection, all
 * transactions in progress for all applications using that persistent
 * connection will be rolled back. For this reason, persistent connections are
 * not recommended for use in applications that require transactions.
 *
 * ===Parameters
 *
 * ====connection
 *       A valid database connection resource variable as returned from
 * ibm_db.connect() or ibm_db.pconnect().
 *
 * ===Return Values
 *
 * Returns TRUE on success or FALSE on failure.
 */
static PyObject *ibm_db_rollback(PyObject *self, PyObject *args)
{
   conn_handle *conn_res;
   int rc;

	if (!PyArg_ParseTuple(args, "O", &conn_res))
      return NULL;

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      rc = SQLEndTran(SQL_HANDLE_DBC, conn_res->hdbc, SQL_ROLLBACK);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			Py_INCREF(Py_True);
         return Py_True;
      }
   }
	Py_INCREF(Py_False);
   return Py_False;
}

/*!# ibm_db.free_stmt
 *
 * ===Description
 * bool ibm_db.free_stmt ( resource stmt )
 *
 * Frees the system and database resources that are associated with a statement
 * resource. These resources are freed implicitly when a script finishes, but
 * you can call ibm_db.free_stmt() to explicitly free the statement resources
 * before the end of the script.
 *
 * ===Parameters
 * ====stmt
 *       A valid statement resource.
 *
 * ===Return Values
 *
 * Returns TRUE on success or FALSE on failure.
 *
 * DEPRECATED
 */
static PyObject *ibm_db_free_stmt(PyObject *self, PyObject *args)
{
   Py_INCREF(Py_None);
   return Py_None;
}

/*   static RETCODE _python_ibm_db_get_data(stmt_handle *stmt_res, int col_num, short ctype, void *buff, int in_length, SQLINTEGER *out_length) */
static RETCODE _python_ibm_db_get_data(stmt_handle *stmt_res, int col_num, short ctype, void *buff, int in_length, SQLINTEGER *out_length)
{
   RETCODE rc=SQL_SUCCESS;

   rc = SQLGetData((SQLHSTMT)stmt_res->hstmt, col_num, ctype, buff, in_length, 
                   out_length);
   if ( rc == SQL_ERROR ) {
      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 1, 
                                      NULL, -1, 1);
   }
   return rc;
}

/* {{{ static RETCODE _python_ibm_db_get_length(stmt_handle* stmt_res, SQLUSMALLINT col_num, SQLINTEGER *sLength) */
static RETCODE _python_ibm_db_get_length(stmt_handle* stmt_res, SQLUSMALLINT col_num, SQLINTEGER *sLength)
{
   RETCODE rc=SQL_SUCCESS;
   SQLHANDLE new_hstmt;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, stmt_res->hdbc, &new_hstmt);
   if ( rc < SQL_SUCCESS ) {
      _python_ibm_db_check_sql_errors(stmt_res->hdbc, SQL_HANDLE_STMT, rc, 1, 
                                      NULL, -1, 1);
      return SQL_ERROR;
   }

   rc = SQLGetLength((SQLHSTMT)new_hstmt, 
                     stmt_res->column_info[col_num-1].loc_type,
                     stmt_res->column_info[col_num-1].lob_loc, sLength,
                     &stmt_res->column_info[col_num-1].loc_ind);
   if ( rc == SQL_ERROR ) {
      _python_ibm_db_check_sql_errors((SQLHSTMT)new_hstmt, SQL_HANDLE_STMT, rc,
                                      1, NULL, -1, 1);
   }

   SQLFreeHandle(SQL_HANDLE_STMT, new_hstmt);

   return rc;
}
       
/* {{{ static RETCODE _python_ibm_db_get_data2(stmt_handle *stmt_res, int col_num, short ctype, void *buff, int in_length, SQLINTEGER *out_length) */
static RETCODE _python_ibm_db_get_data2(stmt_handle *stmt_res, SQLUSMALLINT col_num, SQLSMALLINT ctype, SQLPOINTER buff, SQLLEN in_length, SQLINTEGER *out_length)
{
   RETCODE rc=SQL_SUCCESS;
   SQLHANDLE new_hstmt;
   SQLSMALLINT targetCType = ctype;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, stmt_res->hdbc, &new_hstmt);
   if ( rc < SQL_SUCCESS ) {
      return SQL_ERROR;
   }

   rc = SQLGetSubString((SQLHSTMT)new_hstmt, 
                        stmt_res->column_info[col_num-1].loc_type,
                        stmt_res->column_info[col_num-1].lob_loc, 1, in_length,
                        targetCType, buff, in_length, out_length, 
                        &stmt_res->column_info[col_num-1].loc_ind);
   if ( rc == SQL_ERROR ) {
      _python_ibm_db_check_sql_errors((SQLHSTMT)new_hstmt, SQL_HANDLE_STMT, rc,
                                      1, NULL, -1, 1);
   }

   SQLFreeHandle(SQL_HANDLE_STMT, new_hstmt);

   return rc;
}

/*!# ibm_db.result
 *
 * ===Description
 * mixed ibm_db.result ( resource stmt, mixed column )
 *
 * Returns a single column from a row in the result set
 *
 * Use ibm_db.result() to return the value of a specified column in the current  * row of a result set. You must call ibm_db.fetch_row() before calling
 * ibm_db.result() to set the location of the result set pointer.
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid stmt resource.
 *
 * ====column
 *       Either an integer mapping to the 0-indexed field in the result set, or  * a string matching the name of the column.
 *
 * ===Return Values
 *
 * Returns the value of the requested field if the field exists in the result
 * set. Returns NULL if the field does not exist, and issues a warning.
 */
static PyObject *ibm_db_result(PyObject *self, PyObject *args)
{
   PyObject *column = NULL;
   stmt_handle *stmt_res;
   long col_num;
   RETCODE rc;
   void   *out_ptr;
   char   *out_char_ptr;
   char error[DB2_MAX_ERR_MSG_LEN];
   SQLINTEGER in_length, out_length=-10; /* Initialize out_length to some 
                                          * meaningless value
                                          */
   SQLSMALLINT column_type, lob_bind_type= SQL_C_BINARY;
   double double_val;
   SQLINTEGER long_val;
   PyObject *return_value = NULL;

	if (!PyArg_ParseTuple(args, "OO", &stmt_res, &column))
      return NULL;

   if (!NIL_P(stmt_res)) {

      if(TYPE(column) == PYTHON_STRING) {
         col_num = _python_ibm_db_get_column_by_name(stmt_res, 
                                                     PyString_AsString(column),          															 -1);
      } else {
         col_num = PyLong_AsLong(column);
      }

      /* get column header info */
      if ( stmt_res->column_info == NULL ) {
         if (_python_ibm_db_get_result_set_info(stmt_res)<0) {
            sprintf(error, "Column information cannot be retrieved: %s", 
                    IBM_DB_G(__python_stmt_err_msg));
            PyErr_SetString(PyExc_Exception, error);
            return Py_False;
         }
      }

      if(col_num < 0 || col_num >= stmt_res->num_columns) {
         PyErr_SetString(PyExc_Exception, "Column ordinal out of range");
      }

      /* get the data */
      column_type = stmt_res->column_info[col_num].type;
      switch(column_type) {
         case SQL_CHAR:
         case SQL_VARCHAR:
#ifndef PASE /* i5/OS SQL_LONGVARCHAR is SQL_VARCHAR */
         case SQL_LONGVARCHAR:
#endif /* PASE */
         case SQL_TYPE_DATE:
         case SQL_TYPE_TIME:
         case SQL_TYPE_TIMESTAMP:
         case SQL_BIGINT:
         case SQL_DECIMAL:
         case SQL_NUMERIC:
            if (column_type == SQL_DECIMAL || column_type == SQL_NUMERIC)
               in_length = stmt_res->column_info[col_num].size + 
                           stmt_res->column_info[col_num].scale + 2 + 1;
            else
               in_length = stmt_res->column_info[col_num].size+1;
            out_ptr = (SQLPOINTER)ALLOC_N(char,in_length);
            if ( out_ptr == NULL ) {
               PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
               return Py_False;
            }
            rc = _python_ibm_db_get_data(stmt_res, col_num+1, SQL_C_CHAR, 
                                         out_ptr, in_length, &out_length);
            if ( rc == SQL_ERROR ) {
               return Py_False;
            }
            if (out_length == SQL_NULL_DATA) {
               PyMem_Del(out_ptr);
               return NULL;
            } else {
               return_value = PyString_FromString((char*)out_ptr);
               PyMem_Del(out_ptr);
               return return_value;
            }
            break; 
         case SQL_SMALLINT:
         case SQL_INTEGER:
            rc = _python_ibm_db_get_data(stmt_res, col_num+1, SQL_C_LONG, 
                                         &long_val, sizeof(long_val), 
                                         &out_length);
            if ( rc == SQL_ERROR ) {
               return Py_False;
            }
            if (out_length == SQL_NULL_DATA) {
               return NULL;
            } else {
               return PyLong_FromLong(long_val);
            }
            break;

         case SQL_REAL:
         case SQL_FLOAT:
         case SQL_DOUBLE:
            rc = _python_ibm_db_get_data(stmt_res, col_num+1, SQL_C_DOUBLE, 
                                         &double_val, sizeof(double_val), 
                                         &out_length);
            if ( rc == SQL_ERROR ) {
               return Py_False;
            }
            if (out_length == SQL_NULL_DATA) {
               return NULL;
            } else {
               return PyFloat_FromDouble(double_val);
            }
            break;

         case SQL_CLOB:
            rc = _python_ibm_db_get_length(stmt_res, col_num+1, &in_length);
            if ( rc == SQL_ERROR ) {
               return Py_False;
            }
            if (in_length == SQL_NULL_DATA) {
               return NULL;
            }
            out_char_ptr = (char*)ALLOC_N(char,in_length+1);
            if ( out_char_ptr == NULL ) {
               PyErr_SetString(PyExc_Exception, 
                               "Failed to Allocate Memory for LOB Data");
               return Py_False;
            }
            rc = _python_ibm_db_get_data2(stmt_res, col_num+1, SQL_C_CHAR, 
                                          (void*)out_char_ptr, in_length+1, 
                                          &out_length);
            if (rc == SQL_ERROR) {
               return Py_False;
            }

            return PyString_FromString(out_char_ptr);
            break;

         case SQL_BLOB:
         case SQL_BINARY:
#ifndef PASE /* i5/OS SQL_LONGVARCHAR is SQL_VARCHAR */
         case SQL_LONGVARBINARY:
#endif /* PASE */
         case SQL_VARBINARY:
            rc = _python_ibm_db_get_length(stmt_res, col_num+1, &in_length);
            if ( rc == SQL_ERROR ) {
               return Py_False;
            }
            if (in_length == SQL_NULL_DATA) {
               return NULL;
            }

            switch (stmt_res->s_bin_mode) {
               case PASSTHRU:
                  return PyString_FromStringAndSize("",0);
                  break;
                  /* returns here */
               case CONVERT:
                  in_length *= 2;
                  lob_bind_type = SQL_C_CHAR;
                  /* fall-through */

               case BINARY:

                  out_ptr = (SQLPOINTER)ALLOC_N(char,in_length);
                  if ( out_ptr == NULL ) {
                     PyErr_SetString(PyExc_Exception, 
                                     "Failed to Allocate Memory for LOB Data");
                     return Py_False;
                  }
                  rc = _python_ibm_db_get_data2(stmt_res, col_num+1, 
                                                lob_bind_type, (char *)out_ptr, 
                                                in_length, &out_length);
                  if (rc == SQL_ERROR) {
                     return Py_False;
                  }
                  return PyString_FromStringAndSize((char*)out_ptr,out_length);
               default:
                  break;
            }
            break;

         case SQL_XML:
            rc = _python_ibm_db_get_length(stmt_res, col_num+1, &in_length);
            if ( rc == SQL_ERROR ) {
               return Py_False;
            }
            if (in_length == SQL_NULL_DATA) {
               return NULL;
            }
            out_ptr = (SQLPOINTER)ALLOC_N(char, in_length);
            if ( out_ptr == NULL ) {
               PyErr_SetString(PyExc_Exception, 
                               "Failed to Allocate Memory for XML Data");
               return Py_False;
            }
            rc = _python_ibm_db_get_data2(stmt_res, col_num+1, SQL_C_BINARY, 
                                          (SQLPOINTER)out_ptr, in_length, 
                                          &out_length);
            if (rc == SQL_ERROR) {
               return Py_False;
            }
            return PyString_FromStringAndSize((char*)out_ptr,out_length);

         default:
            break;
      }
   } else {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
   }

   return Py_False;
}

/* static void _python_ibm_db_bind_fetch_helper(INTERNAL_FUNCTION_PARAMETERS, 
                                                int op)
*/
static PyObject *_python_ibm_db_bind_fetch_helper(PyObject *args, int op)
{
   int rc = -1, i;
   SQLINTEGER row_number=-1;
   stmt_handle *stmt_res = NULL;
   SQLSMALLINT column_type, lob_bind_type = SQL_C_BINARY;
   ibm_db_row_data_type *row_data;
   SQLINTEGER out_length, tmp_length;
   unsigned char *out_ptr;
   PyObject *return_value = NULL;
   char error[DB2_MAX_ERR_MSG_LEN];

	if (!PyArg_ParseTuple(args, "O|i", &stmt_res, &row_number))
      return NULL;

   if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
		return NULL;
   }

   _python_ibm_db_init_error_info(stmt_res);

   /* get column header info */
   if ( stmt_res->column_info == NULL ) {
      if (_python_ibm_db_get_result_set_info(stmt_res)<0) {
         sprintf(error, "Column information cannot be retrieved: %s", 
                 IBM_DB_G(__python_stmt_err_msg));
         PyErr_SetString(PyExc_Exception, error);
         return NULL;
      }
   }
   /* bind the data */
   if ( stmt_res->row_data == NULL ) {
      rc = _python_ibm_db_bind_column_helper(stmt_res);
      if ( rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO ) {
         sprintf(error, "Column binding cannot be done: %s", 
                 IBM_DB_G(__python_stmt_err_msg));
         PyErr_SetString(PyExc_Exception, error);
         return NULL;
      }
   }
   /* check if row_number is present */
   if (PyTuple_Size(args) == 2 && row_number > 0) {
#ifndef PASE /* i5/OS problem with SQL_FETCH_ABSOLUTE (temporary until fixed) */
      if (is_systemi) {
         rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_FIRST, 
                             row_number);
         if (row_number>1 && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO))
            rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_RELATIVE, 
                                row_number-1);
      } else {
         rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_ABSOLUTE, 
                             row_number);
      }
#else /* PASE */
      rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_FIRST, 
                          row_number);
      if (row_number>1 && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO))
         rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_RELATIVE, 
                             row_number-1);
#endif /* PASE */
   } else if (PyTuple_Size(args) == 2 && row_number < 0) {
      PyErr_SetString(PyExc_Exception, 
                      "Requested row number must be a positive value");
      return NULL;
   } else {
      /* row_number is NULL or 0; just fetch next row */
      rc = SQLFetch((SQLHSTMT)stmt_res->hstmt);
   }

   if (rc == SQL_NO_DATA_FOUND) {
		Py_INCREF(Py_False);
      return Py_False;
   } else if ( rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 1, 
                                      NULL, -1, 1);
      sprintf(error, "Fetch Failure: %s", IBM_DB_G(__python_stmt_err_msg));
      PyErr_SetString(PyExc_Exception, error);
      return NULL;
   }
   /* copy the data over return_value */
   if ( op & FETCH_ASSOC ) {
         return_value = PyDict_New();
   } else if ( op == FETCH_INDEX ) {
         return_value = PyTuple_New(stmt_res->num_columns);
   }

   for (i=0; i<stmt_res->num_columns; i++) {
      column_type = stmt_res->column_info[i].type;
      row_data = &stmt_res->row_data[i].data;
      out_length = stmt_res->row_data[i].out_length;

      switch(stmt_res->s_case_mode) {
         case CASE_LOWER:
            stmt_res->column_info[i].name = (SQLCHAR*)strtolower((char*)stmt_res->column_info[i].name, strlen((char*)stmt_res->column_info[i].name));
            break;
         case CASE_UPPER:
            stmt_res->column_info[i].name = (SQLCHAR*)strtoupper((char*)stmt_res->column_info[i].name, strlen((char*)stmt_res->column_info[i].name));
            break;
         case CASE_NATURAL:
         default:
            break;
      }
      if (out_length == SQL_NULL_DATA) {
         if ( op & FETCH_ASSOC ) {
            PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      Py_None);
         }
         if ( op == FETCH_INDEX ) {
				PyTuple_SetItem(return_value, i, Py_None);
         } else if ( op == FETCH_BOTH ) {
            PyDict_SetItem(return_value, PyInt_FromLong(i), Py_None);
         }
      } else {
         switch(column_type) {
            case SQL_CHAR:
            case SQL_VARCHAR:
#ifndef PASE /* i5/OS SQL_LONGVARCHAR is SQL_VARCHAR */
            case SQL_LONGVARCHAR:
#else /* PASE */
               /* i5/OS will xlate from EBCIDIC to ASCII (via SQLGetData) */
               tmp_length = stmt_res->column_info[i].size;
               out_ptr = (SQLPOINTER)malloc(tmp_length+1);
               memset(out_ptr,0,tmp_length+1);
               if ( out_ptr == NULL ) {
                  PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
                  return NULL;
               }
               rc = _python_ibm_db_get_data(stmt_res, i+1, SQL_C_CHAR, out_ptr,
                                            tmp_length+1, &out_length);
               if ( rc == SQL_ERROR ) {
                  free(out_ptr);
                  return NULL;
               }
               if (out_length == SQL_NULL_DATA) {
                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      Py_None);
                  }
                  if ( op & FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, Py_None);
                  }
               } else {
                  out_ptr[tmp_length] = '\0';
                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                     PyString_FromString((char *)stmt_res->column_info[i].name),
                     PyString_FromString((char *)out_ptr));
                  }
                  if ( op & FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, 
                                     PyString_FromString((char *)out_ptr));
                  }
               }
	            free(out_ptr);
               break;
#endif /* PASE */
            case SQL_TYPE_DATE:
            case SQL_TYPE_TIME:
            case SQL_TYPE_TIMESTAMP:
            case SQL_BIGINT:
            case SQL_DECIMAL:
            case SQL_NUMERIC:

               if ( op & FETCH_ASSOC ) {
                  PyDict_SetItem(return_value, 
                     PyString_FromString((char *)stmt_res->column_info[i].name),
                     PyString_FromString((char *)row_data->str_val));
               }
               if ( op == FETCH_INDEX ) {
                  PyTuple_SetItem(return_value, i, 
                                PyString_FromString((char *)row_data->str_val));
               } else if ( op == FETCH_BOTH ) {
                  PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                PyString_FromString((char *)row_data->str_val));
               }
               break;
            case SQL_SMALLINT:
               if ( op & FETCH_ASSOC ) {
                  PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyInt_FromLong(row_data->s_val));
               }
               if ( op == FETCH_INDEX ) {
                  PyTuple_SetItem(return_value, i, 
                                  PyInt_FromLong(row_data->s_val));
               } else if ( op == FETCH_BOTH ) {
                  PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                 PyInt_FromLong(row_data->s_val));
               }
               break;
            case SQL_INTEGER:
               if ( op & FETCH_ASSOC ) {
                  PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyLong_FromLong(row_data->i_val));
               }
               if ( op == FETCH_INDEX ) {
                  PyTuple_SetItem(return_value, i, 
                                  PyLong_FromLong(row_data->i_val));
               } else if ( op == FETCH_BOTH ) {
                  PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                 PyLong_FromLong(row_data->i_val));
               }
               break;

            case SQL_REAL:
            case SQL_FLOAT:
               if ( op & FETCH_ASSOC ) {
                  PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyFloat_FromDouble(row_data->f_val));
               }
               if ( op == FETCH_INDEX ) {
                  PyTuple_SetItem(return_value, i, 
                                  PyFloat_FromDouble(row_data->f_val));
               } else if ( op == FETCH_BOTH ) {
                  PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                 PyFloat_FromDouble(row_data->f_val));
               }
               break;

            case SQL_DOUBLE:
               if ( op & FETCH_ASSOC ) {
                  PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyFloat_FromDouble(row_data->d_val));
               }
               if ( op == FETCH_INDEX ) {
                  PyTuple_SetItem(return_value, i, 
                                  PyFloat_FromDouble(row_data->d_val));
               } else if ( op == FETCH_BOTH ) {
                  PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                 PyFloat_FromDouble(row_data->d_val));
               }
               break;

            case SQL_BINARY:
#ifndef PASE /* i5/OS SQL_LONGVARBINARY is SQL_VARBINARY */
            case SQL_LONGVARBINARY:
#endif /* PASE */
            case SQL_VARBINARY:
               if ( stmt_res->s_bin_mode == PASSTHRU ) {
                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyString_FromStringAndSize("",0));
                  }
                  if ( op == FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, 
                                     PyString_FromStringAndSize("",0));
                  } else if ( op == FETCH_BOTH ) {
                     PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                    PyString_FromStringAndSize("",0));
                  }
               } else {
                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyString_FromString((char *)row_data->str_val));
                  }
                  if ( op == FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, 
                                PyString_FromString((char *)row_data->str_val));
                  } else if ( op == FETCH_BOTH ) {
                     PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                PyString_FromString((char *)row_data->str_val));
                  }
               }
               break;

            case SQL_BLOB:
               out_ptr = NULL;
               rc=_python_ibm_db_get_length(stmt_res, i+1, &tmp_length);

               if (tmp_length == SQL_NULL_DATA) {
                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      Py_None);
                  }
                  if ( op == FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, Py_None);
                  } else if ( op == FETCH_BOTH ) {
                     PyDict_SetItem(return_value, PyInt_FromLong(i), Py_None);
                  }
               } else {
                  if (rc == SQL_ERROR) tmp_length = 0;
                  switch (stmt_res->s_bin_mode) {
                     case PASSTHRU:
                        if ( op & FETCH_ASSOC ) {
                              PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      Py_None);
                        }
                        if ( op == FETCH_INDEX ) {
                              PyTuple_SetItem(return_value, i, Py_None);
                        } else if ( op == FETCH_BOTH ) {
                              PyDict_SetItem(return_value, PyInt_FromLong(i), 
                                             Py_None);
                        }
                        break;

                     case CONVERT:
                        tmp_length = 2*tmp_length + 1;
                        lob_bind_type = SQL_C_CHAR;
                        /* fall-through */

                     case BINARY:
                        out_ptr = (SQLPOINTER)ALLOC_N(char, tmp_length);
                        if ( out_ptr == NULL ) {
                           PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
                           return NULL;
                        }
                        rc = _python_ibm_db_get_data2(stmt_res, i+1, 
                                                      lob_bind_type, 
                                                      (char *)out_ptr, 
                                                      tmp_length, &out_length);
                        if (rc == SQL_ERROR) {
			                  PyMem_Del(out_ptr);
                           out_length = 0;
                        }

                        if ( op & FETCH_ASSOC ) {
                           PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyString_FromStringAndSize((char*)out_ptr, out_length));
                        }
                        if ( op == FETCH_INDEX ) {
                           PyTuple_SetItem(return_value, i, 
								              PyString_FromStringAndSize((char*)out_ptr,
                                                                  out_length));
                        } else if ( op == FETCH_BOTH ) {
                           PyDict_SetItem(return_value, PyInt_FromLong(i), 
                        PyString_FromStringAndSize((char*)out_ptr, out_length));
                        }
                        break;
                     default:
                        break;
                  }
               }
               break;

            case SQL_XML:
               out_ptr = NULL;
               rc = _python_ibm_db_get_data(stmt_res, i+1, SQL_C_BINARY, NULL, 
                                            0, (SQLINTEGER *)&tmp_length);
               if ( rc == SQL_ERROR ) {
                  sprintf(error, "Failed to Determine XML Size: %s", 
                          IBM_DB_G(__python_stmt_err_msg));
                  PyErr_SetString(PyExc_Exception, error);
                  return NULL;
               }

               if (tmp_length == SQL_NULL_DATA) {
                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                     PyString_FromString((char *)stmt_res->column_info[i].name),
                     Py_None);
                  }
                  if ( op & FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, Py_None);
                  } else if ( op == FETCH_BOTH) {
                     PyDict_SetItem(return_value, PyInt_FromLong(i), Py_None);
                  }
               } else {
                  out_ptr = (SQLPOINTER)ALLOC_N(char, tmp_length);

                  if ( out_ptr == NULL ) {
                     PyErr_SetString(PyExc_Exception, 
                                     "Failed to Allocate Memory for XML Data");
                     return NULL;
                  }
                  rc = _python_ibm_db_get_data(stmt_res, i+1, SQL_C_BINARY, 
                                              out_ptr, tmp_length, &out_length);
                  if (rc == SQL_ERROR) {
		               PyMem_Del(out_ptr);
                     return NULL;
                  }

                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                     PyString_FromString((char *)stmt_res->column_info[i].name),
                     PyString_FromStringAndSize((char *)out_ptr, out_length));
                  }
                  if ( op == FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, 
                                     PyString_FromStringAndSize((char *)out_ptr,
                                                                out_length));
                  } else if ( op == FETCH_BOTH ) {
                     PyDict_SetItem(return_value, PyInt_FromLong(i), 
                       PyString_FromStringAndSize((char *)out_ptr, out_length));
                  }
               }
               break;

            case SQL_CLOB:
               out_ptr = NULL;
               rc = _python_ibm_db_get_length(stmt_res, i+1, &tmp_length);

               if (tmp_length == SQL_NULL_DATA) {
                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      Py_None);
                  }
                  if ( op == FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, Py_None);
                  } else if ( op == FETCH_BOTH ) {
                     PyDict_SetItem(return_value, PyInt_FromLong(i), Py_None);
                  }
               } else {
                  if (rc == SQL_ERROR) tmp_length = 0;
                  out_ptr = (SQLPOINTER)ALLOC_N(char, tmp_length+1);

                  if ( out_ptr == NULL ) {
                     PyErr_SetString(PyExc_Exception, 
                                     "Failed to Allocate Memory for LOB Data");
                     return NULL;
                  }

                  rc = _python_ibm_db_get_data2(stmt_res, i+1, SQL_C_CHAR, 
                                                out_ptr, tmp_length+1, 
                                                &out_length);
                  if (rc == SQL_ERROR) {
                     PyMem_Del(out_ptr);
                     tmp_length = 0;
                  } else {
                     out_ptr[tmp_length] = '\0';
                  }

                  if ( op & FETCH_ASSOC ) {
                     PyDict_SetItem(return_value, 
                      PyString_FromString((char*)stmt_res->column_info[i].name),
                      PyString_FromStringAndSize((char*)out_ptr, tmp_length));
                  }
                  if ( op == FETCH_INDEX ) {
                     PyTuple_SetItem(return_value, i, 
                                     PyString_FromStringAndSize((char*)out_ptr, 
                                                                tmp_length));
                  } else if ( op == FETCH_BOTH ) {
                     PyDict_SetItem(return_value, PyInt_FromLong(i), 
                        PyString_FromStringAndSize((char*)out_ptr, tmp_length));
                  }
               }
               break;

            default:
               break;
         }
      }
   }
   return return_value;
}

/*!# ibm_db.fetch_row
 *
 * ===Description
 * bool ibm_db.fetch_row ( resource stmt [, int row_number] )
 *
 * Sets the result set pointer to the next row or requested row
 *
 * Use ibm_db.fetch_row() to iterate through a result set, or to point to a
 * specific row in a result set if you requested a scrollable cursor.
 *
 * To retrieve individual fields from the result set, call the ibm_db.result()
 * function. Rather than calling ibm_db.fetch_row() and ibm_db.result(), most
 * applications will call one of ibm_db.fetch_assoc(), ibm_db.fetch_both(), or
 * ibm_db.fetch_array() to advance the result set pointer and return a complete
 * row as an array.
 *
 * ===Parameters
 * ====stmt
 *       A valid stmt resource.
 *
 * ====row_number
 *       With scrollable cursors, you can request a specific row number in the
 * result set. Row numbering is 1-indexed.
 *
 * ===Return Values
 *
 * Returns TRUE if the requested row exists in the result set. Returns FALSE if
 * the requested row does not exist in the result set.
 */
static PyObject *ibm_db_fetch_row(PyObject *self, PyObject *args)           
{
   SQLINTEGER row_number = -1;
   stmt_handle* stmt_res = NULL;
   int rc;
   char error[DB2_MAX_ERR_MSG_LEN];

	if (!PyArg_ParseTuple(args, "O|i", &stmt_res, &row_number))
      return NULL;

	if (NIL_P(stmt_res)) {
      PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
		return NULL;
   }

   /* get column header info */
   if ( stmt_res->column_info == NULL ) {
      if (_python_ibm_db_get_result_set_info(stmt_res)<0) {
         sprintf(error, "Column information cannot be retrieved: %s", 
                 IBM_DB_G(__python_stmt_err_msg));
         PyErr_SetString(PyExc_Exception, error);
			Py_INCREF(Py_False);
         return Py_False;
      }
   }

   /* check if row_number is present */
   if (PyTuple_Size(args) == 2 && row_number > 0) { 
#ifndef PASE /* i5/OS problem with SQL_FETCH_ABSOLUTE */

      rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_ABSOLUTE, 
                          row_number);
#else /* PASE */
      rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_FIRST, 
                          row_number);
      if (row_number>1 && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO))
         rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, SQL_FETCH_RELATIVE, 
                             row_number-1);
#endif /* PASE */
   } else if (PyTuple_Size(args) == 2 && row_number < 0) {
      PyErr_SetString(PyExc_Exception, 
                      "Requested row number must be a positive value");
      return NULL;
   } else {
      /* row_number is NULL or 0; just fetch next row */
      rc = SQLFetch((SQLHSTMT)stmt_res->hstmt);
   }

   if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
      Py_INCREF(Py_True);
      return Py_True;
   } else if (rc == SQL_NO_DATA_FOUND) {
      Py_INCREF(Py_False);
      return Py_False;
   } else {
      _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 1,
                                      NULL, -1, 1);
      Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.fetch_assoc
 *
 * ===Description
 * dictionary ibm_db.fetch_assoc ( resource stmt [, int row_number] )
 *
 * Returns a dictionary, indexed by column name, representing a row in a result  * set.
 *
 * ===Parameters
 * ====stmt
 *       A valid stmt resource containing a result set.
 *
 * ====row_number
 *
 *       Requests a specific 1-indexed row from the result set. Passing this
 * parameter results in a
 *       Python warning if the result set uses a forward-only cursor.
 *
 * ===Return Values
 *
 * Returns an associative array with column values indexed by the column name
 * representing the next
 * or requested row in the result set. Returns FALSE if there are no rows left
 * in the result set,
 * or if the row requested by row_number does not exist in the result set.
 */
static PyObject *ibm_db_fetch_assoc(PyObject *self, PyObject *args)           
{
   return _python_ibm_db_bind_fetch_helper(args, FETCH_ASSOC);
}


/*
 * ibm_db.fetch_object --   Returns an object with properties representing columns in the fetched row
 * 
 * ===Description
 * object ibm_db.fetch_object ( resource stmt [, int row_number] )
 * 
 * Returns an object in which each property represents a column returned in the row fetched from a result set.
 * 
 * ===Parameters
 * 
 * stmt
 *       A valid stmt resource containing a result set. 
 * 
 * row_number
 *       Requests a specific 1-indexed row from the result set. Passing this parameter results in a
 *       Python warning if the result set uses a forward-only cursor. 
 * 
 * ===Return Values
 * 
 * Returns an object representing a single row in the result set. The properties of the object map
 * to the names of the columns in the result set.
 * 
 * The IBM DB2, Cloudscape, and Apache Derby database servers typically fold column names to upper-case,
 * so the object properties will reflect that case.
 * 
 * If your SELECT statement calls a scalar function to modify the value of a column, the database servers
 * return the column number as the name of the column in the result set. If you prefer a more
 * descriptive column name and object property, you can use the AS clause to assign a name
 * to the column in the result set.
 * 
 * Returns FALSE if no row was retrieved. 
 */
/*
PyObject *ibm_db_fetch_object(int argc, PyObject **argv, PyObject *self)
{
   row_hash_struct *row_res;

   row_res = ALLOC(row_hash_struct);
   row_res->hash = _python_ibm_db_bind_fetch_helper(argc, argv, FETCH_ASSOC);

   if (RTEST(row_res->hash)) {
      return Data_Wrap_Struct(le_row_struct,
            _python_ibm_db_mark_row_struct, _python_ibm_db_free_row_struct,
            row_res);
   } else {
      free(row_res);
      return Py_False;
   }
}
*/

/*!# ibm_db.fetch_array
 *
 * ===Description
 *
 * array ibm_db.fetch_array ( resource stmt [, int row_number] )
 *
 * Returns a tuple, indexed by column position, representing a row in a result
 * set. The columns are 0-indexed.
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid stmt resource containing a result set.
 *
 * ====row_number
 *       Requests a specific 1-indexed row from the result set. Passing this
 * parameter results in a warning if the result set uses a forward-only cursor.
 *
 * ===Return Values
 *
 * Returns a 0-indexed tuple with column values indexed by the column position
 * representing the next or requested row in the result set. Returns FALSE if
 * there are no rows left in the result set, or if the row requested by
 * row_number does not exist in the result set.
 */
static PyObject *ibm_db_fetch_array(PyObject *self, PyObject *args)
{
   return _python_ibm_db_bind_fetch_helper(args, FETCH_INDEX);
}

/*!# ibm_db.fetch_both
 *
 * ===Description
 * dictionary ibm_db.fetch_both ( resource stmt [, int row_number] )
 *
 * Returns a dictionary, indexed by both column name and position, representing  * a row in a result set. Note that the row returned by ibm_db.fetch_both()
 * requires more memory than the single-indexed dictionaries/arrays returned by  * ibm_db.fetch_assoc() or ibm_db.fetch_tuple().
 *
 * ===Parameters
 *
 * ====stmt
 *       A valid stmt resource containing a result set.
 *
 * ====row_number
 *       Requests a specific 1-indexed row from the result set. Passing this
 * parameter results in a warning if the result set uses a forward-only cursor.
 *
 * ===Return Values
 *
 * Returns a dictionary with column values indexed by both the column name and
 * 0-indexed column number.
 * The dictionary represents the next or requested row in the result set.
 * Returns FALSE if there are no rows left in the result set, or if the row
 * requested by row_number does not exist in the result set.
 */
static PyObject *ibm_db_fetch_both(PyObject *self, PyObject *args)
{
   return _python_ibm_db_bind_fetch_helper(args, FETCH_BOTH);
}

/*!# ibm_db.set_option
 *
 * ===Description
 * bool ibm_db.set_option ( resource resc, array options, int type )
 *
 * Sets options for a connection or statement resource. You cannot set options
 * for result set resources.
 *
 * ===Parameters
 *
 * ====resc
 *       A valid connection or statement resource.
 *
 * ====options
 *       The options to be set
 *
 * ====type
 *       A field that specifies the resource type (1 = Connection,
 * NON-1 = Statement)
 *
 * ===Return Values
 *
 * Returns TRUE on success or FALSE on failure
 */
static PyObject *ibm_db_set_option(PyObject *self, PyObject *args)
{
   PyObject *conn_or_stmt = NULL;
   PyObject *options = NULL;
   stmt_handle *stmt_res = NULL;
   conn_handle *conn_res;
   int rc = 0;
   long type = 0;

	if (!PyArg_ParseTuple(args, "OOi", &conn_or_stmt, &options, &type))
      return NULL;

   if (!NIL_P(conn_or_stmt)) {
      if ( type == 1 ) {
			conn_res = (conn_handle *)conn_or_stmt;

         if ( !NIL_P(options) ) {
            rc = _python_ibm_db_parse_options(options, SQL_HANDLE_DBC, 
                                              conn_res);
            if (rc == SQL_ERROR) {
               PyErr_SetString(PyExc_Exception, 
                               "Options Array must have string indexes");
               return NULL;
            }
         }
      } else {
         stmt_res = (stmt_handle *)conn_or_stmt;                  

         if ( !NIL_P(options) ) {
            rc = _python_ibm_db_parse_options(options, SQL_HANDLE_STMT, 
                                              stmt_res);
            if (rc == SQL_ERROR) {
               PyErr_SetString(PyExc_Exception, 
                               "Options Array must have string indexes");
               return NULL;
            }
         }
      }
		Py_INCREF(Py_True);
      return Py_True;
   } else {
		Py_INCREF(Py_False);
      return Py_False;
   }
}

/*!# ibm_db.server_info
 *
 * ===Description
 * object ibm_db.server_info ( resource connection )
 *
 * This function returns a read-only object with information about the IBM DB2
 * or Informix Dynamic Server.
 * The following table lists the database server properties:
 *
 * ====Table 1. Database server properties
 * Property name:: Description (Return type)
 *
 * DBMS_NAME:: The name of the database server to which you are connected. For
 * DB2 servers this is a combination of DB2 followed by the operating system on
 * which the database server is running. (string)
 *
 * DBMS_VER:: The version of the database server, in the form of a string
 * "MM.mm.uuuu" where MM is the major version, mm is the minor version, and
 * uuuu is the update. For example, "08.02.0001" represents major version 8,
 * minor version 2, update 1. (string)
 *
 * DB_CODEPAGE:: The code page of the database to which you are connected. (int)
 *
 * DB_NAME:: The name of the database to which you are connected. (string)
 *
 * DFT_ISOLATION:: The default transaction isolation level supported by the
 * server: (string)
 *
 *                         UR:: Uncommitted read: changes are immediately
 * visible by all concurrent transactions.
 *
 *                         CS:: Cursor stability: a row read by one transaction
 * can be altered and committed by a second concurrent transaction.
 *
 *                         RS:: Read stability: a transaction can add or remove
 * rows matching a search condition or a pending transaction.
 *
 *                         RR:: Repeatable read: data affected by pending
 * transaction is not available to other transactions.
 *
 *                         NC:: No commit: any changes are visible at the end of
 * a successful operation. Explicit commits and rollbacks are not allowed.
 *
 * IDENTIFIER_QUOTE_CHAR:: The character used to delimit an identifier. (string)
 *
 * INST_NAME:: The instance on the database server that contains the database.
 * (string)
 *
 * ISOLATION_OPTION:: An array of the isolation options supported by the
 * database server. The isolation options are described in the DFT_ISOLATION
 * property. (array)
 *
 * KEYWORDS:: An array of the keywords reserved by the database server. (array)
 *
 * LIKE_ESCAPE_CLAUSE:: TRUE if the database server supports the use of % and _
 * wildcard characters. FALSE if the database server does not support these
 * wildcard characters. (bool)
 *
 * MAX_COL_NAME_LEN:: Maximum length of a column name supported by the database
 * server, expressed in bytes. (int)
 *
 * MAX_IDENTIFIER_LEN:: Maximum length of an SQL identifier supported by the
 * database server, expressed in characters. (int)
 *
 * MAX_INDEX_SIZE:: Maximum size of columns combined in an index supported by
 * the database server, expressed in bytes. (int)
 *
 * MAX_PROC_NAME_LEN:: Maximum length of a procedure name supported by the
 * database server, expressed in bytes. (int)
 *
 * MAX_ROW_SIZE:: Maximum length of a row in a base table supported by the
 * database server, expressed in bytes. (int)
 *
 * MAX_SCHEMA_NAME_LEN:: Maximum length of a schema name supported by the
 * database server, expressed in bytes. (int)
 *
 * MAX_STATEMENT_LEN:: Maximum length of an SQL statement supported by the
 * database server, expressed in bytes. (int)
 *
 * MAX_TABLE_NAME_LEN:: Maximum length of a table name supported by the
 * database server, expressed in bytes. (bool)
 *
 * NON_NULLABLE_COLUMNS:: TRUE if the database server supports columns that can
 * be defined as NOT NULL, FALSE if the database server does not support columns
 * defined as NOT NULL. (bool)
 *
 * PROCEDURES:: TRUE if the database server supports the use of the CALL
 * statement to call stored procedures, FALSE if the database server does not
 * support the CALL statement. (bool)
 *
 * SPECIAL_CHARS:: A string containing all of the characters other than a-Z,
 * 0-9, and underscore that can be used in an identifier name. (string)
 *
 * SQL_CONFORMANCE:: The level of conformance to the ANSI/ISO SQL-92
 * specification offered by the database server: (string)
 *
 *                            ENTRY:: Entry-level SQL-92 compliance.
 *
 *                            FIPS127:: FIPS-127-2 transitional compliance.
 *
 *                            FULL:: Full level SQL-92 compliance.
 *
 *                            INTERMEDIATE:: Intermediate level SQL-92
 *                                           compliance.
 *
 * ===Parameters
 *
 * ====connection
 *       Specifies an active DB2 client connection.
 *
 * ===Return Values
 *
 * Returns an object on a successful call. Returns FALSE on failure.
 */
static PyObject *ibm_db_server_info(PyObject *self, PyObject *args)
{
   conn_handle *conn_res;
   int rc = 0;
   char buffer11[11];
   char buffer255[255];
   char buffer2k[2048];
   SQLSMALLINT bufferint16;
   SQLUINTEGER bufferint32;
   SQLINTEGER bitmask;

	le_server_info *return_value = PyObject_NEW(le_server_info,
                                         &server_infoType);

	if (!PyArg_ParseTuple(args, "O", &conn_res))
      return NULL;

   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      /* DBMS_NAME */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DBMS_NAME, (SQLPOINTER)buffer255, 
                      sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DBMS_NAME = PyString_FromString(buffer255);
      }

      /* DBMS_VER */
      memset(buffer11, 0, sizeof(buffer11));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DBMS_VER, (SQLPOINTER)buffer11, 
                      sizeof(buffer11), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DBMS_VER = PyString_FromString(buffer11);
      }

#ifndef PASE      /* i5/OS DB_CODEPAGE handled natively */
      /* DB_CODEPAGE */
      bufferint32 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_DATABASE_CODEPAGE, &bufferint32, 
                      sizeof(bufferint32), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DB_CODEPAGE = PyInt_FromLong(bufferint32);
      }
#endif /* PASE */

      /* DB_NAME */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DATABASE_NAME, (SQLPOINTER)buffer255, 
                      sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DB_NAME = PyString_FromString(buffer255);
      }

#ifndef PASE      /* i5/OS INST_NAME handled natively */
      /* INST_NAME */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_SERVER_NAME, (SQLPOINTER)buffer255, 
                      sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->INST_NAME = PyString_FromString(buffer255);
      }

      /* SPECIAL_CHARS */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_SPECIAL_CHARACTERS, 
                      (SQLPOINTER)buffer255, sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->SPECIAL_CHARS = PyString_FromString(buffer255);
      }
#endif /* PASE */

      /* KEYWORDS */
      memset(buffer2k, 0, sizeof(buffer2k));
      rc = SQLGetInfo(conn_res->hdbc, SQL_KEYWORDS, (SQLPOINTER)buffer2k, 
                      sizeof(buffer2k), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         char *keyword, *last;
         PyObject *karray;
			int numkw = 0;
			int count = 0;
	
			for (last = buffer2k; *last; last++) {
            if (*last == ',') {
               numkw++;         
            }
         }
			karray = PyTuple_New(numkw+1);

         for (keyword = last = buffer2k; *last; last++) {
            if (*last == ',') {
               *last = '\0';
					PyTuple_SetItem(karray, count, PyString_FromString(keyword));
               keyword = last+1;
					count++;
            }
         }
			if (*keyword) 
				PyTuple_SetItem(karray, count, PyString_FromString(keyword));
			return_value->KEYWORDS = karray;
      }

      /* DFT_ISOLATION */
      bitmask = 0;
      memset(buffer11, 0, sizeof(buffer11));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DEFAULT_TXN_ISOLATION, &bitmask, 
                      sizeof(bitmask), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         if( bitmask & SQL_TXN_READ_UNCOMMITTED ) {
            strcpy((char *)buffer11, "UR");
         }
         if( bitmask & SQL_TXN_READ_COMMITTED ) {
            strcpy((char *)buffer11, "CS");
         }
         if( bitmask & SQL_TXN_REPEATABLE_READ ) {
            strcpy((char *)buffer11, "RS");
         }
         if( bitmask & SQL_TXN_SERIALIZABLE ) {
            strcpy((char *)buffer11, "RR");
         }
         if( bitmask & SQL_TXN_NOCOMMIT ) {
            strcpy((char *)buffer11, "NC");
         }
			return_value->DFT_ISOLATION = PyString_FromString(buffer11);
      }

#ifndef PASE      /* i5/OS ISOLATION_OPTION handled natively */
      /* ISOLATION_OPTION */
      bitmask = 0;
      memset(buffer11, 0, sizeof(buffer11));
      rc = SQLGetInfo(conn_res->hdbc, SQL_TXN_ISOLATION_OPTION, &bitmask, 
                      sizeof(bitmask), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         PyObject *array;
			int count = 0;

			array = PyTuple_New(5);

         if( bitmask & SQL_TXN_READ_UNCOMMITTED ) {
				PyTuple_SetItem(array, count, PyString_FromString("UR"));
				count++;
         }
         if( bitmask & SQL_TXN_READ_COMMITTED ) {
				PyTuple_SetItem(array, count, PyString_FromString("CS"));
				count++;
         }
         if( bitmask & SQL_TXN_REPEATABLE_READ ) {
				PyTuple_SetItem(array, count, PyString_FromString("RS"));
				count++;
         }
         if( bitmask & SQL_TXN_SERIALIZABLE ) {
				PyTuple_SetItem(array, count, PyString_FromString("RR"));
				count++;
         }
         if( bitmask & SQL_TXN_NOCOMMIT ) {
				PyTuple_SetItem(array, count, PyString_FromString("NC"));
				count++;
         }
			_PyTuple_Resize(&array, count);

			return_value->ISOLATION_OPTION = array;
      }
#endif /* PASE */

      /* SQL_CONFORMANCE */
      bufferint32 = 0;
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_ODBC_SQL_CONFORMANCE, &bufferint32, 
                      sizeof(bufferint32), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         switch (bufferint32) {
            case SQL_SC_SQL92_ENTRY:
               strcpy((char *)buffer255, "ENTRY");
               break;
            case SQL_SC_FIPS127_2_TRANSITIONAL:
               strcpy((char *)buffer255, "FIPS127");
               break;
            case SQL_SC_SQL92_FULL:
               strcpy((char *)buffer255, "FULL");
               break;
            case SQL_SC_SQL92_INTERMEDIATE:
               strcpy((char *)buffer255, "INTERMEDIATE");
               break;
            default:
               break;
         }
			return_value->SQL_CONFORMANCE = PyString_FromString(buffer255);
      }

      /* PROCEDURES */
      memset(buffer11, 0, sizeof(buffer11));
      rc = SQLGetInfo(conn_res->hdbc, SQL_PROCEDURES, (SQLPOINTER)buffer11, 
                      sizeof(buffer11), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         if( strcmp((char *)buffer11, "Y") == 0 ) {
				Py_INCREF(Py_True);
				return_value->PROCEDURES = Py_True;
         } else {
				Py_INCREF(Py_False);
				return_value->PROCEDURES = Py_False;
         }
      }

      /* IDENTIFIER_QUOTE_CHAR */
      memset(buffer11, 0, sizeof(buffer11));
      rc = SQLGetInfo(conn_res->hdbc, SQL_IDENTIFIER_QUOTE_CHAR, 
                      (SQLPOINTER)buffer11, sizeof(buffer11), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->IDENTIFIER_QUOTE_CHAR = PyString_FromString(buffer11);
      }

      /* LIKE_ESCAPE_CLAUSE */
      memset(buffer11, 0, sizeof(buffer11));
      rc = SQLGetInfo(conn_res->hdbc, SQL_LIKE_ESCAPE_CLAUSE, 
                      (SQLPOINTER)buffer11, sizeof(buffer11), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         if( strcmp(buffer11, "Y") == 0 ) {
				Py_INCREF(Py_True);
				return_value->LIKE_ESCAPE_CLAUSE = Py_True;
         } else {
				Py_INCREF(Py_False);
				return_value->LIKE_ESCAPE_CLAUSE = Py_False;
         }
      }

      /* MAX_COL_NAME_LEN */
      bufferint16 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_COLUMN_NAME_LEN, &bufferint16, 
                      sizeof(bufferint16), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_COL_NAME_LEN = PyInt_FromLong(bufferint16);
      }

      /* MAX_ROW_SIZE */
      bufferint32 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_ROW_SIZE, &bufferint32, 
                      sizeof(bufferint32), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_ROW_SIZE = PyInt_FromLong(bufferint32);
      }

#ifndef PASE      /* i5/OS MAX_IDENTIFIER_LEN handled natively */
      /* MAX_IDENTIFIER_LEN */
      bufferint16 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_IDENTIFIER_LEN, &bufferint16, 
                      sizeof(bufferint16), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_IDENTIFIER_LEN = PyInt_FromLong(bufferint16);
      }

      /* MAX_INDEX_SIZE */
      bufferint32 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_INDEX_SIZE, &bufferint32, 
                      sizeof(bufferint32), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_INDEX_SIZE = PyInt_FromLong(bufferint32);
      }

      /* MAX_PROC_NAME_LEN */
      bufferint16 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_PROCEDURE_NAME_LEN, &bufferint16, 
                      sizeof(bufferint16), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_PROC_NAME_LEN = PyInt_FromLong(bufferint16);
      }
#endif /* PASE */

      /* MAX_SCHEMA_NAME_LEN */
      bufferint16 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_SCHEMA_NAME_LEN, &bufferint16, 
                      sizeof(bufferint16), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_SCHEMA_NAME_LEN = PyInt_FromLong(bufferint16);
      }

      /* MAX_STATEMENT_LEN */
      bufferint32 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_STATEMENT_LEN, &bufferint32, 
                      sizeof(bufferint32), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_STATEMENT_LEN = PyInt_FromLong(bufferint32);
      }

      /* MAX_TABLE_NAME_LEN */
      bufferint16 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_MAX_TABLE_NAME_LEN, &bufferint16, 
                      sizeof(bufferint16), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->MAX_TABLE_NAME_LEN = PyInt_FromLong(bufferint16);
      }

      /* NON_NULLABLE_COLUMNS */
      bufferint16 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_NON_NULLABLE_COLUMNS, &bufferint16, 
                      sizeof(bufferint16), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         PyObject *rv = NULL;
         switch (bufferint16) {
            case SQL_NNC_NON_NULL:
					Py_INCREF(Py_True);
               rv = Py_True;
               break;
            case SQL_NNC_NULL:
					Py_INCREF(Py_False);
               rv = Py_False;
               break;
            default:
               break;
         }
			return_value->NON_NULLABLE_COLUMNS = rv;
      }
      return (PyObject *)return_value;
   }
	Py_INCREF(Py_False);
   return Py_False;
}

/*!# ibm_db.client_info
 *
 * ===Description
 * object ibm_db.client_info ( resource connection )
 *
 * This function returns a read-only object with information about the IBM Data
 * Server database client. The following table lists the client properties:
 *
 * ====IBM Data Server client properties
 *
 * APPL_CODEPAGE:: The application code page.
 *
 * CONN_CODEPAGE:: The code page for the current connection.
 *
 * DATA_SOURCE_NAME:: The data source name (DSN) used to create the current
 * connection to the database.
 *
 * DRIVER_NAME:: The name of the library that implements the Call Level
 * Interface (CLI) specification.
 *
 * DRIVER_ODBC_VER:: The version of ODBC that the IBM Data Server client
 * supports. This returns a string "MM.mm" where MM is the major version and mm
 * is the minor version. The IBM Data Server client always returns "03.51".
 *
 * DRIVER_VER:: The version of the client, in the form of a string "MM.mm.uuuu"
 * where MM is the major version, mm is the minor version, and uuuu is the
 * update. For example, "08.02.0001" represents major version 8, minor version
 * 2, update 1. (string)
 *
 * ODBC_SQL_CONFORMANCE:: There are three levels of ODBC SQL grammar supported
 * by the client: MINIMAL (Supports the minimum ODBC SQL grammar), CORE
 * (Supports the core ODBC SQL grammar), EXTENDED (Supports extended ODBC SQL
 * grammar).
 *
 * ODBC_VER:: The version of ODBC that the ODBC driver manager supports. This
 * returns a string "MM.mm.rrrr" where MM is the major version, mm is the minor
 * version, and rrrr is the release. The client always returns "03.01.0000".
 *
 * ===Parameters
 *
 * ====connection
 *
 *      Specifies an active IBM Data Server client connection.
 *
 * ===Return Values
 *
 * Returns an object on a successful call. Returns FALSE on failure.
 */
static PyObject *ibm_db_client_info(PyObject *self, PyObject *args)           
{
   conn_handle *conn_res = NULL;
   int rc = 0;
   char buffer255[255];
   SQLSMALLINT bufferint16;
   SQLUINTEGER bufferint32;

   le_client_info *return_value = PyObject_NEW(le_client_info, 
                                         &client_infoType);

	if (!PyArg_ParseTuple(args, "O", &conn_res))
      return NULL;


   if (!NIL_P(conn_res)) {
      if (!conn_res->handle_active) {
         PyErr_SetString(PyExc_Exception, "Connection is not active");
			return NULL;
      }

      /* DRIVER_NAME */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DRIVER_NAME, (SQLPOINTER)buffer255, 
                      sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DRIVER_NAME = PyString_FromString(buffer255);
      }

      /* DRIVER_VER */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DRIVER_VER, (SQLPOINTER)buffer255, 
                      sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DRIVER_VER = PyString_FromString(buffer255);
      }

      /* DATA_SOURCE_NAME */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DATA_SOURCE_NAME, 
                      (SQLPOINTER)buffer255, sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DATA_SOURCE_NAME = PyString_FromString(buffer255);
      }

      /* DRIVER_ODBC_VER */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_DRIVER_ODBC_VER, 
                      (SQLPOINTER)buffer255, sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
      								           NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->DRIVER_ODBC_VER = PyString_FromString(buffer255);
      }

#ifndef PASE      /* i5/OS ODBC_VER handled natively */
      /* ODBC_VER */
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_ODBC_VER, (SQLPOINTER)buffer255, 
                      sizeof(buffer255), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->ODBC_VER = PyString_FromString(buffer255);
      }
#endif /* PASE */

      /* ODBC_SQL_CONFORMANCE */
      bufferint16 = 0;
      memset(buffer255, 0, sizeof(buffer255));
      rc = SQLGetInfo(conn_res->hdbc, SQL_ODBC_SQL_CONFORMANCE, &bufferint16, 
                      sizeof(bufferint16), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
         switch (bufferint16) {
            case SQL_OSC_MINIMUM:
               strcpy((char *)buffer255, "MINIMUM");
               break;
            case SQL_OSC_CORE:
               strcpy((char *)buffer255, "CORE");
               break;
            case SQL_OSC_EXTENDED:
               strcpy((char *)buffer255, "EXTENDED");
               break;
            default:
               break;
         }
			return_value->ODBC_SQL_CONFORMANCE = PyString_FromString(buffer255);
      }

#ifndef PASE      /* i5/OS APPL_CODEPAGE handled natively */
      /* APPL_CODEPAGE */
      bufferint32 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_APPLICATION_CODEPAGE, &bufferint32, 
                      sizeof(bufferint32), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->APPL_CODEPAGE = PyInt_FromLong(bufferint32);
      }

      /* CONN_CODEPAGE */
      bufferint32 = 0;
      rc = SQLGetInfo(conn_res->hdbc, SQL_CONNECT_CODEPAGE, &bufferint32, 
                      sizeof(bufferint32), NULL);

      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1, 
                                         NULL, -1, 1);
			Py_INCREF(Py_False);
         return Py_False;
      } else {
			return_value->CONN_CODEPAGE = PyInt_FromLong(bufferint32);
      }
#endif /* PASE */

         return (PyObject *)return_value;
   }
	Py_INCREF(Py_False);
   return Py_False;
}

/*!# ibm_db.active
 *
 * ===Description
 * Py_True/Py_False ibm_db.active(resource connection)
 *
 * Checks if the specified connection resource is active
 *
 * Returns Py_True if the given connection resource is active
 *
 * ===Parameters
 * ====connection
 *       The connection resource to be validated.
 *
 * ===Return Values
 *
 * Returns Py_True if the given connection resource is active, otherwise it will
 * return Py_False
 */
static PyObject *ibm_db_active(PyObject *self, PyObject *args)           
{
   int rc;
   conn_handle *conn_res = NULL;
   SQLINTEGER conn_alive;

   conn_alive = 0;

	if (!PyArg_ParseTuple(args, "O", &conn_res))
      return NULL;

   if (!NIL_P(conn_res)) {
#ifndef PASE
      rc = SQLGetConnectAttr(conn_res->hdbc, SQL_ATTR_PING_DB, 
                             (SQLPOINTER)&conn_alive, 0, NULL);
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, rc, 1,
                                         NULL, -1, 1);
      }
#endif /* PASE */
   }
   /*
    * SQLGetConnectAttr with SQL_ATTR_PING_DB will return 0 on failure but will 
    * return the ping time on success.   We only want success or failure.
    */
   if (conn_alive == 0) {
		Py_INCREF(Py_False);
      return Py_False;
   } else {
		Py_INCREF(Py_True);
      return Py_True;
   }
}

/*!# ibm_db.get_option
 *
 * ===Description
 * mixed ibm_db.get_option ( resource resc, int options, int type )
 *
 * Returns a value, that is the current setting of a connection or statement
 * attribute.
 *
 * ===Parameters
 *
 * ====resc
 *       A valid connection or statement resource containing a result set.
 *
 * ====options
 *       The options to be retrieved
 *
 * ====type
 *       A field that specifies the resource type (1 = Connection,
 *       non - 1 = Statement)
 *
 * ===Return Values
 *
 * Returns the current setting of the resource attribute provided.
 */
static PyObject *ibm_db_get_option(PyObject *self, PyObject *args)
{
   PyObject *conn_or_stmt = NULL;
   SQLCHAR *value = NULL;
   SQLINTEGER value_int = 0;
   conn_handle *conn_res = NULL;
   stmt_handle *stmt_res = NULL;
   SQLINTEGER op_integer = 0;
   long type = 0;
   int rc;

	if (!PyArg_ParseTuple(args, "Oii", &conn_or_stmt, &op_integer, &type))
      return NULL;

   if (!NIL_P(conn_or_stmt)) {
      /* Checking to see if we are getting a connection option (1) or a 
       * statement option (non - 1) 
       */
      if (type == 1) {
			conn_res = (conn_handle *)conn_or_stmt;

         /* Check to ensure the connection resource given is active */
         if (!conn_res->handle_active) {
            PyErr_SetString(PyExc_Exception, "Connection is not active");
				return NULL;
         }
         /* Check that the option given is not null */
         if (!NIL_P(&op_integer)) {
            /* ACCTSTR_LEN is the largest possible length of the options to 
             * retrieve 
             */
            value = ALLOC_N(char, ACCTSTR_LEN + 1);
            if ( value == NULL ) {
               PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
               return NULL;
            }

            rc = SQLGetConnectAttr((SQLHDBC)conn_res->hdbc, op_integer, 
                                   (SQLPOINTER)value, ACCTSTR_LEN, NULL);
            if (rc == SQL_ERROR) {
               _python_ibm_db_check_sql_errors(conn_res->hdbc, SQL_HANDLE_DBC, 
                                               rc, 1, NULL, -1, 1);
					Py_INCREF(Py_False); 
					return Py_False;
            }
            return PyString_FromString((char *)value);
         } else {
            PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
				return NULL;
         }
      /* At this point we know we are to retreive a statement option */
      } else {
			stmt_res = (stmt_handle *)conn_or_stmt;

         /* Check that the option given is not null */
         if (!NIL_P(&op_integer)) {
            /* Checking that the option to get is the cursor type because that 
             * is what we support here 
             */
            if (op_integer == SQL_ATTR_CURSOR_TYPE) {
               rc = SQLGetStmtAttr((SQLHSTMT)stmt_res->hstmt, op_integer, 
                                   &value_int, SQL_IS_INTEGER, NULL);
               if (rc == SQL_ERROR) {
                  _python_ibm_db_check_sql_errors(stmt_res->hstmt, SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
						Py_INCREF(Py_False); 
						return Py_False;
               }
               return PyInt_FromLong(value_int);
            } else {
               PyErr_SetString(PyExc_Exception,"Supplied parameter is invalid");
					return NULL;
            }
         } else {
            PyErr_SetString(PyExc_Exception, "Supplied parameter is invalid");
				return NULL;
         }
      }
   }
	Py_INCREF(Py_False);
   return Py_False;
}

/*
 * ibm_db.get_last_serial_value --   Gets the last inserted serial value from IDS
 *
 * ===Description
 * string ibm_db.get_last_serial_value ( resource stmt )
 *
 * Returns a string, that is the last inserted value for a serial column for IDS. 
 * The last inserted value could be auto-generated or entered explicitly by the user
 * This function is valid for IDS (Informix Dynamic Server only)
 *
 * ===Parameters
 *
 * stmt
 *       A valid statement resource.
 *
 * ===Return Values
 *
 * Returns a string representation of last inserted serial value on a successful call. 
 * Returns FALSE on failure.
 */
/*
PyObject *ibm_db_get_last_serial_value(int argc, PyObject **argv, PyObject *self)
{
   PyObject *stmt = NULL;
   SQLCHAR *value = NULL;
   stmt_handle *stmt_res;
   int rc = 0;
   
   rb_scan_args(argc, argv, "1", &stmt);

   if (!NIL_P(stmt)) {
      Data_Get_Struct(stmt, stmt_handle, stmt_res);

      / * We allocate a buffer of size 31 as per recommendations from the CLI IDS team * /
      value = ALLOC_N(char, 31);
      if ( value == NULL ) {
         PyErr_SetString(PyExc_Exception, "Failed to Allocate Memory");
         return Py_False;
      }

      rc = SQLGetStmtAttr((SQLHSTMT)stmt_res->hstmt, SQL_ATTR_GET_GENERATED_VALUE, (SQLPOINTER)value, 31, NULL);
      if ( rc == SQL_ERROR ) {
         _python_ibm_db_check_sql_errors( (SQLHSTMT)stmt_res->hstmt, SQL_HANDLE_STMT, rc, 1, NULL, -1, 1);
         return Py_False;
      }
      return INT2NUM(atoi(value));
   }
   else {
      PyErr_SetString(PyExc_Exception, "Supplied statement handle is invalid");
      return Py_False;
   }
}
*/

static int _python_get_variable_type(PyObject *variable_value)
{
	if (PyBool_Check(variable_value) && (variable_value == Py_True))
		return PYTHON_TRUE;
	else if (PyBool_Check(variable_value) && (variable_value == Py_False))
		return PYTHON_FALSE;
	else if (PyInt_Check(variable_value) || PyLong_Check(variable_value))
		return PYTHON_FIXNUM;
	else if (PyFloat_Check(variable_value))
		return PYTHON_FLOAT;
	else if (PyString_Check(variable_value))
		return PYTHON_STRING;
	else if (variable_value == Py_None)
		return PYTHON_NIL;
	else return 0;
}

/* Listing of ibm_db module functions: */
static PyMethodDef ibm_db_Methods[] = {
   /* name, function, argument type, docstring */
   {"connect", (PyCFunction)ibm_db_connect, METH_VARARGS | METH_KEYWORDS, "Connect to the database"},
   {"pconnect", (PyCFunction)ibm_db_pconnect, METH_VARARGS | METH_KEYWORDS, "Returns a persistent connection to a database"},
   {"exec_immediate", (PyCFunction)ibm_db_exec, METH_VARARGS, "Prepares and executes an SQL statement."},
   {"prepare", (PyCFunction)ibm_db_prepare, METH_VARARGS, "Prepares an SQL statement."},
   {"bind_param", (PyCFunction)ibm_db_bind_param, METH_VARARGS, "Binds a Python variable to an SQL statement parameter"},
   {"execute", (PyCFunction)ibm_db_execute, METH_VARARGS, "Executes an SQL statement that was prepared by ibm_db.prepare()"},
   {"fetch_tuple", (PyCFunction)ibm_db_fetch_array, METH_VARARGS, "Returns an tuple, indexed by column position, representing a row in a result set"},
   {"fetch_assoc", (PyCFunction)ibm_db_fetch_assoc, METH_VARARGS, "Returns a dictionary, indexed by column name, representing a row in a result set"},
   {"fetch_both", (PyCFunction)ibm_db_fetch_both, METH_VARARGS, "Returns a dictionary, indexed by both column name and position, representing a row in a result set"},
   {"fetch_row", (PyCFunction)ibm_db_fetch_row, METH_VARARGS, "Sets the result set pointer to the next row or requested row"},
   {"result", (PyCFunction)ibm_db_result, METH_VARARGS, "Returns a single column from a row in the result set"},
   {"active", (PyCFunction)ibm_db_active, METH_VARARGS, "Checks if the specified connection resource is active"},
   {"autocommit", (PyCFunction)ibm_db_autocommit , METH_VARARGS, "Returns or sets the AUTOCOMMIT state for a database connection"},
   {"close", (PyCFunction)ibm_db_close , METH_VARARGS, "Close a database connection"},
   {"conn_error", (PyCFunction)ibm_db_conn_error , METH_VARARGS, "Returns a string containing the SQLSTATE returned by the last connection attempt"},
   {"conn_errormsg", (PyCFunction)ibm_db_conn_errormsg , METH_VARARGS, "Returns an error message and SQLCODE value representing the reason the last database connection attempt failed"},
   {"client_info", (PyCFunction)ibm_db_client_info , METH_VARARGS, "Returns a read-only object with information about the DB2 database client"},
	{"column_privileges", (PyCFunction)ibm_db_column_privileges , METH_VARARGS, "Returns a result set listing the columns and associated privileges for a table."},
	{"columns", (PyCFunction)ibm_db_columns , METH_VARARGS, "Returns a result set listing the columns and associated metadata for a table"},
	{"commit", (PyCFunction)ibm_db_commit , METH_VARARGS, "Commits a transaction"},
	{"cursor_type", (PyCFunction)ibm_db_cursor_type , METH_VARARGS, "Returns the cursor type used by a statement resource"},
	{"field_display_size", (PyCFunction)ibm_db_field_display_size , METH_VARARGS, "Returns the maximum number of bytes required to display a column"},
	{"field_name", (PyCFunction)ibm_db_field_name , METH_VARARGS, "Returns the name of the column in the result set"},
	{"field_num", (PyCFunction)ibm_db_field_num , METH_VARARGS, "Returns the position of the named column in a result set"},
	{"field_precision", (PyCFunction)ibm_db_field_precision , METH_VARARGS, "Returns the precision of the indicated column in a result set"},
	{"field_scale", (PyCFunction)ibm_db_field_scale , METH_VARARGS, "Returns the scale of the indicated column in a result set"},
	{"field_type", (PyCFunction)ibm_db_field_type , METH_VARARGS, "Returns the data type of the indicated column in a result set"},
	{"field_width", (PyCFunction)ibm_db_field_width , METH_VARARGS, "Returns the width of the indicated column in a result set"},
	{"foreign_keys", (PyCFunction)ibm_db_foreign_keys , METH_VARARGS, "Returns a result set listing the foreign keys for a table"},
	{"free_result", (PyCFunction)ibm_db_free_result , METH_VARARGS, "Frees resources associated with a result set"},
	{"free_stmt", (PyCFunction)ibm_db_free_stmt , METH_VARARGS, "Frees resources associated with the indicated statement resource"},
	{"get_option", (PyCFunction)ibm_db_get_option , METH_VARARGS, "Gets the specified option in the resource."},
	{"next_result", (PyCFunction)ibm_db_next_result , METH_VARARGS, "Requests the next result set from a stored procedure"},
	{"num_fields", (PyCFunction)ibm_db_num_fields , METH_VARARGS, "Returns the number of fields contained in a result set"},
	{"num_rows", (PyCFunction)ibm_db_num_rows , METH_VARARGS, "Returns the number of rows affected by an SQL statement"},
	{"primary_keys", (PyCFunction)ibm_db_primary_keys , METH_VARARGS, "Returns a result set listing primary keys for a table"},
	{"procedure_columns", (PyCFunction)ibm_db_procedure_columns , METH_VARARGS, "Returns a result set listing the parameters for one or more stored procedures."},
	{"procedures", (PyCFunction)ibm_db_procedures , METH_VARARGS, "Returns a result set listing the stored procedures registered in a database"},
	{"rollback", (PyCFunction)ibm_db_rollback , METH_VARARGS, "Rolls back a transaction"},
	{"server_info", (PyCFunction)ibm_db_server_info , METH_VARARGS, "Returns an object with properties that describe the DB2 database server"},
	{"set_option", (PyCFunction)ibm_db_set_option , METH_VARARGS, "Sets the specified option in the resource"},
	{"special_columns", (PyCFunction)ibm_db_special_columns , METH_VARARGS, "Returns a result set listing the unique row identifier columns for a table"},
	{"statistics", (PyCFunction)ibm_db_statistics, METH_VARARGS, "Returns a result set listing the index and statistics for a table"},
	{"stmt_error", (PyCFunction)ibm_db_stmt_error , METH_VARARGS, "Returns a string containing the SQLSTATE returned by an SQL statement"},
	{"stmt_errormsg", (PyCFunction)ibm_db_stmt_errormsg , METH_VARARGS, "Returns a string containing the last SQL statement error message"},
	{"table_privileges", (PyCFunction)ibm_db_table_privileges , METH_VARARGS, "Returns a result set listing the tables and associated privileges in a database"},
	{"tables", (PyCFunction)ibm_db_tables , METH_VARARGS, "Returns a result set listing the tables and associated metadata in a database"},
   /* An end-of-listing sentinel: */ 
   {NULL, NULL, 0, NULL}
};
   
#ifndef PyMODINIT_FUNC   /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
/* Module initialization function */
PyMODINIT_FUNC
initibm_db(void) {
	PyObject* m;

	ibm_db_globals = ALLOC(struct _ibm_db_globals);
	memset(ibm_db_globals, 0, sizeof(struct _ibm_db_globals));
	python_ibm_db_init_globals(ibm_db_globals);

   persistent_list = PyDict_New();

   conn_handleType.tp_new = PyType_GenericNew;
   if (PyType_Ready(&conn_handleType) < 0)
      return;

   stmt_handleType.tp_new = PyType_GenericNew;
   if (PyType_Ready(&stmt_handleType) < 0)
      return;

	client_infoType.tp_new = PyType_GenericNew;
   if (PyType_Ready(&client_infoType) < 0)
      return;

   server_infoType.tp_new = PyType_GenericNew;
   if (PyType_Ready(&server_infoType) < 0)
      return;

	m = Py_InitModule3("ibm_db", ibm_db_Methods,
                      "IBM DataServer Driver for Python.");

   Py_INCREF(&conn_handleType);
   PyModule_AddObject(m, "IBM_DBConnection", (PyObject *)&conn_handleType);

	PyModule_AddIntConstant(m, "SQL_AUTOCOMMIT_ON", SQL_AUTOCOMMIT_ON);
	PyModule_AddIntConstant(m, "SQL_AUTOCOMMIT_OFF", SQL_AUTOCOMMIT_OFF);
	PyModule_AddIntConstant(m, "SQL_ATTR_AUTOCOMMIT", SQL_ATTR_AUTOCOMMIT);
	PyModule_AddIntConstant(m, "ATTR_CASE", ATTR_CASE);
	PyModule_AddIntConstant(m, "CASE_NATURAL", CASE_NATURAL);
	PyModule_AddIntConstant(m, "CASE_LOWER", CASE_LOWER);
	PyModule_AddIntConstant(m, "CASE_UPPER", CASE_UPPER);
	PyModule_AddIntConstant(m, "SQL_ATTR_CURSOR_TYPE", SQL_ATTR_CURSOR_TYPE);
	PyModule_AddIntConstant(m, "SQL_CURSOR_FORWARD_ONLY", 
                           SQL_CURSOR_FORWARD_ONLY);
	PyModule_AddIntConstant(m, "SQL_CURSOR_KEYSET_DRIVEN", 
                           SQL_CURSOR_KEYSET_DRIVEN);
	PyModule_AddIntConstant(m, "SQL_CURSOR_DYNAMIC", SQL_CURSOR_DYNAMIC);
	PyModule_AddIntConstant(m, "SQL_CURSOR_STATIC", SQL_CURSOR_STATIC);
	PyModule_AddIntConstant(m, "SQL_PARAM_INPUT", SQL_PARAM_INPUT);
	PyModule_AddIntConstant(m, "SQL_PARAM_OUTPUT", SQL_PARAM_OUTPUT);
	PyModule_AddIntConstant(m, "SQL_PARAM_INPUT_OUTPUT", SQL_PARAM_INPUT_OUTPUT);
	PyModule_AddIntConstant(m, "PARAM_FILE", PARAM_FILE);

   PyModule_AddIntConstant(m, "SQL_BIGINT", SQL_BIGINT);
   PyModule_AddIntConstant(m, "SQL_BINARY", SQL_BINARY);
   PyModule_AddIntConstant(m, "SQL_BLOB", SQL_BLOB);
   PyModule_AddIntConstant(m, "SQL_BLOB_LOCATOR", SQL_BLOB_LOCATOR);
   PyModule_AddIntConstant(m, "SQL_CHAR", SQL_CHAR);
   PyModule_AddIntConstant(m, "SQL_TINYINT", SQL_TINYINT);
   PyModule_AddIntConstant(m, "SQL_BINARY", SQL_BINARY);
   PyModule_AddIntConstant(m, "SQL_BIT", SQL_BIT);
   PyModule_AddIntConstant(m, "SQL_CLOB", SQL_CLOB);
   PyModule_AddIntConstant(m, "SQL_CLOB_LOCATOR", SQL_CLOB_LOCATOR);
   PyModule_AddIntConstant(m, "SQL_TYPE_DATE", SQL_TYPE_DATE);
   PyModule_AddIntConstant(m, "SQL_DBCLOB", SQL_DBCLOB);
   PyModule_AddIntConstant(m, "SQL_DBCLOB_LOCATOR", SQL_DBCLOB_LOCATOR);
   PyModule_AddIntConstant(m, "SQL_DECIMAL", SQL_DECIMAL);
   PyModule_AddIntConstant(m, "SQL_DECFLOAT", SQL_DECFLOAT);
   PyModule_AddIntConstant(m, "SQL_DECFLOAT", SQL_DECFLOAT);
   PyModule_AddIntConstant(m, "SQL_DOUBLE", SQL_DOUBLE);
   PyModule_AddIntConstant(m, "SQL_FLOAT", SQL_FLOAT);
   PyModule_AddIntConstant(m, "SQL_GRAPHIC", SQL_GRAPHIC);
   PyModule_AddIntConstant(m, "SQL_INTEGER", SQL_INTEGER);
   PyModule_AddIntConstant(m, "SQL_LONGVARCHAR", SQL_LONGVARCHAR);
   PyModule_AddIntConstant(m, "SQL_LONGVARBINARY", SQL_LONGVARBINARY);
   PyModule_AddIntConstant(m, "SQL_LONGVARGRAPHIC", SQL_LONGVARGRAPHIC);
   PyModule_AddIntConstant(m, "SQL_WLONGVARCHAR", SQL_WLONGVARCHAR);
   PyModule_AddIntConstant(m, "SQL_NUMERIC", SQL_NUMERIC);
   PyModule_AddIntConstant(m, "SQL_REAL", SQL_REAL);
   PyModule_AddIntConstant(m, "SQL_SMALLINT", SQL_SMALLINT);
   PyModule_AddIntConstant(m, "SQL_TYPE_TIME", SQL_TYPE_TIME);
   PyModule_AddIntConstant(m, "SQL_TYPE_TIMESTAMP", SQL_TYPE_TIMESTAMP);
   PyModule_AddIntConstant(m, "SQL_VARBINARY", SQL_VARBINARY);
   PyModule_AddIntConstant(m, "SQL_VARCHAR", SQL_VARCHAR);
   PyModule_AddIntConstant(m, "SQL_VARBINARY", SQL_VARBINARY);
   PyModule_AddIntConstant(m, "SQL_VARGRAPHIC", SQL_VARGRAPHIC);
   PyModule_AddIntConstant(m, "SQL_WVARCHAR", SQL_WVARCHAR);
   PyModule_AddIntConstant(m, "SQL_WCHAR", SQL_WCHAR);
   PyModule_AddIntConstant(m, "SQL_XML", SQL_XML);
   PyModule_AddIntConstant(m, "SQL_FALSE", SQL_FALSE);
   PyModule_AddIntConstant(m, "SQL_TRUE", SQL_TRUE);
   PyModule_AddIntConstant(m, "SQL_TABLE_STAT", SQL_TABLE_STAT);
   PyModule_AddIntConstant(m, "SQL_INDEX_CLUSTERED", SQL_INDEX_CLUSTERED);
   PyModule_AddIntConstant(m, "SQL_INDEX_OTHER", SQL_INDEX_OTHER);
   PyModule_AddIntConstant(m, "SQL_ATTR_CURRENT_SCHEMA", SQL_ATTR_CURRENT_SCHEMA);
   PyModule_AddIntConstant(m, "SQL_ATTR_INFO_USERID", SQL_ATTR_INFO_USERID);
   PyModule_AddIntConstant(m, "SQL_ATTR_INFO_WRKSTNNAME", SQL_ATTR_INFO_WRKSTNNAME);
   PyModule_AddIntConstant(m, "SQL_ATTR_INFO_ACCTSTR", SQL_ATTR_INFO_ACCTSTR);
   PyModule_AddIntConstant(m, "SQL_ATTR_INFO_APPLNAME", SQL_ATTR_INFO_APPLNAME);


   Py_INCREF(&stmt_handleType);
   PyModule_AddObject(m, "IBM_DBStatement", (PyObject *)&stmt_handleType);

   Py_INCREF(&client_infoType);
   PyModule_AddObject(m, "IBM_DBClientInfo", (PyObject *)&client_infoType);

	Py_INCREF(&server_infoType);
   PyModule_AddObject(m, "IBM_DBServerInfo", (PyObject *)&server_infoType);
}

