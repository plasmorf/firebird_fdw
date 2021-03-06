#ifndef FIREBIRD_FDW_H
#define FIREBIRD_FDW_H

#include "funcapi.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_user_mapping.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "mb/pg_wchar.h"
#include "nodes/makefuncs.h"
#include "nodes/relation.h"
#include "optimizer/cost.h"
#include "optimizer/pathnode.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "parser/parsetree.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"


#include "libfq.h"


#define FB_FDW_LOGPREFIX "[firebird_fdw] "
#define FB_FDW_LOGPREFIX_LEN strlen(FB_FDW_LOGPREFIX)

/*
 * Macro to indicate if a given PostgreSQL datatype can be
 * converted to a Firebird type
 */
#define canConvertPgType(x) ((x) == TEXTOID || (x) == CHAROID || (x) == BPCHAROID \
                             || (x) == VARCHAROID || (x) == NAMEOID || (x) == INT8OID || (x) == INT2OID \
                             || (x) == INT4OID ||  (x) == FLOAT4OID || (x) == FLOAT8OID \
                             || (x) == NUMERICOID || (x) == DATEOID || (x) == TIMESTAMPOID \
                             || (x) == TIMEOID)

typedef struct fbTableColumn
{
    char *fbname;            /* Firebird column name */
    char *pgname;            /* PostgreSQL column name */
    int pgattnum;            /* PostgreSQL attribute number */
    Oid pgtype;              /* PostgreSQL data type */
    int pgtypmod;            /* PostgreSQL type modifier */
    bool isdropped;          /* indicate if PostgreSQL column is dropped */
    bool used;               /* indicate if column used in current query */
} fbTableColumn;

typedef struct fbTable
{
    Oid foreigntableid;
    int pg_column_total;
    char *pg_table_name;
	fbTableColumn **columns;
} fbTable;


/*
 * Describes the valid options for objects that use this wrapper.
 */
struct FirebirdFdwOption
{
    const char *optname;
    Oid         optcontext;     /* Oid of catalog in which option may appear */
};


/*
 * FDW-specific information for RelOptInfo.fdw_private and ForeignScanState.fdw_state.
 *
 * This is what will be set and stashed away in fdw_private and fetched
 * for subsequent routines.
 */
typedef struct FirebirdFdwState
{
    char       *svr_query;
    char       *svr_table;
    bool        disable_pushdowns;  /* true if server option "disable_pushdowns" supplied */

    FQconn     *conn;
	List       *remote_conds;
	List       *local_conds;

	Bitmapset  *attrs_used;         /* Bitmap of attr numbers to be fetched from the remote server. */
    Cost        startup_cost;       /* cost estimate, only needed for planning */
    Cost        total_cost;         /* cost estimate, only needed for planning */
    int         row;
    char        *query;             /* query to send to Firebird */
} FirebirdFdwState;

/*
 * Execution state of a foreign scan using firebird_fdw.
 */
typedef struct FirebirdFdwScanState
{
    FQconn     *conn;
    /* Foreign table information */
    fbTable    *table;
    List       *retrieved_attrs;    /* attr numbers retrieved by RETURNING */
    /* Query information */
    char       *query;              /* query to send to Firebird */
    bool        db_key_used;        /* indicate whether RDB$DB_KEY was requested */

    FQresult   *result;
    int         row;

} FirebirdFdwScanState;

/*
 * Execution state of a foreign insert/update/delete operation.
 */
typedef struct FirebirdFdwModifyState
{
    Relation    rel;               /* relcache entry for the foreign table */
    AttInMetadata *attinmeta;      /* attribute datatype conversion metadata */

    /* for remote query execution */
    FQconn       *conn;            /* connection for the scan */

    /* extracted fdw_private data */
    char         *query;           /* text of INSERT/UPDATE/DELETE command */
    List         *target_attrs;    /* list of target attribute numbers */
    bool          has_returning;   /* is there a RETURNING clause? */
    List         *retrieved_attrs; /* attr numbers retrieved by RETURNING */

    /* info about parameters for prepared statement */
    AttrNumber    db_keyAttno_CtidPart;  /* attnum of input resjunk rdb$db_key column */
    AttrNumber    db_keyAttno_OidPart;   /* attnum of input resjunk rdb$db_key column (OID part)*/

    int           p_nums;         /* number of parameters to transmit */
    FmgrInfo     *p_flinfo;       /* output conversion functions for them */

    /* working memory context */
    MemoryContext temp_cxt;       /* context for per-tuple temporary data */
} FirebirdFdwModifyState;


/* connection functions (in connection.c) */


extern FQconn *firebirdInstantiateConnection(ForeignServer *server, UserMapping *user);
extern void firebirdCloseConnections(void);

/* option functions (in options.c) */
extern void firebirdGetOptions(Oid foreigntableid, char **query, char **table, bool *disable_pushdowns);

/* query-building functions (in convert.c) */

extern void buildInsertSql(StringInfo buf, PlannerInfo *root,
                 Index rtindex, Relation rel,
                 List *targetAttrs, List *returningList,
                 List **retrieved_attrs);

extern void buildUpdateSql(StringInfo buf, PlannerInfo *root,
                 Index rtindex, Relation rel,
                 List *targetAttrs, List *returningList,
                 List **retrieved_attrs);

extern void buildDeleteSql(StringInfo buf, PlannerInfo *root,
                           Index rtindex, Relation rel,
                           List *returningList,
                           List **retrieved_attrs);

extern void buildSelectSql(StringInfo buf,
               PlannerInfo *root,
               RelOptInfo *baserel,
               Bitmapset *attrs_used,
               List **retrieved_attrs,
               bool *db_key_used);

extern void buildWhereClause(StringInfo buf,
                 PlannerInfo *root,
                 RelOptInfo *baserel,
                 List *exprs,
                 bool is_first,
                 List **params);

extern void
identifyRemoteConditions(PlannerInfo *root,
                         RelOptInfo *baserel,
                         List **remote_conds,
                         List **local_conds,
                         bool disable_pushdowns,
                         int firebird_version);

extern bool
isFirebirdExpr(PlannerInfo *root,
               RelOptInfo *baserel,
               Expr *expr,
               int firebird_version);


extern char *
getFirebirdColumnName(Oid foreigntableid, int varattno);

#if (PG_VERSION_NUM >= 90500)
extern char *
convertFirebirdTable(char *server_name, char *table_name, FQresult *colres);
extern char *
_dataTypeSQL(char *table_name);
#endif

#endif   /* FIREBIRD_FDW_H */
