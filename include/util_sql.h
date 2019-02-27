/*  @file       util_sql.h
    @brief      A set of convenience macros for formatting SQL statements.
    @author     Joa Käis (github.com/jiikai).
    @copyright  Public domain.
*/

#ifndef _util_sql_h_
#define _util_sql_h_

#define SQL_PREPARE_INSERT_INTO(out, out_n, stmt_name, types, table, columns, ...)\
    snprintf(out, out_n, "PREPARE %s (%s) AS INSERT INTO %s (%s) VALUES (%s);",\
        stmt_name, types, table, columns, __VA_ARGS__)

#define SQL_PREPARE_UPDATE_WHERE(out, out_n, stmt_name, types, table, columns, ...)\
    snprintf(out, out_n, "PREPARE %s (%s) AS UPDATE %s SET %s WHERE %s;",\
        stmt_name, types, table, columns, __VA_ARGS__)

#define SQL_SELECT(buf, buf_n, out, out_n, columns, aliases, tables, ...)\
    snprintf(buf, buf_n, "SELECT %s AS %s FROM %s;", columns, aliases, tables)  < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_SELECT_WHERE(buf, buf_n, out, out_n, columns, aliases, tbls, where, ...)\
    snprintf(buf, buf_n, "SELECT %s AS %s FROM %s WHERE %s;",\
            columns, aliases, tbls, where) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_SELECT_JOIN_WHERE(buf, buf_n, out, out_n, columns, aliases,\
    from_tbl, join_type, join_tbl, join_on, where, ...)\
    snprintf(buf, buf_n, "SELECT %s AS %s FROM %s %s JOIN %s ON %s WHERE %s;",\
            columns, aliases, from_tbl, join_type, join_tbl, join_on, where) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_INSERT_INTO(buf, buf_n, out, out_n, table, columns, values, ...)\
    snprintf(buf, buf_n, "INSERT INTO %s (%s) VALUES (%s);", table, columns, values) < 0 ? -1 :\
    snprintf(out, out_n,  buf, __VA_ARGS__)

#define SQL_UPDATE_WHERE(buf, buf_n, out, out_n, table, set, where, ...)\
    snprintf(buf, buf_n, "UPDATE %s SET %s WHERE %s;", table, set, where) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_UPSERT(buf, buf_n, out, out_n, insertsql, arbiter, set, ...)\
    snprintf(buf, buf_n, "%s ON CONFLICT (%s) DO UPDATE SET %s;",\
            insertsql, arbiter, set) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_WITH_SELECT_WHERE(buf, buf_n, out, out_n, with_table,\
        columns, aliases, from_tables, where, ...)\
    snprintf(buf, buf_n, "WITH %s AS (SELECT %s AS %s FROM %s WHERE %s)",\
            with_table, columns, aliases, from_tables, where) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_APPEND_WITH_SELECT_WHERE(buf, buf_n, out, out_n, sql, with_table,\
        columns, aliases, from_tables, where, ...)\
    snprintf(buf, buf_n, "%s, %s AS (SELECT %s AS %s FROM %s WHERE %s)",\
        sql, with_table, columns, aliases, from_tables, where) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_UPDATE_WITH_WHERE(buf, buf_n, out, out_n, withsql, table, set, where, ...)\
    snprintf(buf, buf_n, "%s UPDATE %s SET %s WHERE %s;",\
            withsql, table, set, where) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#define SQL_WHEN_THEN_ELSE(buf, buf_n, out, out_n, condition, sql_1, sql_2, ...)\
    snprintf(buf, buf_n, "CASE WHEN %s THEN %s ELSE %s;") < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#endif /*_util_sql_h_ */
