#ifndef _HIBASE_H_
#define _HIBASE_H_
#ifdef __cplusplus
extern "C" {
#endif
#define FTYPE_INT 		        0x01
#define FTYPE_FLOAT		        0x02
#define FTYPE_TEXT		        0x04
#define FTYPE_BLOB		        0x08
#define FTYPE_ALL 		        (FTYPE_INT|FTYPE_FLOAT|FTYPE_TEXT|FTYPE_BLOB)	
#define FIELD_NUM_MAX		    256
#define HI_INDEX_MAX		    32
#define PATTERN_NUM_MAX         8
#define FIELD_NAME_MAX		    32
#define TABLE_NAME_MAX		    32
#define F_IS_INDEX              0x01
#define F_IS_NULL               0x02
#define TEMPLATE_NAME_MAX	    32
#define HIBASE_PATH_MAX		    256
#define PATTERN_LEN_MAX		    4096
#define TABLE_INCRE_NUM         256
#define TEMPLATE_INCRE_NUM      1000
#define TNODE_INCRE_NUM         10000
#define URI_INCRE_NUM           10000
#define URLMAP_INCRE_NUM        10000
#define URLNODE_INCRE_NUM       10000
#define RECORD_INCRE_NUM        10000
#define TNODE_CHILDS_MAX        10000
#define TAB_STATUS_ERR          -1
#define TAB_STATUS_INIT         0
#define TAB_STATUS_OK           1
#define FIELD_STATUS_INIT       0
#define FIELD_STATUS_OK         1
#define TNODE_STATUS_ERR      -1
#define TNODE_STATUS_INIT     0
#define TNODE_STATUS_OK       1
#define TEMPLATE_STATUS_ERR     -1
#define TEMPLATE_STATUS_INIT    0
#define TEMPLATE_STATUS_OK      1
#define URLNODE_STATUS_ERR      -1
#define URLNODE_STATUS_INIT     0
#define URLNODE_STATUS_OK       1
#ifndef HI_URL_MAX
#define HI_URL_MAX              4096
#endif
#ifndef HI_URLMAP_SIZE
#define HI_URLMAP_SIZE          4
#endif
#ifndef HI_BUF_SIZE
#define HI_BUF_SIZE             262144
#endif
/* field */
typedef struct _IFIELD
{
    short   status;
    short   type;
    int     flag;
    int     id;
    char    name[FIELD_NAME_MAX];
}IFIELD;
/* table */
typedef struct _ITABLE
{
    short   status;
    short   nfields;
    int     uid;
    char    name[TABLE_NAME_MAX];
    IFIELD  fields[FIELD_NUM_MAX];
}ITABLE;
/* page node */
#define TNODE_NUM_MAX   256
#define TNODE_NAME_MAX  256
typedef struct _TNODE
{
    short status;
    short level;
    int id;
    int uid;
    int mmid;
    int parent;
    int nchilds;
    int childs_rootid;
    int ntemplates;
    int templates_rootid;
    int nurlnodes;
    int urlnodes_rootid;
    char name[TNODE_NAME_MAX];
}TNODE;
typedef struct _URLNODE
{
    short status;
    short level;
    int id;
    int parentid;
    int tnodeid;
    int urlid;
    int nchilds;
    int childs_rootid;
    int tnode_mmid;
    int mmid;
    int recordid;
}URLNODE;
#define REG_IS_URL               0x01
#define REG_IS_FILE              0x02
#define REG_IS_NEED_CLEARHTML    0x04
#define REG_IS_NEED_ANTISPAM     0x08
#define REG_IS_POST              0x10
#define REG_IS_UNIQE             0x20
#define REG_IS_LIST              0x40
#define REG_IS_NEED_TRIM         0x80
typedef struct _IREGX
{
    short flag;
    short fieldid;
    int   nodeid;
}IREGX;
/* PCRE RES */
typedef struct _PRES
{
    int start;
    int end;
}PRES;
/* url pattern list */
#define PURL_TYPE_CHAR 0
#define PURL_TYPE_INT  1
#define PURL_NUM_MAX 256
typedef struct _PURL
{
    short type;
    short length;
    int from;
    int to;
    char *sfrom;
    char *sto;
}PURL;
/* record */
typedef struct _IREC
{
    off_t offset;
    int length;
}IREC;
typedef struct _IRECORD
{
    int urlid;
    int parentid;
    int length;
    int nrecords;
    int tableid;
    IREC records[FIELD_NUM_MAX];
}IRECORD;
#define TMP_IS_PUBLIC       0x01
#define TMP_IS_GLOBAL       0x02
#define TMP_IS_IGNORECASE   0x04
#define TMP_IS_LINK         0x08
#define TMP_IS_POST         0x10
/* template regular expression */
typedef struct _ITEMPLATE
{
    short status;
    short nfields;
    IREGX map[FIELD_NUM_MAX];
    char  pattern[PATTERN_LEN_MAX];
    char  url[HI_URL_MAX];
    char  link[HI_URL_MAX];
    int   tableid;
    IREGX linkmap;
    int   flags;
    int   tnodeid;
    int   mmid;
}ITEMPLATE;
/* state info */
typedef struct _ISTATE
{
    int templateio_current;
    int templateio_left;
    int templateio_total;
    int urlnodeio_current;
    int urlnodeio_left;
    int urlnodeio_total;
    int urlnode_task_current;
    int uri_total;
    int recordio_current;
    int recordio_left;
    int recordio_total;
}ISTATE;
/* hibase io/map */
typedef struct _HIO
{
    int     fd;
    int     current;
    int     total;
    int     left;
    off_t   end;
    off_t   size;
    void    *map;
}HIO;
/* URI */
typedef struct _URI
{
    int rootid;
    int count;
}URI;
/* hibase */
typedef struct _HIBASE
{
    void    *mdb;
    int     db_uid_max;
    void    *mtnode;
    HIO     tableio;
    HIO     tnodeio;
    void    *qtnode;
    int     tnode_childs_max;
    int     uid_max;
    HIO     templateio;
    void    *qtemplate;
    int     uri_fd;
    HIO     recordio;
    int     db_fd;
    void    *mmtree;
    HIO     urlnodeio;
    void    *qurlnode;
    void    *qtask;
    void    *qwait;
    ISTATE  *istate;
    void    *logger;
    void    *mutex;
    char    basedir[HIBASE_PATH_MAX];

    int 	(*set_basedir)(struct _HIBASE *, char *dir);
    int     (*db_uid_exists)(struct _HIBASE *, int table_id, char *name, int len);
    int 	(*add_table)(struct _HIBASE *, char *name);
    int	    (*get_table)(struct _HIBASE *, int table_id, ITABLE *table);
    int 	(*rename_table)(struct _HIBASE *, int table_id, char *table_name);
    int 	(*delete_table)(struct _HIBASE *, int table_id);
    int	    (*view_table)(struct _HIBASE *, int table_id, char *block);
    int	    (*view_database)(struct _HIBASE *, char *block);
    int 	(*list_table)(struct _HIBASE *, char *block);
    int     (*add_field)(struct _HIBASE *, int table_id, 
            char *name, int type, int flag);
    int     (*update_field)(struct _HIBASE *, int table_id, int field_id, 
            char *name, int type, int is_index);
    int     (*delete_field)(struct _HIBASE *, int table_id, int field_id);
    int     (*add_template)(struct _HIBASE *, int tnodeid, ITEMPLATE *ptemplate);
    int     (*get_template)(struct _HIBASE *, int templateid, ITEMPLATE *ptemplate);
    int     (*update_template)(struct _HIBASE *, int templateid, ITEMPLATE *ptemplate);
    int     (*delete_template)(struct _HIBASE *, int tnodeid, int templateid);
    int     (*view_templates)(struct _HIBASE *, int tnodeid, char *block);
    /*
    int     (*template_exists)(struct _HIBASE *, char *name, int len);
    int 	(*add_template)(struct _HIBASE *, ITEMPLATE *);
    int 	(*get_template)(struct _HIBASE *, int template_id, char *template_name, ITEMPLATE *);
    int 	(*update_template)(struct _HIBASE *, int template_id, ITEMPLATE *);
    int 	(*delete_template)(struct _HIBASE *, int template_id, char *template_name);
    */
    int     (*tnode_exists)(struct _HIBASE *, int parent, char *name, int name_len);
    int     (*add_tnode)(struct _HIBASE *, int parent, char *name);
    int     (*get_tnode)(struct _HIBASE *, int id, TNODE *tnode);
    int     (*get_tnode_templates)(struct _HIBASE *, int id, ITEMPLATE **templates);
    void    (*free_templates)(ITEMPLATE *templates);
    int     (*get_tnode_childs)(struct _HIBASE *, int id, TNODE **tnodes);
    void    (*free_tnode_childs)(TNODE *tnodes);
    int     (*view_tnode_childs)(struct _HIBASE *, int id, char *block);
    int     (*update_tnode)(struct _HIBASE *, int parentid, int id, char *name);
    int     (*delete_tnode)(struct _HIBASE *, int parentid, int id);
    int     (*add_uri)(struct _HIBASE *, int urlid, int urlnodeid);
    int     (*get_uris)(struct _HIBASE *, int urlid, int **urlnodeids);
    void    (*del_uri)(struct _HIBASE *, int urlid, int urlnodeid);
    void    (*free_uris)(int *urlnodeids);
    int     (*add_urlnode)(struct _HIBASE *, int nodeid, int parentid, int urlid, int level);
    int     (*update_urlnode)(struct _HIBASE *, int urlnodeid, int level);
    int     (*delete_urlnode)(struct _HIBASE *, int urlnodeid);
    int     (*get_urlnode)(struct _HIBASE *, int urlnodeid, URLNODE *urlnode);
    int     (*find_tnode_from_parents)(struct _HIBASE *, int parentid, int tnodeid);
    int     (*get_urlnode_childs)(struct _HIBASE *, int *ppid, int urlnodeid, URLNODE **childs, 
            int *total, int from, int count);
    int     (*get_tnode_urlnodes)(struct _HIBASE *, int nodeid, URLNODE **urlnodes, 
            int *total, int from, int count);
    void    (*free_urlnodes)(URLNODE *urlnodes);
    int     (*pop_urlnode)(struct _HIBASE *, URLNODE *urlnode);
    int     (*push_task_urlnodeid)(struct _HIBASE *, int urlnodeid);
    int     (*pop_task_urlnodeid)(struct _HIBASE *);
    int     (*update_record)(struct _HIBASE *, int parentid, int urlid, 
            PRES *pres, int tableid, char *block, int nblock);
    int     (*get_record)(struct _HIBASE *, int recordid, int urlid);
    int     (*view_record)(struct _HIBASE *, int recordid, int urlnodeid, char **block);
    void    (*free_record)(void *block);
    void 	(*clean)(struct _HIBASE **);	
}HIBASE;
/* hibase initialize */
HIBASE *hibase_init();
#define PHIBASE(p) ((HIBASE *)p)
#ifdef __cplusplus
 }
#endif
#endif
