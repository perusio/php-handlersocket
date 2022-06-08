
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef PHP_WIN32
#ifdef HAVE_DEBUG
#define HS_DEBUG 1
#endif
#endif

#include "php.h"
#include "php_ini.h"
#include "php_network.h"
#include "php_streams.h"
#include "zend_exceptions.h"
#include "ext/standard/file.h"
#include "ext/standard/info.h"
#include "ext/standard/php_smart_str.h"
#include "ext/standard/php_string.h"

#ifndef PHP_WIN32
#define php_select(m, r, w, e, t) select(m, r, w, e, t)
#else
#include "win32/select.h"
#endif

#include "php_handlersocket.h"

#define HS_PRIMARY "PRIMARY"

#define HS_PROTOCOL_OPEN    "P"
#define HS_PROTOCOL_AUTH    "A"
#define HS_PROTOCOL_INSERT  "+"
#define HS_PROTOCOL_FILTER  "F"
#define HS_PROTOCOL_WHILE   "W"
#define HS_PROTOCOL_IN      "@"

#define HS_FIND_EQUAL         "="
#define HS_FIND_LESS          "<"
#define HS_FIND_LESS_EQUAL    "<="
#define HS_FIND_GREATER       ">"
#define HS_FIND_GREATER_EQUAL ">="

#define HS_MODIFY_UPDATE         "U"
#define HS_MODIFY_INCREMENT      "+"
#define HS_MODIFY_DECREMENT      "-"
#define HS_MODIFY_REMOVE         "D"
#define HS_MODIFY_UPDATE_PREV    "U?"
#define HS_MODIFY_INCREMENT_PREV "+?"
#define HS_MODIFY_DECREMENT_PREV "-?"
#define HS_MODIFY_REMOVE_PREV    "D?"

#define HS_CODE_NULL          0x00
#define HS_CODE_DELIMITER     0x09
#define HS_CODE_EOL           0x0a
#define HS_CODE_ESCAPE        0x10
#define HS_CODE_ESCAPE_PREFIX 0x01
#define HS_CODE_ESCAPE_ADD    0x40

#define HS_SOCKET_BLOCK_SIZE 4096

static zend_class_entry *hs_ce = NULL;
static zend_class_entry *hs_index_ce = NULL;
static zend_class_entry *hs_exception_ce = NULL;

typedef struct
{
    zend_object object;
    php_stream *stream;
    long timeout;
    zval *server;
    zval *auth;
    zval *error;
} php_hs_t;

typedef struct
{
    zend_object object;
    long id;
    zval *link;
    zval *filter;
    zval *error;
} php_hs_index_t;

#if ZEND_MODULE_API_NO >= 20090115
#   define PUSH_PARAM(arg) zend_vm_stack_push(arg TSRMLS_CC)
#   define POP_PARAM() (void)zend_vm_stack_pop(TSRMLS_C)
#   define PUSH_EO_PARAM()
#   define POP_EO_PARAM()
#else
#   define PUSH_PARAM(arg) zend_ptr_stack_push(&EG(argument_stack), arg)
#   define POP_PARAM() (void)zend_ptr_stack_pop(&EG(argument_stack))
#   define PUSH_EO_PARAM() zend_ptr_stack_push(&EG(argument_stack), NULL)
#   define POP_EO_PARAM() (void)zend_ptr_stack_pop(&EG(argument_stack))
#   define array_init_size(arg, size) _array_init((arg))
#endif

#define HS_METHOD_BASE(classname, name) zim_##classname##_##name

#define HS_METHOD_HELPER(classname, name, retval, thisptr, num, param) \
  PUSH_PARAM(param); PUSH_PARAM((void*)num); \
  PUSH_EO_PARAM(); \
  HS_METHOD_BASE(classname, name)(num, retval, NULL, thisptr, 0 TSRMLS_CC); \
  POP_EO_PARAM(); \
  POP_PARAM(); POP_PARAM();

#define HS_METHOD(classname, name, retval, thisptr) \
  HS_METHOD_BASE(classname, name)(0, retval, NULL, thisptr, 0 TSRMLS_CC);

#define HS_METHOD6(classname, name, retval, thisptr, param1, param2, param3, param4, param5, param6) \
  PUSH_PARAM(param1); PUSH_PARAM(param2); PUSH_PARAM(param3); PUSH_PARAM(param4);; PUSH_PARAM(param5); \
  HS_METHOD_HELPER(classname, name, retval, thisptr, 6, param6); \
  POP_PARAM(); POP_PARAM(); POP_PARAM(); POP_PARAM(); POP_PARAM();

#define HS_METHOD7(classname, name, retval, thisptr, param1, param2, param3, param4, param5, param6, param7) \
  PUSH_PARAM(param1); PUSH_PARAM(param2); PUSH_PARAM(param3); PUSH_PARAM(param4); PUSH_PARAM(param5); PUSH_PARAM(param6); \
  HS_METHOD_HELPER(classname, name, retval, thisptr, 7, param7); \
  POP_PARAM(); POP_PARAM(); POP_PARAM(); POP_PARAM(); POP_PARAM(); POP_PARAM();

#define HS_CHECK_OBJECT(member, classname) \
  if (!(member)) { \
    zend_throw_exception_ex(hs_exception_ce, 0 TSRMLS_CC, "The " #classname " object has not been correctly initialized by its constructor"); \
    RETURN_FALSE; \
  }

#define HS_INDEX_PROPERTY(property, retval) \
  retval = zend_read_property(hs_index_ce, getThis(), #property, strlen(#property), 0 TSRMLS_CC);

#define HS_ERROR_RESET(error) \
  if (error) { zval_ptr_dtor(&error); } MAKE_STD_ZVAL(error); ZVAL_NULL(error);

#define HS_REQUEST_LONG(buf, num) smart_str_append_long(buf, num);
#define HS_REQUEST_NULL(buf) smart_str_appendc(buf, HS_CODE_NULL);
#define HS_REQUEST_DELIM(buf) smart_str_appendc(buf, HS_CODE_DELIMITER);
#define HS_REQUEST_NEXT(buf) smart_str_appendc(buf, HS_CODE_EOL);


static zend_object_value hs_new(zend_class_entry *ce TSRMLS_DC);
static ZEND_METHOD(HandlerSocket, __construct);
static ZEND_METHOD(HandlerSocket, auth);
static ZEND_METHOD(HandlerSocket, openIndex);
static ZEND_METHOD(HandlerSocket, executeSingle);
static ZEND_METHOD(HandlerSocket, executeMulti);
static ZEND_METHOD(HandlerSocket, executeUpdate);
static ZEND_METHOD(HandlerSocket, executeDelete);
static ZEND_METHOD(HandlerSocket, executeInsert);
static ZEND_METHOD(HandlerSocket, getError);
static ZEND_METHOD(HandlerSocket, createIndex);
#if PHP_VERSION_ID < 50300
static ZEND_METHOD(HandlerSocket, close);
#endif

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs___construct, 0, 0, 2)
    ZEND_ARG_INFO(0, host)
    ZEND_ARG_INFO(0, port)
    ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_auth, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, type)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_openIndex, 0, 0, 5)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, db)
    ZEND_ARG_INFO(0, table)
    ZEND_ARG_INFO(0, index)
    ZEND_ARG_INFO(0, field)
    ZEND_ARG_INFO(0, filter)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_executeSingle, 0, 0, 3)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, operate)
    ZEND_ARG_INFO(0, criteria)
    ZEND_ARG_INFO(0, limit)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, update)
    ZEND_ARG_INFO(0, values)
    ZEND_ARG_INFO(0, filters)
    ZEND_ARG_INFO(0, in_key)
    ZEND_ARG_INFO(0, in_values)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_executeMulti, 0, 0, 1)
    ZEND_ARG_INFO(0, args)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_executeUpdate, 0, 0, 4)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, operate)
    ZEND_ARG_INFO(0, criteria)
    ZEND_ARG_INFO(0, values)
    ZEND_ARG_INFO(0, limit)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, filters)
    ZEND_ARG_INFO(0, in_key)
    ZEND_ARG_INFO(0, in_values)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_executeDelete, 0, 0, 3)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, operate)
    ZEND_ARG_INFO(0, criteria)
    ZEND_ARG_INFO(0, limit)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, filters)
    ZEND_ARG_INFO(0, in_key)
    ZEND_ARG_INFO(0, in_values)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_executeInsert, 0, 0, 2)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, field)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_getError, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_createIndex, 0, 0, 5)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, db)
    ZEND_ARG_INFO(0, table)
    ZEND_ARG_INFO(0, index)
    ZEND_ARG_INFO(0, fields)
    ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

#if PHP_VERSION_ID < 50300
ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_close, 0, 0, 0)
ZEND_END_ARG_INFO()
#endif

static const zend_function_entry hs_methods[] = {
    ZEND_ME(HandlerSocket, __construct,
            arginfo_hs___construct, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, auth,
            arginfo_hs_auth, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, openIndex,
            arginfo_hs_openIndex, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, executeSingle,
            arginfo_hs_executeSingle, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, executeMulti,
            arginfo_hs_executeMulti, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, executeUpdate,
            arginfo_hs_executeUpdate, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, executeDelete,
            arginfo_hs_executeDelete, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, executeInsert,
            arginfo_hs_executeInsert, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, getError,
            arginfo_hs_getError, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocket, createIndex,
            arginfo_hs_createIndex, ZEND_ACC_PUBLIC)
#if PHP_VERSION_ID < 50300
    ZEND_ME(HandlerSocket, close,
            arginfo_hs_close, ZEND_ACC_PUBLIC)
#endif
    ZEND_MALIAS(HandlerSocket, executeFind, executeSingle,
                arginfo_hs_executeSingle, ZEND_ACC_PUBLIC)
    {NULL, NULL, NULL}
};


static zend_object_value hs_index_new(zend_class_entry *ce TSRMLS_DC);
static ZEND_METHOD(HandlerSocketIndex, __construct);
static ZEND_METHOD(HandlerSocketIndex, getId);
static ZEND_METHOD(HandlerSocketIndex, getDatabase);
static ZEND_METHOD(HandlerSocketIndex, getTable);
static ZEND_METHOD(HandlerSocketIndex, getName);
static ZEND_METHOD(HandlerSocketIndex, getField);
static ZEND_METHOD(HandlerSocketIndex, getFilter);
static ZEND_METHOD(HandlerSocketIndex, getOperator);
static ZEND_METHOD(HandlerSocketIndex, getError);
static ZEND_METHOD(HandlerSocketIndex, find);
static ZEND_METHOD(HandlerSocketIndex, insert);
static ZEND_METHOD(HandlerSocketIndex, update);
static ZEND_METHOD(HandlerSocketIndex, remove);
static ZEND_METHOD(HandlerSocketIndex, multi);

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index___construct, 0, 0, 6)
    ZEND_ARG_INFO(0, hs)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, db)
    ZEND_ARG_INFO(0, table)
    ZEND_ARG_INFO(0, index)
    ZEND_ARG_INFO(0, fields)
    ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getId, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getDatabase, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getTable, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getName, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getField, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getFilter, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getOperator, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_getError, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_find, 0, 0, 1)
    ZEND_ARG_INFO(0, query)
    ZEND_ARG_INFO(0, limit)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_insert, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_update, 0, 0, 2)
    ZEND_ARG_INFO(0, query)
    ZEND_ARG_INFO(0, update)
    ZEND_ARG_INFO(0, limit)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_remove, 0, 0, 1)
    ZEND_ARG_INFO(0, query)
    ZEND_ARG_INFO(0, limit)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_hs_index_multi, 0, 0, 1)
    ZEND_ARG_INFO(0, args)
ZEND_END_ARG_INFO()

static const zend_function_entry hs_index_methods[] = {
    ZEND_ME(HandlerSocketIndex, __construct,
            arginfo_hs_index___construct, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getId,
            arginfo_hs_index_getId, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getDatabase,
            arginfo_hs_index_getDatabase, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getTable,
            arginfo_hs_index_getTable, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getName,
            arginfo_hs_index_getName, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getField,
            arginfo_hs_index_getField, ZEND_ACC_PUBLIC)
    ZEND_MALIAS(HandlerSocketIndex, getColumn, getField,
                arginfo_hs_index_getField, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getFilter,
            arginfo_hs_index_getFilter, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getOperator,
            arginfo_hs_index_getOperator, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, getError,
            arginfo_hs_index_getError, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, find,
            arginfo_hs_index_find, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, insert,
            arginfo_hs_index_insert, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, update,
            arginfo_hs_index_update, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, remove,
            arginfo_hs_index_remove, ZEND_ACC_PUBLIC)
    ZEND_ME(HandlerSocketIndex, multi,
            arginfo_hs_index_multi, ZEND_ACC_PUBLIC)
    {NULL, NULL, NULL}
};


PHP_MINIT_FUNCTION(handlersocket)
{
    zend_class_entry ce;

    /* HandlerSocket class */
    INIT_CLASS_ENTRY(
        ce, "HandlerSocket", (zend_function_entry *)hs_methods);
    hs_ce = zend_register_internal_class(&ce TSRMLS_CC);
    hs_ce->create_object = hs_new;

    /* constant */
#if ZEND_MODULE_API_NO < 20050922
    REGISTER_STRING_CONSTANT(
        "HANDLERSOCKET_PRIMARY", HS_PRIMARY, CONST_CS | CONST_PERSISTENT);
    REGISTER_STRING_CONSTANT(
        "HANDLERSOCKET_UPDATE", HS_MODIFY_UPDATE, CONST_CS | CONST_PERSISTENT);
    REGISTER_STRING_CONSTANT(
        "HANDLERSOCKET_DELETE", HS_MODIFY_REMOVE, CONST_CS | CONST_PERSISTENT);
#else
    zend_declare_class_constant_string(
        hs_ce, "PRIMARY", strlen("PRIMARY"), HS_PRIMARY TSRMLS_CC);
    zend_declare_class_constant_string(
        hs_ce, "UPDATE", strlen("UPDATE"), HS_MODIFY_UPDATE TSRMLS_CC);
    zend_declare_class_constant_string(
        hs_ce, "DELETE", strlen("DELETE"), HS_MODIFY_REMOVE TSRMLS_CC);
#endif

    /* HandlerSocketIndex class */
    INIT_CLASS_ENTRY(
        ce, "HandlerSocketIndex", (zend_function_entry *)hs_index_methods);
    hs_index_ce = zend_register_internal_class(&ce TSRMLS_CC);
    hs_index_ce->create_object = hs_index_new;

    /* property */
    zend_declare_property_null(
        hs_index_ce, "_db", strlen("_db"),
        ZEND_ACC_PROTECTED TSRMLS_CC);
    zend_declare_property_null(
        hs_index_ce, "_table", strlen("_table"),
        ZEND_ACC_PROTECTED TSRMLS_CC);
    zend_declare_property_null(
        hs_index_ce, "_name", strlen("_name"),
        ZEND_ACC_PROTECTED TSRMLS_CC);
    zend_declare_property_null(
        hs_index_ce, "_field", strlen("_field"),
        ZEND_ACC_PROTECTED TSRMLS_CC);

    /* HandlerSocketException class */
    INIT_CLASS_ENTRY(ce, "HandlerSocketException", NULL);
    hs_exception_ce = zend_register_internal_class_ex(
        &ce, (zend_class_entry*)zend_exception_get_default(TSRMLS_C),
        NULL TSRMLS_CC);

    return SUCCESS;
}

/*
PHP_MSHUTDOWN_FUNCTION(handlersocket)
{
    return SUCCESS;
}

PHP_RINIT_FUNCTION(handlersocket)
{
    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(handlersocket)
{
    return SUCCESS;
}
*/

PHP_MINFO_FUNCTION(handlersocket)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "MySQL HandlerSocket support", "enabled");
    php_info_print_table_row(
        2, "extension Version", HANDLERSOCKET_EXTENSION_VERSION);
    php_info_print_table_end();
}

zend_module_entry handlersocket_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
    STANDARD_MODULE_HEADER,
#endif
    "handlersocket",
    NULL,
    PHP_MINIT(handlersocket),
    NULL,
    NULL,
    NULL,
    PHP_MINFO(handlersocket),
#if ZEND_MODULE_API_NO >= 20010901
    HANDLERSOCKET_EXTENSION_VERSION,
#endif
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_HANDLERSOCKET
ZEND_GET_MODULE(handlersocket)
#endif

static zval* hs_zval_search_key(zval *value, zval *array TSRMLS_DC)
{
    zval *return_value, **entry, res;
    HashPosition pos;
    HashTable *ht;
    ulong index;
    uint key_len;
    char *key;
    int (*is_equal_func)(zval *, zval *, zval * TSRMLS_DC) = is_equal_function;

    MAKE_STD_ZVAL(return_value);

    ht = HASH_OF(array);
    zend_hash_internal_pointer_reset_ex(ht, &pos);
    while (zend_hash_get_current_data_ex(ht, (void **)&entry, &pos) == SUCCESS)
    {
        is_equal_func(&res, value, *entry TSRMLS_CC);
        if (Z_LVAL(res))
        {
            switch (zend_hash_get_current_key_ex(
                        ht, &key, &key_len, &index, 0, &pos))
            {
                case HASH_KEY_IS_STRING:
                    ZVAL_STRINGL(return_value, key, key_len - 1, 1);
                    break;
                case HASH_KEY_IS_LONG:
                    ZVAL_LONG(return_value, index);
                    break;
                default:
                    ZVAL_NULL(return_value);
                    break;
            }

            return return_value;
        }
        zend_hash_move_forward_ex(ht, &pos);
    }

    ZVAL_NULL(return_value);

    return return_value;
}

static int hs_zval_to_operate_criteria(
    zval *query, zval *operate, zval **criteria, char *defaults TSRMLS_DC)
{
    if (query == NULL)
    {
        return -1;
    }

    if (Z_TYPE_P(query) == IS_ARRAY)
    {
        char *key;
        uint key_len;
        ulong index;
        HashTable *ht;
        zval **tmp;

        ht = HASH_OF(query);

        if (zend_hash_get_current_data_ex(ht, (void **)&tmp, NULL) != SUCCESS)
        {
            return -1;
        }

        if (zend_hash_get_current_key_ex(
                ht, &key, &key_len, &index, 0, NULL) == HASH_KEY_IS_STRING)
        {
            ZVAL_STRINGL(operate, key, key_len - 1, 1);
            *criteria = *tmp;
        }
        else
        {
            ZVAL_STRINGL(operate, defaults, strlen(defaults), 1);
            *criteria = query;
        }
    }
    else
    {
        ZVAL_STRINGL(operate, defaults, strlen(defaults), 1);
        *criteria = query;
    }

    return 0;
}

static void hs_zval_to_filter(
    zval **return_value, zval *filter, zval *value, char *type TSRMLS_DC)
{
    HashTable *ht;
    HashPosition pos;
    zval **tmp, **ftmp, **vtmp, *index, *item;
    long n;

    if (value == NULL || Z_TYPE_P(value) != IS_ARRAY)
    {
        return;
    }

    ht = HASH_OF(value);
    n = zend_hash_num_elements(ht);

    if (n <= 0 ||
        zend_hash_index_find(ht, 0, (void **)&tmp) != SUCCESS)
    {
        return;
    }

    zend_hash_internal_pointer_reset_ex(ht, &pos);

    if (Z_TYPE_PP(tmp) == IS_ARRAY)
    {
        do
        {
            if (zend_hash_move_forward_ex(ht, &pos) < 0)
            {
                break;
            }

            hs_zval_to_filter(return_value, filter, *tmp, type TSRMLS_CC);
        }
        while (
            zend_hash_get_current_data_ex(ht, (void **)&tmp, &pos) == SUCCESS);

        return;
    }
    else if (n < 3)
    {
        return;
    }

    if (zend_hash_index_find(ht, 1, (void **)&ftmp) != SUCCESS)
    {
        return;
    }

    index = hs_zval_search_key(*ftmp, filter TSRMLS_CC);
    if (Z_TYPE_P(index) != IS_LONG)
    {
        zval_ptr_dtor(&index);
        return;
    }

    if (zend_hash_index_find(ht, 2, (void **)&vtmp) != SUCCESS)
    {
        zval_ptr_dtor(&index);
        return;
    }

    MAKE_STD_ZVAL(item);
    array_init(item);

    add_next_index_stringl(item, type, strlen(type), 1);

    convert_to_string(*tmp);
    add_next_index_stringl(item, Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp), 1);

    add_next_index_long(item, Z_LVAL_P(index));

    if (Z_TYPE_PP(vtmp) == IS_NULL)
    {
        add_next_index_null(item);
    }
    else if (Z_TYPE_PP(vtmp) == IS_LONG)
    {
        add_next_index_long(item, Z_LVAL_PP(vtmp));
    }
    else if (Z_TYPE_PP(vtmp) == IS_DOUBLE)
    {
        add_next_index_double(item, Z_DVAL_PP(vtmp));
    }
    else
    {
        convert_to_string(*tmp);
        add_next_index_stringl(item, Z_STRVAL_PP(vtmp), Z_STRLEN_PP(vtmp), 1);
    }

    if (!(*return_value))
    {
        MAKE_STD_ZVAL(*return_value);
        array_init(*return_value);
    }

    add_next_index_zval(*return_value, item);

    zval_ptr_dtor(&index);
}

static void hs_request_string(smart_str *buf, char *str, long str_len)
{
    long i;

    if (str_len <= 0)
    {
        return;
    }
    else
    {
        for (i = 0; i < str_len; i++)
        {
            if ((unsigned char)str[i] > HS_CODE_ESCAPE)
            {
                smart_str_appendc(buf, str[i]);
            }
            else
            {
                smart_str_appendc(buf, HS_CODE_ESCAPE_PREFIX);
                smart_str_appendc(
                    buf, (unsigned char)str[i] + HS_CODE_ESCAPE_ADD);
            }
        }
    }
}

static void hs_request_zval_scalar(smart_str *buf, zval *val, int delim)
{
    switch (Z_TYPE_P(val))
    {
        case IS_LONG:
            HS_REQUEST_LONG(buf, Z_LVAL_P(val));
            break;
        case IS_STRING:
            hs_request_string(buf, Z_STRVAL_P(val), Z_STRLEN_P(val));
            break;
        case IS_DOUBLE:
            convert_to_string(val);
            hs_request_string(buf, Z_STRVAL_P(val), Z_STRLEN_P(val));
            break;
        case IS_BOOL:
            convert_to_long(val);
            HS_REQUEST_LONG(buf, Z_LVAL_P(val));
            break;
        case IS_NULL:
            HS_REQUEST_NULL(buf);
            break;
        default:
            //IS_ARRAY
            //IS_OBJECT
            //IS_RESOURCE
            HS_REQUEST_LONG(buf, 0);
            break;
    }

    if (delim > 0)
    {
        HS_REQUEST_DELIM(buf);
    }
}

static void hs_request_array(
    smart_str *buf, HashTable *ht, int num, int i TSRMLS_DC)
{
    long n;
    HashPosition pos;
    zval **data;

    n = zend_hash_num_elements(ht);
    if (i > 0 && i < n)
    {
        n = i;
    }

    if (n == 0)
    {
        if (num == 1)
        {
            HS_REQUEST_LONG(buf, 1);
            HS_REQUEST_DELIM(buf);
        }
        HS_REQUEST_NULL(buf);
        return;
    }

    if (num == 1)
    {
        HS_REQUEST_LONG(buf, n);
        HS_REQUEST_DELIM(buf);
    }

    zend_hash_internal_pointer_reset_ex(ht, &pos);
    while (zend_hash_get_current_data_ex(ht, (void **)&data, &pos) == SUCCESS)
    {
        if (n <= 0)
        {
            break;
        }
        n--;

        hs_request_zval_scalar(buf, *data, n);

        zend_hash_move_forward_ex(ht, &pos);
    }
}

static void hs_request_filter(smart_str *request, HashTable *ht TSRMLS_DC)
{
    zval **tmp;
    HashPosition pos;
    long n, i = 0;

    n = zend_hash_num_elements(ht);
    if (n >= 0)
    {
        HS_REQUEST_DELIM(request);

        zend_hash_internal_pointer_reset_ex(ht, &pos);
        while (zend_hash_get_current_data_ex(ht, (void **)&tmp, &pos) == SUCCESS)
        {
            switch ((*tmp)->type)
            {
                case IS_STRING:
                    hs_request_string(
                        request, Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp));
                    break;
                case IS_LONG:
                    HS_REQUEST_LONG(request, Z_LVAL_PP(tmp));
                    break;
                default:
                    convert_to_string(*tmp);
                    hs_request_string(
                        request, Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp));
                    break;
            }

            if (++i != n)
            {
                hs_request_string(request, ",", strlen(","));
            }

            zend_hash_move_forward_ex(ht, &pos);
        }
    }
}

static void hs_array_to_in_filter(
    HashTable *ht, zval *filter, zval **filters,
    long *in_key, zval **in_values TSRMLS_DC)
{
    HashPosition pos;
    zval **val;

    char *key;
    ulong key_index;
    uint key_len;

    zend_hash_internal_pointer_reset_ex(ht, &pos);
    while (zend_hash_get_current_data_ex(ht, (void **)&val, &pos) == SUCCESS)
    {
        if (zend_hash_get_current_key_ex(
                ht, &key, &key_len, &key_index, 0, &pos) != HASH_KEY_IS_STRING)
        {
            zend_hash_move_forward_ex(ht, &pos);
            continue;
        }

        if (strcmp(key, "in") == 0)
        {
            /* in */
            if (Z_TYPE_PP(val) == IS_ARRAY)
            {
                HashTable *in_ht;
                HashPosition in_pos;
                zval **tmp;

                char *in_key_name;
                ulong in_key_index;
                uint in_key_len;

                in_ht = HASH_OF(*val);

                zend_hash_internal_pointer_reset_ex(in_ht, &in_pos);
                if (zend_hash_get_current_data_ex(
                        in_ht, (void **)&tmp, &in_pos) == SUCCESS)
                {
                    if (Z_TYPE_PP(tmp) == IS_ARRAY)
                    {
                        switch (zend_hash_get_current_key_ex(
                                    in_ht, &in_key_name, &in_key_len,
                                    &in_key_index, 0, &in_pos))
                        {
                            case HASH_KEY_NON_EXISTANT:
                                *in_key = 0;
                                break;
                            case HASH_KEY_IS_LONG:
                                *in_key = in_key_index;
                                break;
                            default:
                            {
                                zval *key;
                                MAKE_STD_ZVAL(key);
                                ZVAL_STRINGL(key, in_key_name, in_key_len, 1);
                                convert_to_long(key);
                                *in_key = Z_LVAL_P(key);
                                zval_ptr_dtor(&key);
                                break;

                            }
                        }
                        *in_values = *tmp;
                    }
                    else
                    {
                        *in_key = 0;
                        *in_values = *val;
                    }
                }
            }
            else
            {
                *in_key = 0;
                *in_values = *val;
            }
        }
        else if (strcmp(key, "filter") == 0 && filter != NULL)
        {
            /* filter */
            hs_zval_to_filter(
                filters, filter, *val, HS_PROTOCOL_FILTER TSRMLS_CC);
        }
        else if (strcmp(key, "while") == 0 && filter != NULL)
        {
            /* while */
            hs_zval_to_filter(
                filters, filter, *val, HS_PROTOCOL_WHILE TSRMLS_CC);
        }

        zend_hash_move_forward_ex(ht, &pos);
    }
}

static void hs_request_find(
    smart_str *buf, long id,
    zval * operate, zval *criteria, long limit, long offset,
    zval *filters, long in_key, zval *in_values TSRMLS_DC)
{
    HS_REQUEST_LONG(buf, id);
    HS_REQUEST_DELIM(buf);

    convert_to_string(operate);
    hs_request_string(buf, Z_STRVAL_P(operate), Z_STRLEN_P(operate));
    HS_REQUEST_DELIM(buf);

    if (Z_TYPE_P(criteria) == IS_ARRAY)
    {
        hs_request_array(buf, HASH_OF(criteria), 1, -1 TSRMLS_CC);
    }
    else
    {
        HS_REQUEST_LONG(buf, 1);
        HS_REQUEST_DELIM(buf);

        hs_request_zval_scalar(buf, criteria, 0);
    }

    HS_REQUEST_DELIM(buf);
    HS_REQUEST_LONG(buf, limit);

    HS_REQUEST_DELIM(buf);
    HS_REQUEST_LONG(buf, offset);

    if (in_key >= 0 && in_values != NULL)
    {
        HS_REQUEST_DELIM(buf);

        hs_request_string(buf, HS_PROTOCOL_IN, 1);
        HS_REQUEST_DELIM(buf);

        HS_REQUEST_LONG(buf, in_key);
        HS_REQUEST_DELIM(buf);

        if (Z_TYPE_P(in_values) == IS_ARRAY)
        {
            hs_request_array(buf, HASH_OF(in_values), 1, -1 TSRMLS_CC);
        }
        else
        {
            hs_request_zval_scalar(buf, in_values, 0);
        }
    }

    if (filters != NULL && Z_TYPE_P(filters) == IS_ARRAY)
    {
        HashTable *ht;
        HashPosition pos;
        zval **tmp;

        ht = HASH_OF(filters);

        zend_hash_internal_pointer_reset_ex(ht, &pos);
        while (zend_hash_get_current_data_ex(ht, (void **)&tmp, &pos) == SUCCESS)
        {
            if (Z_TYPE_PP(tmp) != IS_ARRAY ||
                zend_hash_num_elements(HASH_OF(*tmp)) < 4)
            {
                zend_hash_move_forward_ex(ht, &pos);
                continue;
            }

            HS_REQUEST_DELIM(buf);

            hs_request_array(buf, HASH_OF(*tmp), -1, 4 TSRMLS_CC);

            zend_hash_move_forward_ex(ht, &pos);
        }
    }
}

static void hs_request_find_execute(
    smart_str *buf, long id,
    zval * operate, zval *criteria,
    long limit, long offset,
    zval *filter, long in_key, zval *in_values TSRMLS_DC)
{
    HS_REQUEST_LONG(buf, id);
    HS_REQUEST_DELIM(buf);

    convert_to_string(operate);
    hs_request_string(buf, Z_STRVAL_P(operate), Z_STRLEN_P(operate));
    HS_REQUEST_DELIM(buf);

    if (Z_TYPE_P(criteria) == IS_ARRAY)
    {
        hs_request_array(buf, HASH_OF(criteria), 1, -1 TSRMLS_CC);
    }
    else
    {
        HS_REQUEST_LONG(buf, 1);
        HS_REQUEST_DELIM(buf);

        hs_request_zval_scalar(buf, criteria, 0);
    }

    HS_REQUEST_DELIM(buf);
    HS_REQUEST_LONG(buf, limit);

    HS_REQUEST_DELIM(buf);
    HS_REQUEST_LONG(buf, offset);

    /* in */
    if (in_key >= 0 && in_values != NULL)
    {
        HS_REQUEST_DELIM(buf);
        hs_request_string(buf, HS_PROTOCOL_IN, 1);

        HS_REQUEST_DELIM(buf);
        HS_REQUEST_LONG(buf, in_key);

        if (Z_TYPE_P(in_values) == IS_ARRAY)
        {
            HashTable *ht;
            HashPosition pos;
            zval **val;

            ht = HASH_OF(in_values);

            HS_REQUEST_DELIM(buf);
            HS_REQUEST_LONG(buf, zend_hash_num_elements(ht));

            zend_hash_internal_pointer_reset_ex(ht, &pos);
            while (zend_hash_get_current_data_ex(
                       ht, (void **)&val, &pos) == SUCCESS)
            {
                if (Z_TYPE_PP(val) == IS_ARRAY)
                {
                    HS_REQUEST_DELIM(buf);
                    hs_request_array(buf, HASH_OF(*val), -1, -1 TSRMLS_CC);
                }
                else
                {
                    HS_REQUEST_DELIM(buf);
                    hs_request_zval_scalar(buf, *val, 0);
                }

                zend_hash_move_forward_ex(ht, &pos);
            }
        }
        else
        {
            HS_REQUEST_DELIM(buf);
            HS_REQUEST_LONG(buf, 1);

            HS_REQUEST_DELIM(buf);
            hs_request_zval_scalar(buf, in_values, 0);
        }
    }

    /* filter */
    if (filter != NULL && Z_TYPE_P(filter) == IS_ARRAY)
    {
        HashTable *ht;
        HashPosition pos;
        zval **val, **tmp;

        ht = HASH_OF(filter);

        zend_hash_internal_pointer_reset_ex(ht, &pos);
        while(zend_hash_get_current_data_ex(ht, (void **)&val, &pos) == SUCCESS)
        {
            if (Z_TYPE_PP(val) != IS_ARRAY ||
                zend_hash_num_elements(HASH_OF(*val)) < 4)
            {
                zend_hash_move_forward_ex(ht, &pos);
                continue;
            }

            if (zend_hash_index_find(
                    HASH_OF(*val), 0, (void **)&tmp) == SUCCESS)
            {
                HS_REQUEST_DELIM(buf);
                hs_request_string(buf, Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp));
            }

            if (zend_hash_index_find(
                    HASH_OF(*val), 1, (void **)&tmp) == SUCCESS)
            {
                HS_REQUEST_DELIM(buf);
                hs_request_string(buf, Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp));
            }

            if (zend_hash_index_find(
                    HASH_OF(*val), 2, (void **)&tmp) == SUCCESS)
            {
                HS_REQUEST_DELIM(buf);
                hs_request_zval_scalar(buf, *tmp, 0);
            }

            if (zend_hash_index_find(
                    HASH_OF(*val), 3, (void **)&tmp) == SUCCESS)
            {
                HS_REQUEST_DELIM(buf);

                if (Z_TYPE_PP(tmp) == IS_ARRAY)
                {
                    hs_request_array(buf, HASH_OF(*tmp), 1, 0 TSRMLS_CC);
                }
                else
                {
                    hs_request_zval_scalar(buf, *tmp, 0);
                }
            }

            zend_hash_move_forward_ex(ht, &pos);
        }
    }
}

static int hs_request_find_modify(
    smart_str *buf, zval *update, zval *values, long field TSRMLS_DC)
{
    int ret = -1;
    long len;

    if (update == NULL || Z_TYPE_P(update) != IS_STRING)
    {
        return -1;
    }

    len = Z_STRLEN_P(update);
    if (len == 1)
    {
        ret = 1;
    }
    else if (len == 2)
    {
        ret = 0;
    }
    else
    {
        return -1;
    }

    if (values == NULL)
    {
        HS_REQUEST_DELIM(buf);
        hs_request_string(buf, Z_STRVAL_P(update), Z_STRLEN_P(update));

        HS_REQUEST_DELIM(buf);
        HS_REQUEST_NULL(buf);
    }
    else if (Z_TYPE_P(values) == IS_ARRAY)
    {
        if (field > 0 &&
            zend_hash_num_elements(HASH_OF(values)) < field)
        {
            return -1;
        }

        HS_REQUEST_DELIM(buf);
        hs_request_string(buf, Z_STRVAL_P(update), Z_STRLEN_P(update));

        HS_REQUEST_DELIM(buf);
        hs_request_array(buf, HASH_OF(values), 0, -1 TSRMLS_CC);
    }
    else
    {
        if (field > 0 && field != 1)
        {
            return -1;
        }

        HS_REQUEST_DELIM(buf);
        hs_request_string(buf, Z_STRVAL_P(update), Z_STRLEN_P(update));

        HS_REQUEST_DELIM(buf);
        hs_request_zval_scalar(buf, values, 0);
    }

    return ret;
}

static long hs_request_send(php_hs_t *hs, smart_str *request TSRMLS_DC)
{
    long ret;
#ifdef HS_DEBUG
    long i;
    smart_str debug = {0};

    if (request->len <= 0)
    {
        return -1;
    }

    for (i = 0; i < request->len; i++)
    {
        if ((unsigned char)request->c[i] == HS_CODE_NULL)
        {
            smart_str_appendl_ex(&debug, "\\0", strlen("\\0"), 1);
        }
        else
        {
            smart_str_appendc(&debug, request->c[i]);
        }
    }
    smart_str_0(&debug);

    zend_error(
        E_WARNING,
        "[handlersocket] (request) %ld : \"%s\"", request->len, debug.c);

    smart_str_free(&debug);
#endif

    ret = php_stream_write(hs->stream, request->c, request->len);

    return ret;
}

static long hs_response_select(php_hs_t *hs TSRMLS_DC)
{
    php_socket_t max_fd = 0;
    int retval, max_set_count = 0;
    struct timeval tv;
    struct timeval *tv_p = NULL;
    fd_set fds;

    FD_ZERO(&fds);

    if (php_stream_cast(
            hs->stream,
            PHP_STREAM_AS_FD_FOR_SELECT | PHP_STREAM_CAST_INTERNAL,
            (void*)&max_fd, 1) == SUCCESS &&
        max_fd != -1)
    {
        PHP_SAFE_FD_SET(max_fd, &fds);
        max_set_count++;
    }

    PHP_SAFE_MAX_FD(max_fd, max_set_count);

    if (hs->timeout > 0)
    {
        tv.tv_sec = hs->timeout;
        tv.tv_usec = 0;
        tv_p = &tv;
    }

    retval = php_select(max_fd + 1, &fds, NULL, NULL, tv_p);
    if (retval == -1)
    {
        zend_error(E_WARNING, "[handlersocket] unable to select");
        return -1;
    }

    if (!PHP_SAFE_FD_ISSET(max_fd, &fds))
    {
        return -1;
    }

    return 0;
}

static long hs_response_recv(php_hs_t *hs, char *recv, size_t size TSRMLS_DC)
{
    long ret;
#ifdef HS_DEBUG
    long i;
    smart_str debug = {0};
#endif

    ret  = php_stream_read(hs->stream, recv, size);
    if (ret <= 0)
    {
        return -1;
    }

#ifdef HS_DEBUG
    for (i = 0; i < ret; i++)
    {
        if ((unsigned char)recv[i] == HS_CODE_NULL)
        {
            smart_str_appendl_ex(&debug, "\\0", strlen("\\0"), 1);
        }
        else
        {
            smart_str_appendc(&debug, recv[i]);
        }
    }
    smart_str_0(&debug);

    zend_error(
        E_WARNING, "[handlersocket] (recv) %ld : \"%s\"", ret, debug.c);

    smart_str_free(&debug);
#endif

    return ret;
}

static zval* hs_response_add(zval *return_value TSRMLS_DC)
{
    zval *value;
    MAKE_STD_ZVAL(value);
    array_init(value);
    add_next_index_zval(return_value, value);
    return value;
}

static zval* hs_response_zval(smart_str *buf TSRMLS_DC)
{
    zval *val;
    MAKE_STD_ZVAL(val);
    ZVAL_STRINGL(val, buf->c, buf->len, 1);
    return val;
}

static void hs_response_value(
    php_hs_t *hs, zval *error, zval *return_value, int modify TSRMLS_DC)
{
    char *recv;
    long i, j, len;
    zval *val, *item;

    smart_str response = {0};
    long n = 0, block_size = HS_SOCKET_BLOCK_SIZE;
    int escape = 0, flag = 0, null = 0;
    long ret[2] = {-1, -1};

    if (hs_response_select(hs TSRMLS_CC) < 0)
    {
        ZVAL_BOOL(return_value, 0);
    }

    recv = emalloc(block_size + 1);
    len = hs_response_recv(hs, recv, block_size TSRMLS_CC);
    if (len <= 0)
    {
        efree(recv);
        ZVAL_BOOL(return_value, 0);
        return;
    }

    do
    {
        for (i = 0; i < len; i++)
        {
            if (recv[i] == HS_CODE_DELIMITER || recv[i] == HS_CODE_EOL)
            {
                val = hs_response_zval(&response TSRMLS_CC);
                convert_to_long(val);
                ret[flag] = Z_LVAL_P(val);
                flag++;
                zval_ptr_dtor(&val);
                smart_str_free(&response);
            }
            else
            {
                smart_str_appendc(&response, recv[i]);
            }

            if (flag > 1)
            {
                break;
            }
        }

        if (flag > 1)
        {
            break;
        }
        else
        {
            i = 0;
            len = hs_response_recv(hs, recv, block_size TSRMLS_CC);
            if (len <= 0)
            {
                break;
            }
        }
    } while (1);

    if (ret[0] != 0)
    {
        if (recv[i] != HS_CODE_EOL)
        {
            smart_str err = {0};

            i++;

            if (i > len)
            {
                i = 0;
                len = -1;
            }

            do
            {
                for (j = i; j < len; j++)
                {
                    if (recv[j] == HS_CODE_EOL)
                    {
                        break;
                    }

                    if (recv[j] == HS_CODE_ESCAPE_PREFIX)
                    {
                        escape = 1;
                    }
                    else if (escape)
                    {
                        escape = 0;
                        smart_str_appendc(
                            &err, (unsigned char)recv[j] - HS_CODE_ESCAPE_ADD);
                    }
                    else
                    {
                        smart_str_appendc(&err, recv[j]);
                    }
                }

                if (recv[j] == HS_CODE_EOL)
                {
                    break;
                }

                i = 0;
            } while ((len = hs_response_recv(
                          hs, recv, block_size TSRMLS_CC)) > 0);

            if (error)
            {
                ZVAL_STRINGL(error, err.c, err.len, 1);
            }

            smart_str_free(&err);
        }
        else if (error)
        {
            ZVAL_NULL(error);
        }

        efree(recv);
        ZVAL_BOOL(return_value, 0);

        return;
    }

    if (ret[1] == 1 && recv[i] == HS_CODE_EOL)
    {
        efree(recv);
        ZVAL_BOOL(return_value, 1);
        return;
    }

    i++;

    if (i > len)
    {
        i = 0;
        len = -1;
    }

    if (modify)
    {
        if (i > 0 && recv[i-1] == HS_CODE_EOL)
        {
            efree(recv);
            ZVAL_LONG(return_value, 0);
            return;
        }

        do
        {
            for (j = i; j < len; j++)
            {
                if (recv[j] == HS_CODE_EOL)
                {
                    ZVAL_STRINGL(return_value, response.c, response.len, 1);
                    break;
                }

                if (recv[j] == HS_CODE_ESCAPE_PREFIX)
                {
                    escape = 1;
                }
                else if (escape)
                {
                    escape = 0;
                    smart_str_appendc(
                        &response, (unsigned char)recv[j] - HS_CODE_ESCAPE_ADD);
                }
                else
                {
                    smart_str_appendc(&response, recv[j]);
                }
            }

            if (recv[j] == HS_CODE_EOL)
            {
                break;
            }
            i = 0;
        } while ((len = hs_response_recv(hs, recv, block_size TSRMLS_CC)) > 0);

        convert_to_long(return_value);
    }
    else
    {
        array_init(return_value);

        if (i > 0 && recv[i-1] == HS_CODE_EOL)
        {
            efree(recv);
            return;
        }

        item = hs_response_add(return_value TSRMLS_CC);

        do
        {
            for (j = i; j < len; j++)
            {
                if (recv[j] == HS_CODE_DELIMITER)
                {
                    if (response.len == 0 && null == 1)
                    {
                        add_next_index_null(item);
                    }
                    else
                    {
                        add_next_index_stringl(
                            item, response.c, response.len, 1);
                    }

                    n++;
                    null = 0;
                    if (n == ret[1])
                    {
                        item = hs_response_add(return_value TSRMLS_CC);
                        n = 0;
                    }

                    smart_str_free(&response);

                    continue;
                }
                else if (recv[j] == HS_CODE_EOL)
                {
                    if (response.len == 0 && null == 1)
                    {
                        add_next_index_null(item);
                    }
                    else
                    {
                        add_next_index_stringl(
                            item, response.c, response.len, 1);
                    }
                    null = 0;
                    break;
                }

                if (recv[j] == HS_CODE_ESCAPE_PREFIX)
                {
                    escape = 1;
                }
                else if (escape)
                {
                    escape = 0;
                    smart_str_appendc(
                        &response, (unsigned char)recv[j] - HS_CODE_ESCAPE_ADD);
                }
                else if (recv[j] == HS_CODE_NULL)
                {
                    null = 1;
                }
                else
                {
                    smart_str_appendc(&response, recv[j]);
                }
            }

            if (recv[j] == HS_CODE_EOL)
            {
                break;
            }
            i = 0;
        } while ((len = hs_response_recv(hs, recv, block_size TSRMLS_CC)) > 0);
    }

    efree(recv);

    smart_str_free(&response);
}

static void hs_response_multi(
    php_hs_t *hs, zval *return_value, zval *rmodify, zval *error TSRMLS_DC)
{
    char *recv;
    long i, len, count;
    long current = 0;
    smart_str response = {0};
    long block_size = HS_SOCKET_BLOCK_SIZE;

    if (hs_response_select(hs TSRMLS_CC) < 0)
    {
        ZVAL_BOOL(return_value, 0);
    }

    recv = emalloc(block_size + 1);
    len = hs_response_recv(hs, recv, block_size TSRMLS_CC);
    if (len <= 0)
    {
        efree(recv);
        RETVAL_BOOL(0);
    }

    count = zend_hash_num_elements(HASH_OF(rmodify));

    array_init(return_value);

    array_init(error);

    for(i = 0; i < count; i++)
    {
        long j, k;
        zval *rval, *item, *val, **tmp;

        int flag = 0, escape = 0, null = 0;
        long n = 0, modify = 0;
        long ret[2] = {-1, -1};

        if (zend_hash_index_find(
                HASH_OF(rmodify), i, (void **)&tmp) == SUCCESS)
        {
            modify = Z_LVAL_PP(tmp);
        }

        smart_str_free(&response);

        do
        {
            for (j = current; j < len; j++)
            {
                if (recv[j] == HS_CODE_DELIMITER || recv[j] == HS_CODE_EOL)
                {
                    rval = hs_response_zval(&response TSRMLS_CC);
                    convert_to_long(rval);
                    ret[flag] = Z_LVAL_P(rval);
                    flag++;
                    zval_ptr_dtor(&rval);
                    smart_str_free(&response);
                }
                else
                {
                    smart_str_appendc(&response, recv[j]);
                }

                if (flag > 1)
                {
                    break;
                }
            }

            if (flag > 1)
            {
                break;
            }
            else
            {
                j = 0;
                current = 0;
                len = hs_response_recv(hs, recv, block_size TSRMLS_CC);
                if (len <= 0)
                {
                    break;
                }
            }
        } while (1);


        if (ret[0] != 0)
        {
            if (recv[j] != HS_CODE_EOL)
            {
                smart_str err = {0};

                j++;

                if (j > len)
                {
                    j = 0;
                    current = 0;
                    len = -1;
                }

                do
                {
                    for (k = j; k < len; k++)
                    {
                        if (recv[k] == HS_CODE_EOL)
                        {
                            break;
                        }

                        if (recv[k] == HS_CODE_ESCAPE_PREFIX)
                        {
                            escape = 1;
                        }
                        else if (escape)
                        {
                            escape = 0;
                            smart_str_appendc(
                                &err,
                                (unsigned char)recv[k] - HS_CODE_ESCAPE_ADD);
                        }
                        else
                        {
                            smart_str_appendc(&err, recv[k]);
                        }
                    }

                    if (recv[k] == HS_CODE_EOL)
                    {
                        current = k;
                        break;
                    }

                    j = 0;
                    current = 0;

                } while ((len = hs_response_recv(
                              hs, recv, block_size TSRMLS_CC)) > 0);

                add_next_index_stringl(error, err.c, err.len, 1);

                smart_str_free(&err);
            }
            else
            {
                add_next_index_null(error);
            }

            add_next_index_bool(return_value, 0);

            current++;

            continue;
        }

        add_next_index_null(error);

        if (ret[1] == 1 && recv[j] == HS_CODE_EOL)
        {
            add_next_index_bool(return_value, 1);

            current = j + 1;

            continue;
        }

        j++;

        if (j > len)
        {
            j = 0;
            current = 0;
            len = -1;
        }

        if (modify)
        {
            zval *num_z;

            if (j > 0 && recv[j-1] == HS_CODE_EOL)
            {
                current = j;

                add_next_index_long(return_value, 0);

                continue;
            }

            MAKE_STD_ZVAL(num_z);

            do
            {
                for (k = j; k < len; k++)
                {
                    if (recv[k] == HS_CODE_EOL)
                    {
                        ZVAL_STRINGL(num_z, response.c, response.len, 1);
                        break;
                    }

                    if (recv[k] == HS_CODE_ESCAPE_PREFIX)
                    {
                        escape = 1;
                    }
                    else if (escape)
                    {
                        escape = 0;
                        smart_str_appendc(
                            &response,
                            (unsigned char)recv[k] - HS_CODE_ESCAPE_ADD);
                    }
                    else
                    {
                        smart_str_appendc(&response, recv[k]);
                    }
                }

                if (recv[k] == HS_CODE_EOL)
                {
                    current = k;
                    break;
                }

                j = 0;
                current = 0;

            } while ((len = hs_response_recv(
                          hs, recv, block_size TSRMLS_CC)) > 0);

            convert_to_long(num_z);

            add_next_index_long(return_value, Z_LVAL_P(num_z));

            zval_ptr_dtor(&num_z);
        }
        else
        {
            item = hs_response_add(return_value TSRMLS_CC);

            if (j > 0 && recv[j-1] == HS_CODE_EOL)
            {
                current = j;

                continue;
            }

            val = hs_response_add(item TSRMLS_CC);

            do
            {
                for (k = j; k < len; k++)
                {
                    if (recv[k] == HS_CODE_DELIMITER)
                    {
                        if (response.len == 0 && null == 1)
                        {
                            add_next_index_null(val);
                        }
                        else
                        {
                            add_next_index_stringl(
                                val, response.c, response.len, 1);
                        }

                        null = 0;
                        n++;
                        if (n == ret[1])
                        {
                            val = hs_response_add(item TSRMLS_CC);
                            n = 0;
                        }

                        smart_str_free(&response);

                        continue;
                    }
                    else if (recv[k] == HS_CODE_EOL)
                    {
                        if (response.len == 0 && null == 1)
                        {
                            add_next_index_null(val);
                        }
                        else
                        {
                            add_next_index_stringl(
                                val, response.c, response.len, 1);
                        }
                        null = 0;
                        break;
                    }

                    if (recv[k] == HS_CODE_ESCAPE_PREFIX)
                    {
                        escape = 1;
                    }
                    else if (escape)
                    {
                        escape = 0;
                        smart_str_appendc(
                            &response,
                            (unsigned char)recv[k] - HS_CODE_ESCAPE_ADD);
                    }
                    else if (recv[k] == HS_CODE_NULL)
                    {
                        null = 1;
                    }
                    else
                    {
                        smart_str_appendc(&response, recv[k]);
                    }
                }

                if (recv[k] == HS_CODE_EOL)
                {
                    current = k;
                    break;
                }

                j = 0;
                current = 0;

            } while ((len = hs_response_recv(
                          hs, recv, block_size TSRMLS_CC)) > 0);
        }

        current++;
    }

    efree(recv);

    smart_str_free(&response);
}

static int hs_is_options_safe(HashTable *options TSRMLS_DC)
{
    zval **tmp;

    if (zend_hash_find(
            options, "safe", sizeof("safe"), (void**)&tmp) == SUCCESS)
    {
        if (Z_TYPE_PP(tmp) == IS_STRING ||
            ((Z_TYPE_PP(tmp) == IS_LONG || Z_TYPE_PP(tmp) == IS_BOOL) &&
             Z_LVAL_PP(tmp) >= 1))
        {
            return 1;
        }
    }

    return 0;
}

/* HandlerSocket Class */
static void hs_free(php_hs_t *hs TSRMLS_DC)
{
    if (hs)
    {
        if (hs->stream)
        {
            php_stream_close(hs->stream);
        }

        if (hs->server)
        {
            zval_ptr_dtor(&hs->server);
        }

        if (hs->auth)
        {
            zval_ptr_dtor(&hs->auth);
        }

        if (hs->error)
        {
            zval_ptr_dtor(&hs->error);
        }

        zend_object_std_dtor(&hs->object TSRMLS_CC);

        efree(hs);
    }
}

static zend_object_value hs_new(zend_class_entry *ce TSRMLS_DC)
{
    zend_object_value retval;
    zval *tmp;
    php_hs_t *hs;

    hs = (php_hs_t *)emalloc(sizeof(php_hs_t));

    zend_object_std_init(&hs->object, ce TSRMLS_CC);
#if PHP_VERSION_ID < 50399
    zend_hash_copy(
        hs->object.properties, &ce->default_properties,
        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
#else
    object_properties_init(&hs->object, ce);
#endif

    retval.handle = zend_objects_store_put(
        hs, (zend_objects_store_dtor_t)zend_objects_destroy_object,
        (zend_objects_free_object_storage_t)hs_free,
        NULL TSRMLS_CC);
    retval.handlers = zend_get_std_object_handlers();

    hs->stream = NULL;
    hs->server = NULL;
    hs->auth = NULL;
    hs->error = NULL;

    return retval;
}

static ZEND_METHOD(HandlerSocket, __construct)
{
    char *host, *port, *server = NULL;
    int host_len, port_len, server_len;
    zval *options = NULL;

    char *hashkey = NULL, *errstr = NULL;
    int err;
    struct timeval tv;

    php_hs_t *hs;

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    MAKE_STD_ZVAL(hs->server);
    hs->timeout = FG(default_socket_timeout);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "ss|z",
            &host, &host_len, &port, &port_len, &options) == FAILURE)
    {
        return;
    }

    if (strlen(host) == 0 || strlen(port) == 0)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] no server name or port given");
        RETURN_FALSE;
    }

    if (options && Z_TYPE_P(options) == IS_ARRAY)
    {
        zval **tmp;

        if (zend_hash_find(
                Z_ARRVAL_P(options), "timeout", sizeof("timeout"),
                (void **)&tmp) == SUCCESS)
        {
            convert_to_long_ex(tmp);
            hs->timeout = Z_LVAL_PP(tmp);
        }
    }

    server_len = spprintf(&server, 0, "%s:%s", host, port);
    ZVAL_STRINGL(hs->server, server, server_len, 1);
    efree(server);

    if (hs->timeout > 0)
    {
        tv.tv_sec = hs->timeout;
        tv.tv_usec = 0;
    }

    hs->stream = php_stream_xport_create(
        Z_STRVAL_P(hs->server), Z_STRLEN_P(hs->server),
        ENFORCE_SAFE_MODE | REPORT_ERRORS,
        STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT,
        hashkey, &tv, NULL, &errstr, &err);
    if (!hs->stream)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to connect %s: %s",
            Z_STRVAL_P(hs->server),
            errstr == NULL ? "Unknown error" : errstr);
        RETURN_FALSE;
    }

    if (hashkey)
    {
        efree(hashkey);
    }

    if (errstr)
    {
        efree(errstr);
    }

    /* non-blocking */
    if (php_stream_set_option(
            hs->stream, PHP_STREAM_OPTION_BLOCKING, 0, NULL) == -1)
    {
        zend_error(
            E_WARNING, "[handlersocket] Un set non-blocking mode on a stream");
    }
}

static ZEND_METHOD(HandlerSocket, auth)
{
    long key_len, type_len;
    char *key, *type;

    zval *retval;
    smart_str request = {0};

    php_hs_t *hs;

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "s|s",
            &key, &key_len, &type, &type_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (key_len <= 0)
    {
        RETURN_FALSE;
    }

    if (!hs->stream)
    {
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(hs->auth);
    ZVAL_STRINGL(hs->auth, key, key_len, 1);

    MAKE_STD_ZVAL(retval);

    hs_request_string(&request, HS_PROTOCOL_AUTH, 1);
    HS_REQUEST_DELIM(&request);
    hs_request_string(&request, "1", 1);
    HS_REQUEST_DELIM(&request);
    hs_request_string(&request, Z_STRVAL_P(hs->auth), Z_STRLEN_P(hs->auth));
    HS_REQUEST_NEXT(&request);

    /* request: send */
    if (hs_request_send(hs, &request TSRMLS_CC) >= 0)
    {
        /* response */
        hs_response_value(hs, NULL, retval, 0 TSRMLS_CC);

        if (Z_TYPE_P(retval) == IS_BOOL &&
            Z_LVAL_P(retval) == 1)
        {
            ZVAL_BOOL(return_value, 1);
        }
        else
        {
            ZVAL_BOOL(return_value, 0);
        }
    }
    else
    {
        ZVAL_BOOL(return_value, 0);
    }

    smart_str_free(&request);

    zval_ptr_dtor(&retval);
}

static ZEND_METHOD(HandlerSocket, openIndex)
{
    long id;
    char *db, *table, *index;
    int db_len, table_len, index_len;
    zval *field = NULL, *filters = NULL;
    smart_str request = {0}, request_field = {0};

    php_hs_t *hs;

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "lsssz|z",
            &id, &db, &db_len, &table, &table_len,
            &index, &index_len, &field, &filters) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!hs->stream)
    {
        RETURN_FALSE;
    }

    if (Z_TYPE_P(field) == IS_ARRAY)
    {
        zval **tmp;
        HashPosition pos;
        long n;

        n = zend_hash_num_elements(HASH_OF(field));
        if (n >= 0)
        {
            zend_hash_internal_pointer_reset_ex(HASH_OF(field), &pos);
            while (zend_hash_get_current_data_ex(
                       HASH_OF(field), (void **)&tmp, &pos) == SUCCESS)
            {
                switch ((*tmp)->type)
                {
                    case IS_STRING:
                        smart_str_appendl_ex(
                            &request_field,
                            Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp), 1);
                        break;
                    default:
                        convert_to_string(*tmp);
                        smart_str_appendl_ex(
                            &request_field,
                            Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp), 1);
                        break;
                }

                smart_str_appendl_ex(&request_field, ",", strlen(","), 1);

                zend_hash_move_forward_ex(HASH_OF(field), &pos);
            }

            request_field.len--;
            request_field.a--;
        }

    }
    else if (Z_TYPE_P(field) == IS_STRING)
    {
        smart_str_appendl_ex(
            &request_field, Z_STRVAL_P(field), Z_STRLEN_P(field), 1);
    }
    else
    {
        convert_to_string(field);
        smart_str_appendl_ex(
            &request_field, Z_STRVAL_P(field), Z_STRLEN_P(field), 1);
    }

    hs_request_string(&request, HS_PROTOCOL_OPEN, 1);
    HS_REQUEST_DELIM(&request);

    /* id */
    HS_REQUEST_LONG(&request, id);
    HS_REQUEST_DELIM(&request);

    /* db */
    hs_request_string(&request, db, db_len);
    HS_REQUEST_DELIM(&request);

    /* table */
    hs_request_string(&request, table, table_len);
    HS_REQUEST_DELIM(&request);

    /* index */
    hs_request_string(&request, index, index_len);
    HS_REQUEST_DELIM(&request);

    /* field */
    hs_request_string(&request, request_field.c, request_field.len);

    /* filter */
    if (filters)
    {
        if (Z_TYPE_P(filters) == IS_ARRAY)
        {
            hs_request_filter(&request, HASH_OF(filters) TSRMLS_CC);
        }
        else
        {
            HS_REQUEST_DELIM(&request);
            hs_request_zval_scalar(&request, filters, 0);
        }
    }

    /* eol */
    HS_REQUEST_NEXT(&request);

    /* request: send */
    if (hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        smart_str_free(&request);
        RETURN_FALSE;
    }

    smart_str_free(&request);

    /* response */
    hs_response_value(hs, hs->error, return_value, 0 TSRMLS_CC);

    if (Z_TYPE_P(return_value) == IS_BOOL && Z_LVAL_P(return_value) == 0)
    {
        RETURN_FALSE;
    }
}

static ZEND_METHOD(HandlerSocket, executeSingle)
{
    long id, operate_len, update_len = 0, in_key = -1;
    long limit = 1, offset = 0;
    char *operate, *update = NULL;
    zval *criteria, *values = NULL, *filters = NULL, *in_values = NULL;
    zval *find_operate = NULL;

    php_hs_t *hs;

    smart_str request = {0};

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "lsz|llszzlz",
            &id, &operate, &operate_len, &criteria,
            &limit, &offset, &update, &update_len, &values, &filters,
            &in_key, &in_values) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!hs->stream)
    {
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(find_operate);
    ZVAL_STRINGL(find_operate, operate, operate_len, 1);

    /* find */
    hs_request_find_execute(
        &request, id, find_operate, criteria,
        limit, offset, filters, in_key, in_values TSRMLS_CC);

    /* find: modify */
    if (update_len > 0)
    {
        int modify, field;
        zval *find_update;

        MAKE_STD_ZVAL(find_update);
        ZVAL_STRINGL(find_update, update, update_len, 1);

        if (values != NULL && Z_TYPE_P(values) == IS_ARRAY)
        {
            field = zend_hash_num_elements(HASH_OF(values));
        }
        else
        {
            field = -1;
        }

        modify = hs_request_find_modify(
            &request, find_update, values, field TSRMLS_CC);
        if (modify >= 0)
        {
            /* eol */
            HS_REQUEST_NEXT(&request);

            /* request: send */
            if (hs_request_send(hs, &request TSRMLS_CC) < 0)
            {
                ZVAL_BOOL(return_value, 0);
            }
            else
            {
                /* response */
                hs_response_value(hs, hs->error, return_value, modify TSRMLS_CC);
            }
        }
        else
        {
            ZVAL_BOOL(return_value, 0);
            ZVAL_STRINGL(
                hs->error,
                "[handlersocket] unable to update parameter",
                strlen("[handlersocket] unable to update parameter"), 1);
        }

        zval_ptr_dtor(&find_update);
    }
    else
    {
        /* eol */
        HS_REQUEST_NEXT(&request);

        /* request: send */
        if (hs_request_send(hs, &request TSRMLS_CC) < 0)
        {
            ZVAL_BOOL(return_value, 0);
        }
        else
        {
            /* response */
            hs_response_value(hs, hs->error, return_value, 0 TSRMLS_CC);
        }
    }

    zval_ptr_dtor(&find_operate);

    smart_str_free(&request);
}

static ZEND_METHOD(HandlerSocket, executeMulti)
{
    zval *args = NULL;
    zval *rmodify;
    HashPosition pos;
    zval **val;

    smart_str request = {0};

    php_hs_t *hs;

    int err = -1;

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &args) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!hs->stream)
    {
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(rmodify);
    array_init(rmodify);

    zend_hash_internal_pointer_reset_ex(HASH_OF(args), &pos);
    while (zend_hash_get_current_data_ex(
               HASH_OF(args), (void **)&val, &pos) == SUCCESS)
    {
        HashTable *ht;
        long i, id, n, in_key = -1, limit = 1, offset = 0;
        zval **operate, **criteria, **tmp;
        zval *update = NULL, *values = NULL;
        zval *filters = NULL, *in_values = NULL;

        if (Z_TYPE_PP(val) != IS_ARRAY)
        {
            err = -1;
            break;
        }

        ht = HASH_OF(*val);

        /* 0: id */
        if (zend_hash_index_find(ht, 0, (void **)&tmp) != SUCCESS)
        {
            err = -1;
            break;
        }
        convert_to_long(*tmp);
        id = Z_LVAL_PP(tmp);

        /* 1: operate */
        if (zend_hash_index_find(ht, 1, (void **)&operate) != SUCCESS)
        {
            err = -1;
            break;
        }
        convert_to_string(*operate);

        /* 2: criteria */
        if (zend_hash_index_find(ht, 2, (void **)&criteria) != SUCCESS)
        {
            err = -1;
            break;
        }

        n = zend_hash_num_elements(ht);

        for (i = 3; i < n; i++)
        {
            switch (i)
            {
                case 3:
                    /* 3: limit */
                    if (zend_hash_index_find(ht, 3, (void **)&tmp) == SUCCESS)
                    {
                        convert_to_long(*tmp);
                        limit = Z_LVAL_PP(tmp);
                    }
                    break;
                case 4:
                    /* 4: offset */
                    if (zend_hash_index_find(ht, 4, (void **)&tmp) == SUCCESS)
                    {
                        convert_to_long(*tmp);
                        offset = Z_LVAL_PP(tmp);
                    }
                    break;
                case 5:
                    /* 5: update */
                    if (zend_hash_index_find(ht, 5, (void **)&tmp) == SUCCESS)
                    {
                        update = *tmp;
                    }
                    break;
                case 6:
                    /* 6: values */
                    if (zend_hash_index_find(ht, 6, (void **)&tmp) == SUCCESS)
                    {
                        values = *tmp;
                    }
                    break;
                case 7:
                    /* 7: filters */
                    if (zend_hash_index_find(ht, 7, (void **)&tmp) == SUCCESS)
                    {
                        filters = *tmp;
                    }
                    break;
                case 8:
                    /* 8: in_key */
                    if (zend_hash_index_find(ht, 8, (void **)&tmp) == SUCCESS)
                    {
                        convert_to_long(*tmp);
                        in_key = Z_LVAL_PP(tmp);
                    }
                    break;
                case 9:
                    /* 9: in_values */
                    if (zend_hash_index_find(ht, 9, (void **)&tmp) == SUCCESS)
                    {
                        in_values = *tmp;
                    }
                    break;
                default:
                    break;
            }
        }

        /* find */
        hs_request_find_execute(
            &request, id, *operate, *criteria,
            limit, offset, filters, in_key, in_values TSRMLS_CC);

        /* find: modify */
        if (update != NULL && Z_TYPE_P(update) != IS_NULL)
        {
            int modify, field;

            if (values != NULL && Z_TYPE_P(values) == IS_ARRAY)
            {
                field = zend_hash_num_elements(HASH_OF(values));
            }
            else
            {
                field = -1;
            }

            modify = hs_request_find_modify(
                &request, update, values, field TSRMLS_CC);
            if (modify >= 0)
            {
                add_next_index_long(rmodify, modify);
            }
            else
            {
                err = -1;
                break;
            }
        }
        else
        {
            add_next_index_long(rmodify, 0);
        }

        /* eol */
        HS_REQUEST_NEXT(&request);

        err = 0;

        zend_hash_move_forward_ex(HASH_OF(args), &pos);
    }

    /* request: send */
    if (err < 0  || hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        smart_str_free(&request);
        zval_ptr_dtor(&rmodify);
        RETURN_FALSE;
    }
    smart_str_free(&request);

    /* response */
    hs_response_multi(hs, return_value, rmodify, hs->error TSRMLS_CC);

    zval_ptr_dtor(&rmodify);
}

static ZEND_METHOD(HandlerSocket, executeUpdate)
{
    long id, operate_len, in_key = -1;
    long limit = 1, offset = 0;
    char *operate;
    zval *criteria, *values = NULL, *filters = NULL, *in_values = NULL;
    zval *find_update, *find_operate = NULL;
    int modify;
    long field;

    php_hs_t *hs;

    smart_str request = {0};

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "lszz|llzlz",
            &id, &operate, &operate_len, &criteria,
            &values, &limit, &offset, &filters, &in_key, &in_values) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!hs->stream)
    {
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(find_operate);
    ZVAL_STRINGL(find_operate, operate, operate_len, 1);

    /* find */
    hs_request_find_execute(
        &request, id, find_operate, criteria,
        limit, offset, filters, in_key, in_values TSRMLS_CC);

    /* find: modify */
    MAKE_STD_ZVAL(find_update);
    ZVAL_STRINGL(find_update, HS_MODIFY_UPDATE, 1, 1);

    field = zend_hash_num_elements(HASH_OF(values));
    modify = hs_request_find_modify(
        &request, find_update, values, field TSRMLS_CC);
    if (modify >= 0)
    {
        /* eol */
        HS_REQUEST_NEXT(&request);

        /* request: send */
        if (hs_request_send(hs, &request TSRMLS_CC) < 0)
        {
            ZVAL_BOOL(return_value, 0);
        }
        else
        {
            /* response */
            hs_response_value(hs, hs->error, return_value, modify TSRMLS_CC);
        }
    }
    else
    {
        ZVAL_BOOL(return_value, 0);
        ZVAL_STRINGL(
            hs->error,
            "[handlersocket] unable to update parameter",
            strlen("[handlersocket] unable to update parameter"), 1);
    }

    zval_ptr_dtor(&find_operate);
    zval_ptr_dtor(&find_update);

    smart_str_free(&request);
}

static ZEND_METHOD(HandlerSocket, executeDelete)
{
    long id, operate_len, in_key = -1;
    long limit = 1, offset = 0;
    char *operate;
    zval *criteria, *values = NULL, *filters = NULL, *in_values = NULL;
    zval *find_update, *find_operate = NULL;
    int modify;

    php_hs_t *hs;

    smart_str request = {0};

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "lsz|llzlz",
            &id, &operate, &operate_len, &criteria,
            &limit, &offset, &filters, &in_key, &in_values) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!hs->stream)
    {
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(find_operate);
    ZVAL_STRINGL(find_operate, operate, operate_len, 1);

    /* find */
    hs_request_find_execute(
        &request, id, find_operate, criteria,
        limit, offset, filters, in_key, in_values TSRMLS_CC);

    /* find: modify */
    MAKE_STD_ZVAL(find_update);
    ZVAL_STRINGL(find_update, HS_MODIFY_REMOVE, 1, 1);

    MAKE_STD_ZVAL(values);
    ZVAL_NULL(values);

    modify = hs_request_find_modify(
        &request, find_update, values, -1 TSRMLS_CC);
    if (modify >= 0)
    {
        /* eol */
        HS_REQUEST_NEXT(&request);

        /* request: send */
        if (hs_request_send(hs, &request TSRMLS_CC) < 0)
        {
            ZVAL_BOOL(return_value, 0);
        }
        else
        {
            /* response */
            hs_response_value(hs, hs->error, return_value, modify TSRMLS_CC);
        }
    }
    else
    {
        ZVAL_BOOL(return_value, 0);
        ZVAL_STRINGL(
            hs->error,
            "[handlersocket] unable to update parameter",
            strlen("[handlersocket] unable to update parameter"), 1);
    }

    zval_ptr_dtor(&find_operate);
    zval_ptr_dtor(&find_update);
    zval_ptr_dtor(&values);

    smart_str_free(&request);
}

static ZEND_METHOD(HandlerSocket, executeInsert)
{
    long id;
    zval *operate, *field;

    php_hs_t *hs;

    smart_str request = {0};

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "lz", &id, &field) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (Z_TYPE_P(field) != IS_ARRAY ||
        zend_hash_num_elements(HASH_OF(field)) <= 0)
    {
        RETURN_FALSE;
    }

    if (!hs->stream)
    {
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(operate);
    ZVAL_STRINGL(operate, HS_PROTOCOL_INSERT, 1, 1);

    /* find */
    hs_request_find(
        &request, id, operate, field, 0, 0, NULL, -1, NULL TSRMLS_CC);

    /* eol */
    HS_REQUEST_NEXT(&request);

    /* request: send */
    if (hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        ZVAL_BOOL(return_value, 0);
    }
    else
    {
        /* response */
        hs_response_value(hs, hs->error, return_value, 1 TSRMLS_CC);
    }

    zval_ptr_dtor(&operate);
    smart_str_free(&request);
}

static ZEND_METHOD(HandlerSocket, getError)
{
    php_hs_t *hs;

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (hs->error == NULL)
    {
        RETURN_NULL();
    }
    else
    {
        RETVAL_ZVAL(hs->error, 1, 0);
    }
}

static ZEND_METHOD(HandlerSocket, createIndex)
{
    long id;
    char *db, *table, *index;
    int db_len, table_len, index_len;
    zval *field = NULL, *options = NULL;
    zval temp;
    zval *id_z, *db_z, *table_z, *index_z, *opt_z = NULL;

    php_hs_t *hs;

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);
    HS_ERROR_RESET(hs->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "lsssz|z",
            &id, &db, &db_len, &table, &table_len,
            &index, &index_len, &field, &options) == FAILURE)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] expects parameters");
        RETURN_FALSE;
    }

    if (options != NULL)
    {
        if (Z_TYPE_P(options) == IS_STRING)
        {
            MAKE_STD_ZVAL(opt_z);
            array_init(opt_z);
            add_assoc_zval(opt_z, "filter", options);
        }
    }

    MAKE_STD_ZVAL(id_z);
    MAKE_STD_ZVAL(db_z);
    MAKE_STD_ZVAL(table_z);
    MAKE_STD_ZVAL(index_z);

    ZVAL_LONG(id_z, id);
    ZVAL_STRINGL(db_z, db, db_len, 1);
    ZVAL_STRINGL(table_z, table, table_len, 1);
    ZVAL_STRINGL(index_z, index, index_len, 1);

    object_init_ex(return_value, hs_index_ce);

    if (options == NULL)
    {
        HS_METHOD6(
            HandlerSocketIndex, __construct, &temp, return_value,
            getThis(), id_z, db_z, table_z, index_z, field);
    }
    else if (opt_z != NULL)
    {
        HS_METHOD7(
            HandlerSocketIndex, __construct, &temp, return_value,
            getThis(), id_z, db_z, table_z, index_z, field, opt_z);
        zval_ptr_dtor(&opt_z);
    }
    else
    {
        HS_METHOD7(
            HandlerSocketIndex, __construct, &temp, return_value,
            getThis(), id_z, db_z, table_z, index_z, field, options);
    }

    zval_ptr_dtor(&id_z);
    zval_ptr_dtor(&db_z);
    zval_ptr_dtor(&table_z);
    zval_ptr_dtor(&index_z);
}

#if PHP_VERSION_ID < 50300
static ZEND_METHOD(HandlerSocket, close)
{
    php_hs_t *hs;

    hs = (php_hs_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (hs)
    {
        if (hs->stream)
        {
            php_stream_close(hs->stream);
        }
        hs->stream = NULL;

        if (hs->server)
        {
            zval_ptr_dtor(&hs->server);
        }
        hs->server = NULL;

        if (hs->auth)
        {
            zval_ptr_dtor(&hs->auth);
        }
        hs->auth = NULL;
    }
}
#endif

/* HandlerSocket Index Class */
static void hs_index_free(php_hs_index_t *hsi TSRMLS_DC)
{
    if (hsi)
    {
        if (hsi->link)
        {
            zval_ptr_dtor(&hsi->link);
        }

        if (hsi->filter)
        {
            zval_ptr_dtor(&hsi->filter);
        }

        if (hsi->error)
        {
            zval_ptr_dtor(&hsi->error);
        }

        zend_object_std_dtor(&hsi->object TSRMLS_CC);

        efree(hsi);
    }
}

static zend_object_value hs_index_new(zend_class_entry *ce TSRMLS_DC)
{
    zend_object_value retval;
    zval *tmp;
    php_hs_index_t *hsi;

    hsi = (php_hs_index_t *)emalloc(sizeof(php_hs_index_t));

    zend_object_std_init(&hsi->object, ce TSRMLS_CC);
#if PHP_VERSION_ID < 50399
    zend_hash_copy(
        hsi->object.properties, &ce->default_properties,
        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
#else
    object_properties_init(&hsi->object, ce);
#endif

    retval.handle = zend_objects_store_put(
        hsi, (zend_objects_store_dtor_t)zend_objects_destroy_object,
        (zend_objects_free_object_storage_t)hs_index_free,
        NULL TSRMLS_CC);
    retval.handlers = zend_get_std_object_handlers();

    hsi->id = 0;
    hsi->link = NULL;
    hsi->filter = NULL;
    hsi->error = NULL;

    return retval;
}

static ZEND_METHOD(HandlerSocketIndex, __construct)
{
    zval *link;
    long id;
    char *db, *table, *index;
    int db_len, table_len, index_len;
    zval *fields, *opts = NULL;

    smart_str request = {0}, request_field = {0};
    php_hs_t *hs;
    php_hs_index_t *hsi;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);
    HS_ERROR_RESET(hsi->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "Olsssz|z",
            &link, hs_ce, &id, &db, &db_len, &table, &table_len,
            &index, &index_len, &fields, &opts) == FAILURE)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] expects parameters");
        return;
    }

    hs = (php_hs_t *)zend_object_store_get_object(link TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (!hs->stream)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to open index: %ld", id);
        RETURN_FALSE;
    }

    hsi->link = link;
    zval_add_ref(&hsi->link);

    if (Z_TYPE_P(fields) == IS_ARRAY)
    {
        zval **tmp;
        HashTable *ht;
        HashPosition pos;

        ht = HASH_OF(fields);

        if (zend_hash_num_elements(ht) >= 0)
        {
            zend_hash_internal_pointer_reset_ex(ht, &pos);
            while (zend_hash_get_current_data_ex(
                       ht, (void **)&tmp, &pos) == SUCCESS)
            {
                switch ((*tmp)->type)
                {
                    case IS_STRING:
                        smart_str_appendl_ex(
                            &request_field,
                            Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp), 1);
                        break;
                    default:
                        convert_to_string(*tmp);
                        smart_str_appendl_ex(
                            &request_field,
                            Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp), 1);
                        break;
                }

                smart_str_appendl_ex(&request_field, ",", strlen(","), 1);

                zend_hash_move_forward_ex(ht, &pos);
            }

            request_field.len--;
            request_field.a--;
        }

    }
    else if (Z_TYPE_P(fields) == IS_STRING)
    {
        smart_str_appendl_ex(
            &request_field, Z_STRVAL_P(fields), Z_STRLEN_P(fields), 1);
    }
    else
    {
        convert_to_string(fields);
        smart_str_appendl_ex(
            &request_field, Z_STRVAL_P(fields), Z_STRLEN_P(fields), 1);
    }

    if (opts)
    {
        zval **tmp;

        convert_to_array(opts);
        if (zend_hash_find(
                HASH_OF(opts), "filter", sizeof("filter"),
                (void **)&tmp) == SUCCESS)
        {
            MAKE_STD_ZVAL(hsi->filter);

            if (Z_TYPE_PP(tmp) == IS_ARRAY)
            {
                long n, i;

                array_init(hsi->filter);

                n = zend_hash_num_elements(HASH_OF(*tmp));
                for (i = 0; i < n; i++)
                {
                    zval **val;
                    if (zend_hash_index_find(
                            HASH_OF(*tmp), i, (void **)&val) == SUCCESS)
                    {
                        convert_to_string(*val);
                        add_next_index_stringl(
                            hsi->filter, Z_STRVAL_PP(val), Z_STRLEN_PP(val), 1);
                    }
                }
            }
            else if (Z_TYPE_PP(tmp) == IS_STRING)
            {
                zval delim;

                ZVAL_STRINGL(&delim, ",", strlen(","), 0);

                array_init(hsi->filter);
                php_explode(&delim, *tmp, hsi->filter, LONG_MAX);
            }
            else
            {
                ZVAL_NULL(hsi->filter);
            }
        }
    }

    hs_request_string(&request, HS_PROTOCOL_OPEN, 1);
    HS_REQUEST_DELIM(&request);

    /* id */
    HS_REQUEST_LONG(&request, id);
    HS_REQUEST_DELIM(&request);

    /* db */
    hs_request_string(&request, db, db_len);
    HS_REQUEST_DELIM(&request);

    /* table */
    hs_request_string(&request, table, table_len);
    HS_REQUEST_DELIM(&request);

    /* index */
    hs_request_string(&request, index, index_len);
    HS_REQUEST_DELIM(&request);

    /* fields */
    hs_request_string(&request, request_field.c, request_field.len);

    /* filters */
    if (hsi->filter && Z_TYPE_P(hsi->filter) == IS_ARRAY)
    {
        hs_request_filter(&request, HASH_OF(hsi->filter) TSRMLS_CC);
    }

    /* eol */
    HS_REQUEST_NEXT(&request);

    /* request: send */
    if (hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        smart_str_free(&request);
        RETURN_FALSE;
    }

    smart_str_free(&request);

    /* response */
    hs_response_value(hs, hsi->error, return_value, 0 TSRMLS_CC);

    if (Z_TYPE_P(return_value) == IS_BOOL && Z_LVAL_P(return_value) == 0)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to open index: %ld: %s",
            id, hsi->error == NULL ? "Unknwon error" : Z_STRVAL_P(hsi->error));
        RETURN_FALSE;
    }

    /* property */
    zend_update_property_stringl(
        hs_index_ce, getThis(), "_db", strlen("_db"),
        db, db_len TSRMLS_CC);
    zend_update_property_stringl(
        hs_index_ce, getThis(), "_table", strlen("_table"),
        table, table_len TSRMLS_CC);
    zend_update_property_stringl(
        hs_index_ce, getThis(), "_name", strlen("_name"),
        index, index_len TSRMLS_CC);
    zend_update_property(
        hs_index_ce, getThis(), "_field", strlen("_field"), fields TSRMLS_CC);

    /* id */
    hsi->id = id;
}

static ZEND_METHOD(HandlerSocketIndex, getId)
{
    php_hs_index_t *hsi;
    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);

    RETVAL_LONG(hsi->id);
}
static ZEND_METHOD(HandlerSocketIndex, getDatabase)
{
    zval *prop;
    HS_INDEX_PROPERTY(_db, prop);
    RETVAL_ZVAL(prop, 1, 0);
}

static ZEND_METHOD(HandlerSocketIndex, getTable)
{
    zval *prop;
    HS_INDEX_PROPERTY(_table, prop);
    RETVAL_ZVAL(prop, 1, 0);
}

static ZEND_METHOD(HandlerSocketIndex, getName)
{
    zval *prop;
    HS_INDEX_PROPERTY(_name, prop);
    RETVAL_ZVAL(prop, 1, 0);
}

static ZEND_METHOD(HandlerSocketIndex, getField)
{
    zval *prop;

    HS_INDEX_PROPERTY(_field, prop);

    if (Z_TYPE_P(prop) == IS_STRING)
    {
        zval delim;

        array_init(return_value);
        ZVAL_STRINGL(&delim, ",", strlen(","), 0);

        php_explode(&delim, prop, return_value, LONG_MAX);
    }
    else
    {
        RETVAL_ZVAL(prop, 1, 0);
    }
}

static ZEND_METHOD(HandlerSocketIndex, getFilter)
{
    php_hs_index_t *hsi;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);

    if (!hsi->filter)
    {
        RETURN_NULL();
    }

    RETVAL_ZVAL(hsi->filter, 1, 0);
}

static ZEND_METHOD(HandlerSocketIndex, getOperator)
{
    zval *find, *modify;

    MAKE_STD_ZVAL(find);
    array_init(find);

    add_next_index_stringl(
        find, HS_FIND_EQUAL, strlen(HS_FIND_EQUAL), 1);
    add_next_index_stringl(
        find, HS_FIND_LESS, strlen(HS_FIND_LESS), 1);
    add_next_index_stringl(
        find, HS_FIND_LESS_EQUAL, strlen(HS_FIND_LESS_EQUAL), 1);
    add_next_index_stringl(
        find, HS_FIND_GREATER, strlen(HS_FIND_GREATER), 1);
    add_next_index_stringl(
        find, HS_FIND_GREATER_EQUAL, strlen(HS_FIND_GREATER_EQUAL), 1);

    MAKE_STD_ZVAL(modify);
    array_init(modify);

    add_next_index_stringl(
        modify, HS_MODIFY_UPDATE, strlen(HS_MODIFY_UPDATE), 1);
    add_next_index_stringl(
        modify, HS_MODIFY_INCREMENT, strlen(HS_MODIFY_INCREMENT), 1);
    add_next_index_stringl(
        modify, HS_MODIFY_DECREMENT, strlen(HS_MODIFY_DECREMENT), 1);
    add_next_index_stringl(
        modify, HS_MODIFY_REMOVE, strlen(HS_MODIFY_REMOVE), 1);
    add_next_index_stringl(
        modify, HS_MODIFY_UPDATE_PREV, strlen(HS_MODIFY_UPDATE_PREV), 1);
    add_next_index_stringl(
        modify, HS_MODIFY_INCREMENT_PREV, strlen(HS_MODIFY_INCREMENT_PREV), 1);
    add_next_index_stringl(
        modify, HS_MODIFY_DECREMENT_PREV, strlen(HS_MODIFY_DECREMENT_PREV), 1);
    add_next_index_stringl(
        modify, HS_MODIFY_REMOVE_PREV, strlen(HS_MODIFY_REMOVE_PREV), 1);

    array_init(return_value);
    add_next_index_zval(return_value, find);
    add_next_index_zval(return_value, modify);
}

static ZEND_METHOD(HandlerSocketIndex, getError)
{
    php_hs_index_t *hsi;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);

    if (hsi->error == NULL)
    {
        RETURN_NULL();
    }
    else
    {
        RETVAL_ZVAL(hsi->error, 1, 0);
    }
}

static ZEND_METHOD(HandlerSocketIndex, find)
{
    zval *query, *operate, *criteria;
    long limit = 1, offset = 0;
    zval *options = NULL;

    smart_str request = {0};

    zval *filters = NULL, *in_values = NULL;
    long in_key = -1;
    int safe = -1;

    php_hs_index_t *hsi;
    php_hs_t *hs;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);
    HS_ERROR_RESET(hsi->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "z|llz",
            &query, &limit, &offset, &options) == FAILURE)
    {
        RETURN_FALSE;
    }

    hs = (php_hs_t *)zend_object_store_get_object(hsi->link TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (!hs->stream)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to open index: %ld", hsi->id);
        RETURN_FALSE;
    }

    /* operete : criteria */
    MAKE_STD_ZVAL(operate);
    if (hs_zval_to_operate_criteria(
            query, operate, &criteria, HS_FIND_EQUAL TSRMLS_CC) < 0)
    {
        zval_ptr_dtor(&operate);
        RETURN_FALSE;
    }

    if (options != NULL && Z_TYPE_P(options) == IS_ARRAY)
    {
        /* options: safe */
        safe = hs_is_options_safe(HASH_OF(options) TSRMLS_CC);

        /* options: fiters, in */
        hs_array_to_in_filter(
            HASH_OF(options), hsi->filter,
            &filters, &in_key, &in_values TSRMLS_CC);
    }

    /* find */
    hs_request_find(
        &request, hsi->id, operate, criteria,
        limit, offset, filters, in_key, in_values TSRMLS_CC);

    /* eol */
    HS_REQUEST_NEXT(&request);

    /* request: send */
    if (hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        ZVAL_BOOL(return_value, 0);
    }
    else
    {
        /* response */
        hs_response_value(hs, hsi->error, return_value, 0 TSRMLS_CC);
    }

    zval_ptr_dtor(&operate);
    if (filters)
    {
        zval_ptr_dtor(&filters);
    }
    smart_str_free(&request);

    /* exception */
    if (safe > 0 &&
        Z_TYPE_P(return_value) == IS_BOOL && Z_LVAL_P(return_value) == 0)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] response error: %s",
            hsi->error == NULL ? "Unknown error" : Z_STRVAL_P(hsi->error));
    }
}

static ZEND_METHOD(HandlerSocketIndex, insert)
{
    zval ***args;
    zval *operate, *fields;
    long i, argc = ZEND_NUM_ARGS();

    smart_str request = {0};

    php_hs_index_t *hsi;
    php_hs_t *hs;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);
    HS_ERROR_RESET(hsi->error);

    if (argc < 1)
    {
        zend_wrong_param_count(TSRMLS_C);
        RETURN_FALSE;
    }

    args = safe_emalloc(argc, sizeof(zval **), 0);
    if (zend_get_parameters_array_ex(argc, args) == FAILURE)
    {
        efree(args);
        zend_wrong_param_count(TSRMLS_C);
        RETURN_FALSE;
    }

    hs = (php_hs_t *)zend_object_store_get_object(hsi->link TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (!hs->stream)
    {
        efree(args);
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to open index: %ld", hsi->id);
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(operate);
    ZVAL_STRINGL(operate, HS_PROTOCOL_INSERT, 1, 1);

    if (Z_TYPE_PP(args[0]) == IS_ARRAY)
    {
        fields = *args[0];
    }
    else
    {
        MAKE_STD_ZVAL(fields);
        array_init(fields);

        for (i = 0; i < argc; i++)
        {
            switch (Z_TYPE_P(*args[i]))
            {
                case IS_NULL:
                    add_next_index_null(fields);
                    break;
                case IS_LONG:
                    add_next_index_long(fields, Z_LVAL_P(*args[i]));
                    break;
                case IS_DOUBLE:
                    add_next_index_double(fields, Z_DVAL_P(*args[i]));
                    break;
                default:
                    convert_to_string(*args[i]);
                    add_next_index_stringl(
                        fields, Z_STRVAL_P(*args[i]), Z_STRLEN_P(*args[i]), 1);
                    break;
            }
        }
    }

    /* find */
    hs_request_find(
        &request, hsi->id, operate, fields, 0, 0, NULL, -1, NULL TSRMLS_CC);


    /* eol */
    HS_REQUEST_NEXT(&request);

    /* request: send */
    if (hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        ZVAL_BOOL(return_value, 0);
    }
    else
    {
        /* response */
        hs_response_value(hs, hsi->error, return_value, 1 TSRMLS_CC);
    }

   zval_ptr_dtor(&operate);
   zval_ptr_dtor(&fields);
   smart_str_free(&request);
   efree(args);
}

static ZEND_METHOD(HandlerSocketIndex, update)
{
    zval *query, *update, *operate, *criteria, *modify_operate, *modify_criteria;
    zval *options = NULL;
    long limit = 1, offset = 0;

    smart_str request = {0};

    zval *filters = NULL, *in_values = NULL;
    long in_key = -1;
    int safe = -1, modify = 1;

    php_hs_index_t *hsi;
    php_hs_t *hs;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);
    HS_ERROR_RESET(hsi->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "zz|llz",
            &query, &update, &limit, &offset, &options) == FAILURE)
    {
        RETURN_FALSE;
    }

    hs = (php_hs_t *)zend_object_store_get_object(hsi->link TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (!hs->stream)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to open index: %ld", hsi->id);
        RETURN_FALSE;
    }

    /* operete : criteria */
    MAKE_STD_ZVAL(operate);
    if (hs_zval_to_operate_criteria(
            query, operate, &criteria, HS_FIND_EQUAL TSRMLS_CC) < 0)
    {
        zval_ptr_dtor(&operate);
        RETURN_FALSE;
    }

    /* modify_operete : modify_criteria */
    MAKE_STD_ZVAL(modify_operate);
    if (hs_zval_to_operate_criteria(
            update, modify_operate, &modify_criteria,
            HS_MODIFY_UPDATE TSRMLS_CC) < 0)
    {
        zval_ptr_dtor(&operate);
        zval_ptr_dtor(&modify_operate);
        RETURN_FALSE;
    }

    if (options != NULL && Z_TYPE_P(options) == IS_ARRAY)
    {
        /* options: safe */
        safe = hs_is_options_safe(HASH_OF(options) TSRMLS_CC);

        /* options: fiters, in */
        hs_array_to_in_filter(
            HASH_OF(options), hsi->filter,
            &filters, &in_key, &in_values TSRMLS_CC);
    }

    /* find */
    hs_request_find(
        &request, hsi->id, operate, criteria,
        limit, offset, filters, in_key, in_values TSRMLS_CC);

    /* find: modify */
    modify = hs_request_find_modify(
        &request, modify_operate, modify_criteria, -1 TSRMLS_CC);
    if (modify >= 0)
    {
        /* eol */
        HS_REQUEST_NEXT(&request);

        /* request: send */
        if (hs_request_send(hs, &request TSRMLS_CC) < 0)
        {
            ZVAL_BOOL(return_value, 0);
        }
        else
        {
            /* response */
            hs_response_value(hs, hsi->error, return_value, modify TSRMLS_CC);
        }
    }
    else
    {
        ZVAL_BOOL(return_value, 0);
        ZVAL_STRINGL(
            hsi->error,
            "[handlersocket] unable to update parameter",
            strlen("[handlersocket] unable to update parameter"), 1);
    }

    zval_ptr_dtor(&operate);
    zval_ptr_dtor(&modify_operate);
    if (filters)
    {
        zval_ptr_dtor(&filters);
    }
    smart_str_free(&request);

    /* exception */
    if (safe > 0 &&
        Z_TYPE_P(return_value) == IS_BOOL && Z_LVAL_P(return_value) == 0)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] response error: %s",
            hsi->error == NULL ? "Unknown error" : Z_STRVAL_P(hsi->error));
    }
}

static ZEND_METHOD(HandlerSocketIndex, remove)
{
    zval *query, *operate, *criteria, *options = NULL;
    long limit = 1, offset = 0;

    smart_str request = {0};

    zval *filters = NULL, *in_values = NULL;
    long in_key = -1;
    int safe = -1;

    php_hs_index_t *hsi;
    php_hs_t *hs;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);
    HS_ERROR_RESET(hsi->error);

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "z|llz",
            &query, &limit, &offset, &options) == FAILURE)
    {
        RETURN_FALSE;
    }

    hs = (php_hs_t *)zend_object_store_get_object(hsi->link TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (!hs->stream)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to open index: %ld", hsi->id);
        RETURN_FALSE;
    }

    /* operete : criteria */
    MAKE_STD_ZVAL(operate);
    if (hs_zval_to_operate_criteria(
            query, operate, &criteria, HS_FIND_EQUAL TSRMLS_CC) < 0)
    {
        zval_ptr_dtor(&operate);
        RETURN_FALSE;
    }

    if (options != NULL && Z_TYPE_P(options) == IS_ARRAY)
    {
        /* options: safe */
        safe = hs_is_options_safe(HASH_OF(options) TSRMLS_CC);

        /* options: fiters, in */
        hs_array_to_in_filter(
            HASH_OF(options), hsi->filter,
            &filters, &in_key, &in_values TSRMLS_CC);
    }

    /* find */
    hs_request_find(
        &request, hsi->id, operate, criteria,
        limit, offset, filters, in_key, in_values TSRMLS_CC);

    /* find: modify: D */
    HS_REQUEST_DELIM(&request);
    hs_request_string(&request, HS_MODIFY_REMOVE, 1);

    /* eol */
    HS_REQUEST_NEXT(&request);

    /* request: send */
    if (hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        ZVAL_BOOL(return_value, 0);
    }
    else
    {
        /* response */
        hs_response_value(hs, hsi->error, return_value, 1 TSRMLS_CC);
    }

    zval_ptr_dtor(&operate);
    if (filters)
    {
        zval_ptr_dtor(&filters);
    }
    smart_str_free(&request);

    /* exception */
    if (safe > 0 &&
        Z_TYPE_P(return_value) == IS_BOOL && Z_LVAL_P(return_value) == 0)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] response error: %s",
            hsi->error == NULL ? "Unknown error" : Z_STRVAL_P(hsi->error));
    }
}

static ZEND_METHOD(HandlerSocketIndex, multi)
{
    zval *args = NULL;
    zval *rmodify;
    HashPosition pos;
    zval **val;

    smart_str request = {0};

    php_hs_index_t *hsi;
    php_hs_t *hs;

    int err = -1;
    long i, n;

    hsi = (php_hs_index_t *)zend_object_store_get_object(getThis() TSRMLS_CC);
    HS_CHECK_OBJECT(hsi, HandlerSocketIndex);
    HS_ERROR_RESET(hsi->error);

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &args) == FAILURE)
    {
        RETURN_FALSE;
    }

    hs = (php_hs_t *)zend_object_store_get_object(hsi->link TSRMLS_CC);
    HS_CHECK_OBJECT(hs, HandlerSocket);

    if (!hs->stream)
    {
        zend_throw_exception_ex(
            hs_exception_ce, 0 TSRMLS_CC,
            "[handlersocket] unable to open index: %ld", hsi->id);
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(rmodify);
    array_init(rmodify);

    zend_hash_internal_pointer_reset_ex(HASH_OF(args), &pos);
    while (zend_hash_get_current_data_ex(
               HASH_OF(args), (void **)&val, &pos) == SUCCESS)
    {
        zval **method;
        HashTable *ht;

        if (Z_TYPE_PP(val) != IS_ARRAY)
        {
            err = -1;
            break;
        }

        ht = HASH_OF(*val);

        /* 0: method */
        if (zend_hash_index_find(ht, 0, (void **)&method) != SUCCESS)
        {
            err = -1;
            break;
        }

        convert_to_string(*method);

        if (strncmp(Z_STRVAL_PP(method), "find", strlen("find")) == 0)
        {
            /* method: find */
            zval **query, **tmp, *operate, *criteria;
            zval *options = NULL, *filters = NULL, *in_values = NULL;
            long limit = 1, offset = 0, in_key = -1;

            n = zend_hash_num_elements(ht);
            if (n <= 1)
            {
                err = -1;
                break;
            }

            /* 1: query */
            if (zend_hash_index_find(ht, 1, (void **)&query) != SUCCESS)
            {
                err = -1;
                break;
            }

            /* operete : criteria */
            MAKE_STD_ZVAL(operate);
            if (hs_zval_to_operate_criteria(
                    *query, operate, &criteria, HS_FIND_EQUAL TSRMLS_CC) < 0)
            {
                err = -1;
                zval_ptr_dtor(&operate);
                break;
            }

            if (*query == NULL)
            {
                err = -1;
                zval_ptr_dtor(&operate);
                break;
            }

            for (i = 2; i < n; i++)
            {
                if (zend_hash_index_find(ht, i, (void **)&tmp) == SUCCESS)
                {
                    switch (i)
                    {
                        case 2:
                            /* 2: limit */
                            convert_to_long(*tmp);
                            limit = Z_LVAL_PP(tmp);
                            break;
                        case 3:
                            /* 3: offset */
                            convert_to_long(*tmp);
                            offset = Z_LVAL_PP(tmp);
                            break;
                        case 4:
                            /* 4: options */
                            options = *tmp;
                            break;
                        default:
                            break;
                    }
                }
            }

            if (options != NULL && Z_TYPE_P(options) == IS_ARRAY)
            {
                /* options: fiters, in */
                hs_array_to_in_filter(
                    HASH_OF(options), hsi->filter,
                    &filters, &in_key, &in_values TSRMLS_CC);
            }

            /* find */
            hs_request_find(
                &request, hsi->id, operate, criteria,
                limit, offset, filters, in_key, in_values TSRMLS_CC);

            /* eol */
            HS_REQUEST_NEXT(&request);

            add_next_index_long(rmodify, 0);
            err = 0;

            zval_ptr_dtor(&operate);
            if (filters)
            {
                zval_ptr_dtor(&filters);
            }
        }
        else if (strncmp(Z_STRVAL_PP(method), "insert", strlen("insert")) == 0)
        {
            /* method: insert */
            zval *operate, *fields;
            zval **tmp;

            n = zend_hash_num_elements(ht);

            if (n <= 1)
            {
                err = -1;
                break;
            }

            if (zend_hash_index_find(ht, 1, (void **)&tmp) != SUCCESS)
            {
                err = -1;
                break;
            }

            MAKE_STD_ZVAL(fields);
            array_init(fields);

            if (Z_TYPE_PP(tmp) == IS_ARRAY)
            {
                n = zend_hash_num_elements(HASH_OF(*tmp));
                for (i = 0; i < n; i++)
                {
                    zval **val;
                    if (zend_hash_index_find(
                            HASH_OF(*tmp), i, (void **)&val) == SUCCESS)
                    {
                        if (Z_TYPE_PP(val) == IS_NULL)
                        {
                            add_next_index_null(fields);
                        }
                        else
                        {
                            convert_to_string(*val);
                            add_next_index_stringl(
                                fields, Z_STRVAL_PP(val), Z_STRLEN_PP(val), 1);
                        }
                    }
                }
            }
            else
            {
                i = 1;
                do
                {
                    if (Z_TYPE_PP(tmp) == IS_NULL)
                    {
                        add_next_index_null(fields);
                    }
                    else
                    {
                        convert_to_string(*tmp);
                        add_next_index_stringl(
                            fields, Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp), 1);
                    }

                    i++;

                    if (zend_hash_index_find(ht, i, (void **)&tmp) != SUCCESS)
                    {
                        break;
                    }
                }
                while (i < n);
            }

            MAKE_STD_ZVAL(operate);
            ZVAL_STRINGL(operate, HS_PROTOCOL_INSERT, 1, 1);

            /* find */
            hs_request_find(
                &request, hsi->id, operate, fields, 0, 0,
                NULL, -1, NULL TSRMLS_CC);

            /* eol */
            HS_REQUEST_NEXT(&request);

            add_next_index_long(rmodify, 1);
            err = 0;

            zval_ptr_dtor(&operate);
            zval_ptr_dtor(&fields);
        }
        else if (strncmp(Z_STRVAL_PP(method), "remove", strlen("remove")) == 0)
        {
            /* method: remove */
            zval **query, **tmp, *operate, *criteria;
            zval *options = NULL, *filters = NULL, *in_values = NULL;
            long limit = 1, offset = 0, in_key = -1;

            n = zend_hash_num_elements(ht);
            if (n <= 1)
            {
                err = -1;
                break;
            }

            /* 1: query */
            if (zend_hash_index_find(ht, 1, (void **)&query) != SUCCESS)
            {
                if (operate)
                {
                    zval_ptr_dtor(&operate);
                }
                err = -1;
                break;
            }

            /* operete : criteria */
            MAKE_STD_ZVAL(operate);
            if (hs_zval_to_operate_criteria(
                    *query, operate, &criteria, HS_FIND_EQUAL TSRMLS_CC) < 0)
            {
                zval_ptr_dtor(&operate);
                err = -1;
                break;
            }

            for (i = 2; i < n; i++)
            {
                if (zend_hash_index_find(ht, i, (void **)&tmp) == SUCCESS)
                {
                    switch (i)
                    {
                        case 2:
                            /* 2: limit */
                            convert_to_long(*tmp);
                            limit = Z_LVAL_PP(tmp);
                            break;
                        case 3:
                            /* 3: offset */
                            convert_to_long(*tmp);
                            offset = Z_LVAL_PP(tmp);
                            break;
                        case 4:
                            /* 4: options */
                            options = *tmp;
                            break;
                        default:
                            break;
                    }
                }
            }

            if (options != NULL && Z_TYPE_P(options) == IS_ARRAY)
            {
                /* options: fiters, in */
                hs_array_to_in_filter(
                    HASH_OF(options), hsi->filter,
                    &filters, &in_key, &in_values TSRMLS_CC);
            }

            /* find */
            hs_request_find(
                &request, hsi->id, operate, criteria,
                limit, offset, filters, in_key, in_values TSRMLS_CC);

            /* find: modify: D */
            HS_REQUEST_DELIM(&request);

            hs_request_string(&request, HS_MODIFY_REMOVE, 1);

            /* eol */
            HS_REQUEST_NEXT(&request);

            add_next_index_long(rmodify, 1);
            err = 0;

            zval_ptr_dtor(&operate);
            if (filters)
            {
                zval_ptr_dtor(&filters);
            }
        }
        else if (strncmp(Z_STRVAL_PP(method), "update", strlen("update")) == 0)
        {
            /* method: update */
            zval **query, **update, **tmp;
            zval *operate, *criteria, *modify_operate, *modify_criteria;
            zval *options = NULL, *filters = NULL, *in_values = NULL;
            long limit = 1, offset = 0, in_key = -1;
            int modify;

            n = zend_hash_num_elements(ht);
            if (n <= 2)
            {
                err = -1;
                break;
            }

            /* 1: query */
            if (zend_hash_index_find(ht, 1, (void **)&query) != SUCCESS)
            {
                err = -1;
                break;
            }

            /* 2: update */
            if (zend_hash_index_find(ht, 2, (void **)&update) != SUCCESS)
            {
                err = -1;
                break;
            }

            /* operete : criteria */
            MAKE_STD_ZVAL(operate);
            if (hs_zval_to_operate_criteria(
                    *query, operate, &criteria, HS_FIND_EQUAL TSRMLS_CC) < 0)
            {
                zval_ptr_dtor(&operate);
                err = -1;
                break;
            }

            /* modify_operete : modify_criteria */
            MAKE_STD_ZVAL(modify_operate);
            if (hs_zval_to_operate_criteria(
                    *update, modify_operate, &modify_criteria,
                    HS_MODIFY_UPDATE TSRMLS_CC) < 0)
            {
                zval_ptr_dtor(&operate);
                zval_ptr_dtor(&modify_operate);
                err = -1;
                break;
            }

            for (i = 3; i < n; i++)
            {
                if (zend_hash_index_find(ht, i, (void **)&tmp) == SUCCESS)
                {
                    switch (i)
                    {
                        case 3:
                            /* 3: limit */
                            convert_to_long(*tmp);
                            limit = Z_LVAL_PP(tmp);
                            break;
                        case 4:
                            /* 4: offset */
                            convert_to_long(*tmp);
                            offset = Z_LVAL_PP(tmp);
                            break;
                        case 5:
                            /* 5: options */
                            options = *tmp;
                            break;
                        default:
                            break;
                    }
                }
            }

            if (options != NULL && Z_TYPE_P(options) == IS_ARRAY)
            {
                /* options: fiters, in */
                hs_array_to_in_filter(
                    HASH_OF(options), hsi->filter,
                    &filters, &in_key, &in_values TSRMLS_CC);
            }

            /* find */
            hs_request_find(
                &request, hsi->id, operate, criteria,
                limit, offset, filters, in_key, in_values TSRMLS_CC);

            //find: modify
            modify = hs_request_find_modify(
                &request, modify_operate, modify_criteria,
                -1 TSRMLS_CC);
            if (modify < 0)
            {
                err = -1;
                break;
            }

            /* eol */
            HS_REQUEST_NEXT(&request);

            add_next_index_long(rmodify, modify);
            err = 0;

            zval_ptr_dtor(&operate);
            zval_ptr_dtor(&modify_operate);
            if (filters)
            {
                zval_ptr_dtor(&filters);
            }
        }
        else
        {
            err = -1;
            break;
        }

        zend_hash_move_forward_ex(HASH_OF(args), &pos);
    }

    /* request: send */
    if (err < 0  || hs_request_send(hs, &request TSRMLS_CC) < 0)
    {
        smart_str_free(&request);
        zval_ptr_dtor(&rmodify);
        RETURN_FALSE;
    }
    smart_str_free(&request);

    /* response */
    hs_response_multi(hs, return_value, rmodify, hsi->error TSRMLS_CC);

    zval_ptr_dtor(&rmodify);
}
