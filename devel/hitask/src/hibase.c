#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "hibase.h"
#include "trie.h"
#include "fqueue.h"
#include "timer.h"
#include "logger.h"
#include "base64.h"
#include "hio.h"
#include "mmtree.h"
#define  HIBASE_TABLE_NAME          "hibase.table"
#define  HIBASE_TEMPLATE_NAME       "hibase.template"
#define  HIBASE_TNODE_NAME          "hibase.tnode"
#define  HIBASE_URLNODE_NAME        "hibase.urlnode"
#define  HIBASE_URI_NAME            "hibase.uri"
#define  HIBASE_MMTREE_NAME         "hibase.mmtree"
#define  HIBASE_QTNODE_NAME         "hibase.qtnode"
#define  HIBASE_QTEMPLATE_NAME      "hibase.qtemplate"
#define  HIBASE_QURLNODE_NAME       "hibase.qurlnode"
#define  HIBASE_QTASK_NAME          "hibase.qtask"
#define  HIBASE_QWAIT_NAME          "hibase.qwait"
#define  HIBASE_ISTATE_NAME         "hibase.istate"
#define INCRE_STATE(hibase, io, name) (hibase->istate->io##_##name = ++(hibase->io.name))
#define DECRE_STATE(hibase, io, name) (hibase->istate->io##_##name = --(hibase->io.name))
#define UPDATE_STATE(hibase, io)                                        \
do                                                                      \
{                                                                       \
    if(hibase && hibase->istate)                                        \
    {                                                                   \
        hibase->istate->io##_left = hibase->io.left;                    \
        hibase->istate->io##_current = hibase->io.current;              \
    }                                                                   \
}while(0)
#define RESUME_STATE(hibase, io)                                           \
do                                                                      \
{                                                                       \
    if(hibase && hibase->istate)                                        \
    {                                                                   \
        hibase->io.left = hibase->istate->io##_left;                    \
        hibase->io.current = hibase->istate->io##_current;              \
    }                                                                   \
}while(0)
#define ID_IS_VALID(hibase, io, x) (x > 0 && x < hibase->io.total)
#define ID_IS_VALID2(hibase, io, x) (x >= 0 && x < hibase->io.total)
//tables
static char *table_data_types[] = {"null", "int", "float", "null", "text",
    "null","null","null","blob"};
/* mkdir */
int hibase_mkdir(char *path, int mode)
{
    char *p = NULL, fullpath[HIBASE_PATH_MAX];
    int ret = 0, level = -1;
    struct stat st;

    if(path)
    {
        strcpy(fullpath, path);
        p = fullpath;
        while(*p != '\0')
        {
            if(*p == '/' )
            {
                level++;
                while(*p != '\0' && *p == '/' && *(p+1) == '/')++p;
                if(level > 0)
                {
                    *p = '\0';
                    memset(&st, 0, sizeof(struct stat));
                    ret = stat(fullpath, &st);
                    if(ret == 0 && !S_ISDIR(st.st_mode)) return -1;
                    if(ret != 0 && mkdir(fullpath, mode) != 0) return -1;
                    *p = '/';
                }
            }
            ++p;
        }
        return 0;
    }
    return -1;
}

/* set basedir */
int hibase_set_basedir(HIBASE *hibase, char *dir)
{
    char path[HIBASE_PATH_MAX], *p = NULL;
    ITABLE *table = NULL;
    TNODE *tnode = NULL;
    URLNODE *urlnode = NULL;
    int i = 0, j = 0, n = 0, fd = -1;
    struct stat st = {0};
    void *dp = NULL;

    if(hibase && dir)
    {
        n = sprintf(hibase->basedir, "%s/", dir);
        hibase_mkdir(hibase->basedir, 0755);
        //resum state 
        sprintf(path, "%s/%s", dir, HIBASE_ISTATE_NAME);
        if((fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            if(fstat(fd, &st) == 0)
            {
                if(st.st_size == 0) ftruncate(fd, sizeof(ISTATE));
                if((hibase->istate = (ISTATE *)mmap(NULL, sizeof(ISTATE),
                    PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == (void *)-1)
                {
                    _EXIT_("mmap %s failed, %s\n", path, strerror(errno));
                }
            }
            else
            {
                _EXIT_("state %s failed, %s\n", path, strerror(errno));
            }
            close(fd);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        p = path;
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QTASK_NAME);  
        FQUEUE_INIT(hibase->qtask, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QWAIT_NAME);  
        FQUEUE_INIT(hibase->qwait, p, int);
        //resume table
        p = path;
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TABLE_NAME);  
        HIO_INIT(hibase->tableio, p, st, ITABLE, 1, TABLE_INCRE_NUM);
        if(hibase->tableio.fd > 0 && (table = HIO_MAP(hibase->tableio, ITABLE)))
        {
            hibase->tableio.left = 0;
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if((n = strlen(table[i].name)) > 0)
                {
                    if(table[i].uid > hibase->db_uid_max)
                        hibase->db_uid_max = table[i].uid;
                    dp = (void *)((long)(table[i].uid));
                    TRIETAB_ADD(hibase->mdb, table[i].name, n, dp);
                    for(j = 0; j < table[i].nfields; j++)
                    {
                        if((n = strlen(table[i].fields[j].name)) > 0)
                        {
                            if(table[i].fields[j].uid > hibase->db_uid_max)
                                hibase->db_uid_max = table[i].fields[j].uid;
                            dp = (void *)((long)(table[i].fields[j].uid));
                            TRIETAB_ADD(hibase->mdb, table[i].fields[j].name, n, dp);
                        }
                    }
                    hibase->tableio.current = i;
                }
                else hibase->tableio.left++;
            }
        }
        //resume template 
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QTEMPLATE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qtemplate, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TEMPLATE_NAME);   
        HIO_INIT(hibase->templateio, p, st, ITEMPLATE, 0, TEMPLATE_INCRE_NUM);
        RESUME_STATE(hibase, templateio);
        /*
        HIO_MMAP(hibase->templateio, ITEMPLATE, TEMPLATE_INCRE_NUM); 
        if(hibase->templateio.fd > 0 && (template = HIO_MAP(hibase->templateio, ITEMPLATE)))
        {
            hibase->templateio.left = 0;
            for(i = 1; i < hibase->templateio.total; i++)
            {
                if(template[i].status == TEMPLATE_STATUS_OK)
                {
                    hibase->templateio.current = i;
                }
                else hibase->templateio.left++;
            }
        }
        */
        //resume tnode
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QTNODE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qtnode, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TNODE_NAME);  
        HIO_INIT(hibase->tnodeio, p, st, TNODE, 1, TNODE_INCRE_NUM);
        if(hibase->tnodeio.fd  > 0 && (tnode = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            hibase->tnodeio.left = 0;
            for(i = 1; i < hibase->tnodeio.total; i++)
            {
                if(tnode[i].status && (n = strlen(tnode[i].name)) > 0)
                {
                    dp = (void *)((long)(i + 1));
                    TRIETAB_ADD(hibase->mtnode, tnode[i].name, n, dp);
                    hibase->tnodeio.current = i;
                    if(tnode[i].uid > hibase->uid_max) hibase->uid_max = tnode[i].uid;
                    if(tnode[i].nchilds > hibase->tnode_childs_max)
                        hibase->tnode_childs_max = tnode[i].nchilds;
                }
                else hibase->tnodeio.left++;
            }
        }
        //resume urlnode
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QURLNODE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qurlnode, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_URLNODE_NAME);
        HIO_INIT(hibase->urlnodeio, p, st, URLNODE, 0, URLNODE_INCRE_NUM);
        RESUME_STATE(hibase, urlnodeio);
        /*
        //HIO_MMAP(hibase->urlnodeio, URLNODE, URLNODE_INCRE_NUM);
        if(hibase->urlnodeio.fd  > 0 && (urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            hibase->urlnodeio.left = 0;
            for(i = 1; i < hibase->urlnodeio.total; i++)
            {
                if(urlnode[i].status > 0)
                {
                    hibase->urlnodeio.current = i;
                }
                else
                {
                    hibase->urlnodeio.left++;
                }
            }
            //HIO_MUNMAP(hibase->urlnodeio);
        }*/
        //urlio
        sprintf(path, "%s%s", hibase->basedir, HIBASE_URI_NAME);
        HIO_INIT(hibase->uriio, p, st, URI, 0, URI_INCRE_NUM);
        if(hibase->uriio.fd < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        RESUME_STATE(hibase, uriio);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_MMTREE_NAME);
        hibase->mmtree = mmtree_init(path);
    }
    return -1;
}

/* check table exists */
int hibase_db_uid_exists(HIBASE *hibase, int tableid, char *name, int len)
{
    ITABLE *tab = NULL;
    int uid = -1, i = 0;
    void *dp = NULL;

    if(hibase && name && len > 0 && hibase->mdb)
    {
        MUTEX_LOCK(hibase->mutex);
        TRIETAB_GET(hibase->mdb, name, len, dp);
        if((uid = (int)((long)dp)) == 0) --uid;
        if(uid < 0 && tableid >= 0)
        {
            uid = ++(hibase->db_uid_max);
            dp = (void *)((long)uid);
            TRIETAB_ADD(hibase->mdb, name, len, dp);
        }
        else
        {
            if(uid > 0 && tableid >= 0 && tableid < hibase->tableio.total
                    && (tab = (ITABLE *)hibase->tableio.map) && tab != (ITABLE *)-1)
            {
                for(i = 0; i < FIELD_NUM_MAX; i++)
                {
                    if(tab[tableid].fields[i].uid == uid)
                    {
                        uid = 0;
                        break;
                    }
                }
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return uid;
}

/* add table */
int hibase_add_table(HIBASE *hibase, char *table_name)
{
    int id = -1, uid = -1, i = 0, n = 0;
    ITABLE *tab = NULL;
    void *dp = NULL;

    if(hibase && (n = strlen(table_name))  > 0) 
    {
        uid = hibase_db_uid_exists(hibase, -1, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(hibase->tableio.left == 0)
        {
            HIO_MMAP(hibase->tableio, ITABLE, TABLE_INCRE_NUM);
        }        
        if(hibase->tableio.left > 0 && (tab = HIO_MAP(hibase->tableio, ITABLE))) 
        {
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(tab[i].uid == uid)
                {
                    uid = 0;
                    break;
                }
                //check exists
                else if(id < 0 && tab[i].status == TAB_STATUS_INIT)
                {
                    id = i;
                }
            }
            //set table
            if(id >= 0 && uid != 0)
            {
                memset(&(tab[id]), 0, sizeof(ITABLE));
                tab[id].status = TAB_STATUS_OK;
                memcpy(tab[id].name, table_name, n);
                if(uid > 0)
                    tab[id].uid = uid;
                else 
                {
                    tab[id].uid = ++(hibase->db_uid_max);
                    dp = (void *)((long)(tab[id].uid));
                    TRIETAB_ADD(hibase->mdb, table_name, n, dp);
                    hibase->tableio.left--;
                }
            }else id = -1;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    else id = -1;
    return id;
}

/* get table */
int hibase_get_table(HIBASE *hibase, int tableid, ITABLE *ptable)
{
    ITABLE *tab = NULL;
    int id = -1;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        id = tableid;
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            memcpy(ptable, &(tab[id]), sizeof(ITABLE)); 
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* rename table */
int hibase_rename_table(HIBASE *hibase, int tableid, char *table_name)
{
    int id = -1, uid =-1, i = 0, n = 0;
    ITABLE *tab = NULL;
    void*dp = NULL;

    if(hibase && hibase->mdb && (n = strlen(table_name)) > 0)
    {
        uid = hibase_db_uid_exists(hibase, -1, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(tableid >= 0 && tableid < hibase->tableio.total 
                && (tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            if(uid > 0)
            {
                for(i = 0; i < hibase->tableio.total; i++)
                {
                    if(tab[i].uid == uid)
                    {
                        //name exists
                        uid = 0;
                        break;
                    }
                }
            }
            else
            {
                uid = ++(hibase->db_uid_max);
                dp = (void *)((long) uid);
                TRIETAB_ADD(hibase->mdb, table_name, n, dp);
            }
            if(uid > 0)
            {
                tab[tableid].uid = uid;
                memset(tab[tableid].name, 0, TABLE_NAME_MAX);
                memcpy(tab[tableid].name, table_name, n);
                id = tableid;
                //failed
            }else id = uid;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* delete table */
int hibase_delete_table(HIBASE *hibase, int tableid)
{
    ITABLE *tab = NULL;
    int id = -1;

    if(hibase && hibase->mdb && tableid >= 0 && tableid < hibase->tableio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
                && tab[tableid].status == TAB_STATUS_OK)
        {
            memset(&(tab[tableid]), 0, sizeof(ITABLE));
            hibase->tableio.left++;
            id = tableid;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* add new field */
int hibase_add_field(HIBASE *hibase, int tableid, char *name, int type, int flag)
{
    int from = 0, to = 0, i = 0, n = 0, id = -1, uid = -1;
    ITABLE *tab = NULL;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total 
            && (n = strlen(name)) > 0 && n < FIELD_NAME_MAX && (type & FTYPE_ALL) 
            && (uid = hibase_db_uid_exists(hibase, tableid, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
                && tab[tableid].nfields < FIELD_NUM_MAX)
        {
            if(flag >= 0 && (flag & F_IS_INDEX) > 0){from = 0; to = HI_INDEX_MAX;}
            else {from = HI_INDEX_MAX; to = FIELD_NUM_MAX;}
            for(i = from; i < to; i++)
            {
                if(tab[tableid].fields[i].status == FIELD_STATUS_INIT)
                {
                    tab[tableid].fields[i].status = FIELD_STATUS_OK;
                    tab[tableid].fields[i].type   = type;
                    if(flag >= 0) tab[tableid].fields[i].flag   |= flag;
                    tab[tableid].fields[i].uid    = uid;
                    memcpy(tab[tableid].fields[i].name, name, n);
                    tab[tableid].nfields++;
                    id = i;
                    break;
                }
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* update  field */
int hibase_update_field(HIBASE *hibase, int tableid, int fieldid, 
        char *name, int type, int flag)
{
    int from = 0, to = 0, i = 0, n = 0, id = -1, uid = -1, 
        isindex = 0, is_need_move = 0, old_is_index = 0;
    ITABLE *tab = NULL;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total 
            && fieldid >= 0 && fieldid < FIELD_NUM_MAX)
    {
        if(name && (n = strlen(name)) > 0 && n < FIELD_NAME_MAX 
                && (uid = hibase_db_uid_exists(hibase, tableid, name, n)) == 0)
        {
            return -1;
        }
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
            && tab[tableid].fields[fieldid].status == FIELD_STATUS_OK)
        {
            if(flag >= 0 ) isindex = (flag & F_IS_INDEX);
            old_is_index = (tab[tableid].fields[fieldid].flag & F_IS_INDEX);
            if(isindex > 0 && old_is_index == 0)
            {
                is_need_move = 1; from = 0; to = HI_INDEX_MAX;
            }
            else if(isindex == 0 && old_is_index > 0)
            {
                is_need_move = 1; from = HI_INDEX_MAX; to = FIELD_NUM_MAX;
            }
            if(is_need_move)
            {
                for(i = from; i < to; i++)
                {
                    if(tab[tableid].fields[i].status == FIELD_STATUS_INIT)
                    {
                        memcpy(&(tab[tableid].fields[i]), &(tab[tableid].fields[fieldid]), 
                                sizeof(IFIELD));
                        if(type > 0) tab[tableid].fields[i].type   = type;
                        memset(&(tab[tableid].fields[fieldid]), 0, sizeof(IFIELD));
                        id = i;
                        break;
                    }
                }
            }
            else
            {
                if(type & FTYPE_ALL) tab[tableid].fields[fieldid].type = type;
                id = fieldid;
            }
            if(id >= 0 && uid > 0)
            {
                tab[tableid].fields[id].uid    = uid;
                memset(tab[tableid].fields[id].name, 0, FIELD_NAME_MAX);
                memcpy(tab[tableid].fields[id].name, name, n);
            }
            if(id >= 0 && flag >= 0)
            {
                tab[tableid].fields[id].flag = (tab[tableid].fields[id].flag & flag);
                tab[tableid].fields[id].flag |= flag;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* delete field */
int hibase_delete_field(HIBASE *hibase, int tableid, int fieldid)
{
    ITABLE *tab = NULL;
    int id = -1;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total
            && fieldid >= 0 && fieldid < FIELD_NUM_MAX)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            if(tab[tableid].nfields > 0 && tab[tableid].fields[fieldid].status == FIELD_STATUS_OK)
            {
                memset(&(tab[tableid].fields[fieldid]), 0, sizeof(IFIELD));
                tab[tableid].nfields--;
                id = fieldid;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* view  table */
int hibase_view_table(HIBASE *hibase, int tableid, char *block)
{
    int ret = -1, i = 0;
    ITABLE *tab = NULL;
    char buf[HI_BUF_SIZE], *p =  NULL, *pp = NULL;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
            && tab[tableid].status == TAB_STATUS_OK)
        {
            p = buf;
            p += sprintf(p, "({'id':'%d', 'name':'%s', 'nfields':'%d', 'fields':[", 
                    tableid, tab[tableid].name, tab[tableid].nfields);
            pp = p;
            for(i = 0; i < FIELD_NUM_MAX; i++)
            {
                if(tab[tableid].fields[i].status == FIELD_STATUS_OK)
                {
                    p += sprintf(p, "{'id':'%d', 'name':'%s', 'type':'%d', "
                            "'flag':'%d', 'status':'%d'},", i, tab[tableid].fields[i].name, 
                            tab[tableid].fields[i].type, tab[tableid].fields[i].flag, 
                            tab[tableid].fields[i].status);
                }
            }
            if(p != pp) --p;
            p += sprintf(p, "%s", "]})");
            ret = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* list tables */
int hibase_list_table(HIBASE *hibase, char *block)
{
    int ret = -1, i = 0;
    ITABLE *tab = NULL;
    char buf[HI_BUF_SIZE], *p =  NULL, *pp = NULL;

    if(hibase && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            p = buf;
            p += sprintf(p, "%s","({'tables':[");
            pp = p;
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(tab[i].status == TAB_STATUS_OK)
                    p += sprintf(p, "{'id':'%d', 'name':'%s', 'nfields':'%d'},", 
                            i, tab[i].name, tab[i].nfields);
            }
            if(p == pp) ret = 0;
            else
            {
                --p;
                p += sprintf(p, "%s", "]})");
                ret = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* view DB */
int hibase_view_database(HIBASE *hibase, char *block)
{
    int ret = -1, i = 0, j = 0;
    ITABLE *tab = NULL;
    char buf[HI_BUF_SIZE], *p =  NULL, *pp = NULL, *ppp = NULL;

    if(hibase && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            p = buf;
            p += sprintf(p, "%s","({'tables':{");
            pp = p;
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(tab[i].status == TAB_STATUS_OK)
                {
                    p += sprintf(p, "'%d':{'id':'%d', 'name':'%s', 'nfields':'%d', 'fields':{", 
                        i, i, tab[i].name, tab[i].nfields);
                    ppp = p;
                    for(j = 0; j < FIELD_NUM_MAX; j++)
                    {
                        if(tab[i].fields[j].status == FIELD_STATUS_OK)
                        {
                            p += sprintf(p, "'%d':{'id':'%d', 'name':'%s', 'type':'%d', "
                                    "flag:'%d', status:'%d'},", j, j, tab[i].fields[j].name, 
                                    tab[i].fields[j].type, tab[i].fields[j].flag, 
                                    tab[i].fields[j].status);
                        }
                    }
                    if(p != ppp) --p;
                    p += sprintf(p, "%s", "}},");
                }
            }
            if(p != pp)--p;
            p += sprintf(p, "%s", "}})");
            ret = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}
/* check tnode exists */
int hibase_tnode_exists(HIBASE *hibase, TNODE *parent, char *name, int len)
{
    int uid = -1, id = -1, old = 0;
    void *dp = NULL;

    if(hibase && name && len > 0 && hibase->mtnode && parent)
    {
        MUTEX_LOCK(hibase->mutex);
        TRIETAB_GET(hibase->mtnode, name, len, dp);
        if((uid = (long)dp) <= 0)
        {
            uid = ++(hibase->uid_max);
            dp = (void *)((long)uid);
            TRIETAB_ADD(hibase->mtnode, name, len, dp);
        }
        if(parent->childs_rootid <= 0)
        {
            if((id = parent->childs_rootid = mmtree_new_tree(hibase->mmtree, uuid, 0)) > 0)
            {
                parent->nchilds++;
                is_need_overwrite = 1;
            }
        }
        else
        {
            id = mmtree_insert(hibase->mmtree, parent->childs_rootid, uuid, 0, &old);
            if(id > 0 && old == 0)
            {
                parent->nchilds++;
                is_need_overwrite = 1;
            }
            else id = -1;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* add tnode */
int hibase_add_tnode(HIBASE *hibase, int parentid, char *name)
{
    int tnodeid = -1, mid = -1, n = 0, *px = NULL, x = 0;
    TNODE tnode = {0}, parent = {0};

    if(hibase && name && (n = strlen(name))  > 0 
            && parentid >= 0 && parentid < hibase->tnodeio.total
            && pread(hibase->tnodeio.fd, &parent, sizeof(TNODE), 
                (off_t)sizeof(TNODE) * (off_t)parentid) > 0
            && (mid = hibase_tnode_exists(hibase, &parent, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->tnodeio.left == 0)
        {
            HIO_INCRE(hibase->tnodeio, TNODE, TNODE_INCRE_NUM);
        }
        px = &x;
        if(FQUEUE_POP(hibase->qtnode, int, px) == 0)
            tnodeid = x;
        else
            tnodeid = ++(hibase->tnodeio.current);
        if(ID_IS_VALID(hibase, tnodeio, tnodeid))
        {
            mmtree_set_data(hibase->mmtree, mid, tnodeid);
            memcpy(tnode.name, name, n); 
            tnode.id = tnodeid;
            tnode.status = TNODE_STATUS_OK;
            tnode.parent = parentid;
            tnode.level = parent.level + 1;
            parent.nchilds++;
            if(parent.nchilds > hibase->tnode_childs_max)
                hibase->tnode_childs_max = parent.nchilds;
            hibase->tnodeio.left--;
            if(pwrite(hibase->tnodeio.fd, &tnode, sizeof(TNODE), 
                        (off_t)sizeof(TNODE) * (off_t) tnodeid) > 0)
            {
                pwrite(hibase->tnodeio.fd, &parent, sizeof(TNODE), 
                        (off_t)sizeof(TNODE) * (off_t) parentid);
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    else tnodeid = -1;
    return tnodeid;
}

/* get tnode */
int hibase_get_tnode(HIBASE *hibase, int tnodeid, TNODE *ptnode)
{
    int ret = -1;

    if(hibase && ptnode && ID_IS_VALID(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        if(pread(hibase->tnodeio.fd, ptnode, sizeof(TNODE), 
                    (off_t)tnodeid * (off_t)sizeof(TNODE)) > 0)
        {
            ret = tnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get tnode templates */
int hibase_get_tnode_templates(HIBASE *hibase, int tnodeid, ITEMPLATE **templates)
{
    int n = -1, x = -1, i = 0, key = 0, data = 0;
    ITEMPLATE *template = NULL;
    TNODE tnode = {0};

    if(hibase && templates && ID_IS_VALID2(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        if(pread(hibase->tnodeio.fd, &tnode,sizeof(TDNOE),(off_t)sizeof(TNODE)*(off_t)tnodeid) > 0 
            &&  tnode.status > 0 && tnode.templates_rootid > 0 
            && (n = tnode.ntemplates) > 0 && hibase->templateio.fd > 0 
            && (x = mmtree_min(hibase->mmtree, tnode.templates_rootid, &key, &data)) > 0
            && (template = *templates = (ITEMPLATE *)calloc(n, sizeof(ITEMPLATE))))
        {
            i = 0;
            do
            {
                if(pread(hibase->templateio.fd, template, sizeof(ITEMPLATE), 
                            (off_t)sizeof(ITEMPLATE) * (off_t)x) > 0)
                {
                    ++i;
                    ++template;
                }
                else break; 
            }while((x = mmtree_next(hibase->mmtree, x, &key, &data)) > 0);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

void hibase_free_templates(ITEMPLATE *templates)
{
    if(templates) free(templates);
    return ;
}
/* get tnode childs */
int hibase_get_tnode_childs(HIBASE *hibase, int tnodeid, TNODE *ptnode)
{
    TNODE *tnode = NULL, *parent = NULL;
    int i = 0, x = 0;

    if(hibase && hibase->mtnode && ptnode)
    {
        MUTEX_LOCK(hibase->mutex);
        if(tnodeid >= 0 && tnodeid < hibase->tnodeio.total 
                && (tnode = (TNODE *)(hibase->tnodeio.map)) && tnode != (TNODE *)-1)
        {
            parent = &(tnode[tnodeid]);
            x = parent->first;
            while(i < parent->nchilds && x >= 0 && x < hibase->tnodeio.total)
            {
                memcpy(ptnode, &(tnode[x]), sizeof(TNODE)); 
                x = tnode[x].next;
                ptnode++;
                i++;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return i;
}

/* view tnode childs */
int hibase_view_tnode_childs(HIBASE *hibase, int tnodeid, char *block)
{
    TNODE *tnode = NULL, *parent = NULL;
    char buf[HI_BUF_SIZE], *p = NULL;
    int i = 0, x = 0, n = -1;

    if(hibase && hibase->mtnode)
    {
        MUTEX_LOCK(hibase->mutex);
        if(tnodeid >= 0 && tnodeid < hibase->tnodeio.total 
                && (tnode = (TNODE *)(hibase->tnodeio.map)) && tnode != (TNODE *)-1)
        {
            parent = &(tnode[tnodeid]);
            p = buf;
            p += sprintf(p, "({'id':'%d','nchilds':'%d', 'childs':[", tnodeid, parent->nchilds);
            x = parent->first;
            while(i < parent->nchilds && x >= 0 && x < hibase->tnodeio.total)
            {
                if(i < (parent->nchilds - 1))
                    p += sprintf(p, "{'id':'%d','name':'%s','nchilds':'%d'},",
                            tnode[x].id, tnode[x].name, tnode[x].nchilds);
                else
                    p += sprintf(p, "{'id':'%d','name':'%s','nchilds':'%d'}",
                            tnode[x].id, tnode[x].name, tnode[x].nchilds);
                x = tnode[x].next;
                i++;
            }
            p += sprintf(p, "%s", "]})\r\n");
            n = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}


/* update tnode */
int hibase_update_tnode(HIBASE *hibase, int tnodeid, char *name)
{
    TNODE *tnode = NULL, node = {0};
    int id = -1, uid = -1, n = 0;

    if(hibase && hibase->mtnode && name && (n = strlen(name)) > 0 && n < TNODE_NAME_MAX
        && tnodeid > 0 && tnodeid < hibase->tnodeio.total
        && hibase_get_tnode(hibase, tnodeid, &node) >= 0 
        && (uid = hibase_tnode_exists(hibase, node.parent, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode = (TNODE *)(hibase->tnodeio.map)) 
                && tnode != (TNODE *)-1 && tnode[tnodeid].status > 0)
        {
            memset(tnode[tnodeid].name, 0, TNODE_NAME_MAX);
            memcpy(tnode[tnodeid].name, name, n); 
            tnode[tnodeid].uid = uid;
            id = tnodeid;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* reset tnode */
int hibase_reset_tnode(HIBASE *hibase, int tnodeid)
{
    TNODE *tnodes = NULL, *parent = NULL;
    int id = -1, i = 0, x = 0, xx = 0, n = 0, *px = NULL;
    if(hibase)
    {
        if((id = tnodeid) > 0 && id < hibase->tnodeio.total 
                && (tnodes = (TNODE *)(hibase->tnodeio.map)) 
                && tnodes != (TNODE *)-1 && tnodes[id].status > 0)
        {
            if((x = tnodes[id].parent) >= 0 && x < hibase->tnodeio.total)
            {
                parent = &(tnodes[x]);
                if(parent->first == id) parent->first = tnodes[id].next;
                if(parent->last == id) parent->last = tnodes[id].prev;
                parent->nchilds--;
            }
            if((n = tnodes[id].prev) > 0 && n < hibase->tnodeio.total)
            {
                tnodes[n].next = tnodes[n].next;
            }
            if((n = tnodes[id].next) > 0 && n < hibase->tnodeio.total)
            {
                tnodes[n].prev = tnodes[n].prev;
            }
            px = &id;
            FQUEUE_PUSH(hibase->qtnode, int, px);
            i = 0;
            n = tnodes[id].nchilds;
            x = tnodes[id].first;
            while(i < n && x > 0 && x < hibase->tnodeio.total)
            {
                xx = tnodes[x].next;
                hibase_reset_tnode(hibase, x);
                x = xx;
                i++;
            }
            /*
            fprintf(stdout, "%d::delete %d nodes from node:%d ->%s\n",
                    __LINE__, n, id, tnodes[id].name);
            */
            memset(&(tnodes[id]), 0, sizeof(TNODE));
            hibase->tnodeio.left++;
        }
    }
    return id;
}
/* delete tnode */
int hibase_delete_tnode(HIBASE *hibase, int tnodeid)
{
    int id = -1;

    if(hibase)
    {
        MUTEX_LOCK(hibase->mutex);
        id = hibase_reset_tnode(hibase, tnodeid);
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* List tnode */
int hibase_list_tnode(HIBASE *hibase, int tnodeid, FILE *fp)
{
    TNODE *tnode = NULL;
    int i = 0, x = 0;

    if(hibase && tnodeid >= 0 && tnodeid < hibase->tnodeio.total
        && (tnode = (TNODE *)(hibase->tnodeio.map)) && tnode != (TNODE *)-1)
    {
        for(i = 0; i < tnode[tnodeid].level; i++)
        {
            fprintf(fp, "%s", "  ┆");
        }
        if(tnode[tnodeid].nchilds > 0)
        {
            fprintf(fp, "--+[%d]%s[%d]\n", tnode[tnodeid].nchilds, tnode[tnodeid].name, tnodeid);
        }
        else
        {
            fprintf(fp, "---%s[%d]\n", tnode[tnodeid].name, tnodeid);
        }
        x = tnode[tnodeid].first;
        for(i = 0; i < tnode[tnodeid].nchilds; i++)
        {
            if(x > 0 && x < hibase->tnodeio.total)
            {
                hibase_list_tnode(hibase, x, fp);
            }
            x = tnode[x].next;
        }
        return 0;
    }
    return -1;
}
/* add template */
int hibase_add_template(HIBASE *hibase, int tnodeid, ITEMPLATE *template)
{
    int templateid = -1, x = 0, *px = NULL;
    TNODE *tnode = NULL;

    if(hibase && tnodeid >= 0 && tnodeid < hibase->tnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->templateio.left == 0)
        {
            HIO_INCRE(hibase->templateio, ITEMPLATE, TEMPLATE_INCRE_NUM);
            UPDATE_STATE(hibase, templateio);
        }
        px = &x;
        if(FQUEUE_POP(hibase->qtemplate, int, px) == 0)
            templateid = x;
        else
            templateid = INCRE_STATE(hibase, templateio, current);
        if((tnode = HIO_MAP(hibase->tnodeio, TNODE))
            && templateid >= 0 && templateid < hibase->templateio.total)
        {
            template->status = TEMPLATE_STATUS_OK;
            //memcpy(&(ptemplate[templateid]), template, sizeof(ITEMPLATE));
            //ptemplate[templateid].status = TEMPLATE_STATUS_OK;
            if(tnode[tnodeid].ntemplates == 0)
            {
                tnode[tnodeid].template_first = tnode[tnodeid].template_last = templateid;
            }
            else
            {
               x =  tnode[tnodeid].template_last;
               if(pwrite(hibase->templateio.fd, &templateid, sizeof(int), 
                       (off_t)x * (off_t)sizeof(ITEMPLATE) 
                       + (off_t)((void *)&(template->next) - (void *)template)) >0)
                template->prev = x;
               //ptemplate[x].next = templateid;
               //ptemplate[templateid].prev = x;
            }
            if(pwrite(hibase->templateio.fd, template, sizeof(ITEMPLATE), 
                        (off_t)sizeof(ITEMPLATE) * (off_t) templateid) > 0)
            {
                tnode[tnodeid].template_last = templateid;
                tnode[tnodeid].ntemplates++;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return templateid;
}

/* get template */
int hibase_get_template(HIBASE *hibase, int templateid, ITEMPLATE *template)
{
    int ret = -1;

    if(hibase && templateid >= 0 && templateid < hibase->templateio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->templateio.fd > 0 && pread(hibase->templateio.fd, template, 
                    sizeof(ITEMPLATE), (off_t)sizeof(ITEMPLATE) * (off_t)templateid) > 0)
        {
            //memcpy(template, &(ptemplate[templateid]), sizeof(ITEMPLATE));
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* update template */
int hibase_update_template(HIBASE *hibase, int templateid, ITEMPLATE *template)
{
    int ret = -1;

    if(hibase && templateid >= 0 && templateid < hibase->templateio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        template->status = TEMPLATE_STATUS_OK;
        if(hibase->templateio.fd > 0 && pwrite(hibase->templateio.fd, template, 
                    ((void *)&(template->prev) - (void *)template), //ignore next/prev 
                    (off_t)sizeof(ITEMPLATE) * (off_t)templateid) > 0)
        {
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* delete template */
int hibase_delete_template(HIBASE *hibase, int tnodeid, int templateid)
{
    int ret = -1, x = 0, *px = NULL;
    ITEMPLATE template = {0};
    TNODE *tnode = NULL;

    if(hibase && tnodeid >= 0 && tnodeid < hibase->tnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode = HIO_MAP(hibase->tnodeio, TNODE)) && templateid >= 0 
            && templateid < hibase->templateio.total && hibase->templateio.fd > 0
            && pread(hibase->templateio.fd, &template, sizeof(ITEMPLATE),
                (off_t)sizeof(ITEMPLATE) * (off_t) templateid) > 0)
        {
            if((x = template.prev) > 0)
            {
                pwrite(hibase->templateio.fd, &(template.next), sizeof(int), 
                    (off_t)x * (off_t)sizeof(ITEMPLATE) 
                    + (off_t)((void *)&(template.next) - (void *)&template));
                //ptemplate[x].next = ptemplate[templateid].next;
            }
            if((x = template.next) > 0)
            {
                pwrite(hibase->templateio.fd, &(template.prev), sizeof(int), 
                    (off_t)x * (off_t)sizeof(ITEMPLATE) 
                    + (off_t)((void *)&(template.prev) - (void *)&template));
                //x = ptemplate[templateid].next;
                //ptemplate[x].prev = ptemplate[templateid].prev;
            }
            if(tnode[tnodeid].template_first == templateid)
                tnode[tnodeid].template_first = template.next;
            if(tnode[tnodeid].template_last == templateid)
                tnode[tnodeid].template_last = template.prev;
            //memset(&(ptemplate[templateid]), 0, sizeof(ITEMPLATE));
            px = &templateid;
            FQUEUE_PUSH(hibase->qtemplate, int, px);
            tnode[tnodeid].ntemplates--;
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* view templates */
int hibase_view_templates(HIBASE *hibase, int tnodeid, char *block)
{
    ITEMPLATE template = {0};
    TNODE *tnode = NULL;
    int n = -1, x = 0, i = 0;
    char buf[HI_BUF_SIZE], xbuf[HI_BUF_SIZE], *pattern = NULL, *p = NULL;

    if(hibase && tnodeid >= 0 && tnodeid < hibase->tnodeio.total && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode = HIO_MAP(hibase->tnodeio, TNODE)) && hibase->templateio.fd > 0)
        {
            p = buf;
            p += sprintf(p, "({'id':'%d', 'name':'%s', 'ntemplates':'%d','templates':[", 
                    tnodeid, tnode[tnodeid].name, tnode[tnodeid].ntemplates);
            if(tnode[tnodeid].ntemplates > 0)
            {
                x = tnode[tnodeid].template_first;
                while(x > 0 && pread(hibase->templateio.fd, &template, sizeof(ITEMPLATE),
                        (off_t)x * (off_t)sizeof(ITEMPLATE)) > 0)
                {
                    if((n = strlen(template.pattern)) > 0 && (HI_BUF_SIZE > BASE64_LEN(n)))
                    {
                        base64_encode(xbuf, (unsigned char *)template.pattern, n);
                        pattern = xbuf;
                    }
                    else pattern = "";
                    //fprintf(stdout, "%d::%s\n%s\n", __LINE__, ptemplate[x].pattern, pattern);
                    p += sprintf(p, "{'id':'%d', 'tableid':'%d', 'flags':'%d', "
                            "'pattern':'%s', 'link':'%s',", x, template.tableid,
                             template.flags, pattern, template.link);
                    {
                        p += sprintf(p, "'linkmap':{'fieldid':'%d','nodeid':'%d', 'flag':'%d'},", 
                            template.linkmap.fieldid, template.linkmap.nodeid, 
                            template.linkmap.flag);
                    }
                    p += sprintf(p, "'url':'%s', 'nfields':'%d', 'map':[", 
                            template.url, template.nfields);
                    if(template.nfields > 0)
                    {
                        i = 0;
                        while(i < template.nfields && template.nfields < FIELD_NUM_MAX)
                        {
                            p += sprintf(p, "{'fieldid':'%d', 'nodeid':'%d', 'flag':'%d'},", 
                                    template.map[i].fieldid, template.map[i].nodeid, 
                                    template.map[i].flag);
                            i++;
                        }
                        --p;
                    }
                    p += sprintf(p, "%s","]},");
                    x = template.next;
                }
                *--p = '\0';
            }
            p += sprintf(p, "%s", "]})");
            n = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}
/* hibase urlid => urlnode/node map check */
int hibase_urlmap_exists(HIBASE *hibase, int urlid, int tnodeid, int parentid)
{
    int n = 0, ret = -1, key = 0, old_urlnode_treeid = 0, old_tnode_treeid = 0,
        urlnode_treeid = 0, tnode_treeid = 0;
    URI uri = {0};

    if(hibase && hibase->mmtree && urlid >= 0 && tnodeid > 0 && parentid >= 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->uriio.left == 0 || (urlid >= hibase->uriio.total))
        {
            n = urlid/URI_INCRE_NUM;
            if((urlid/URI_INCRE_NUM)) ++n;
            hibase->uriio.left = (n * URI_INCRE_NUM) - hibase->uriio.total;
            hibase->uriio.size = (off_t)n * (off_t)URI_INCRE_NUM * (off_t)sizeof(URI);
            hibase->uriio.total = n * URI_INCRE_NUM;
            ftruncate(hibase->uriio.fd, hibase->uriio.size);
            UPDATE_STATE(hibase, uriio);
            if((uri.urlnode_treeid = mmtree_new_tree(hibase->mmtree, parentid)) > 0
                && (uri.tnode_treeid = mmtree_new_tree(hibase->mmtree, tnodeid)) > 0)
            {
                pwrite(hibase->uriio.fd, &uri, sizeof(URI), (off_t)urlid * (off_t)sizeof(URI));
                ret = 0;
                fprintf(stdout, "%s::%d add url:%d to urlnode:%d tnode:%d\n", __FILE__, __LINE__, urlid, parentid, tnodeid);
                goto end;
            }
        }
        else
        {
            if(pread(hibase->uriio.fd, &uri, sizeof(URI), (off_t)urlid * (off_t)sizeof(URI)) > 0)
            {
                if((urlnode_treeid = mmtree_insert(hibase->mmtree, 
                                uri.urlnode_treeid, parentid, &old_urlnode_treeid)) > 0
                    && (tnode_treeid = mmtree_insert(hibase->mmtree, uri.tnode_treeid, 
                            tnodeid, &old_tnode_treeid)) > 0
                    && old_urlnode_treeid == 0 && old_tnode_treeid == 0)
                {
                    fprintf(stdout, "%s::%d add url:%d to urlnode:%d tnode:%d\n", __FILE__, __LINE__, urlid, parentid, tnodeid);
                    ret = 0;
                    goto end;
                }
                else
                {
                    if(old_urlnode_treeid == 0 && urlnode_treeid > 0)
                        mmtree_remove(hibase->mmtree, urlnode_treeid, &key);
                    if(old_tnode_treeid == 0 && tnode_treeid > 0)
                        mmtree_remove(hibase->mmtree, tnode_treeid, &key);
                    fprintf(stdout, "%s::%d exists url:%d at urlnode:%d tnode:%d\n", __FILE__, __LINE__, urlid, parentid, tnodeid);
                }
                //fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            }
        }
        //fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
end:
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* hibase check urlid exist */
int hibase_exists_urlnode(HIBASE *hibase, int nodeid, int parentid, int urlid)
{
    int urlnodeid = -1, x = 0, z = 0;
    URLNODE *urlnode = NULL;
    TNODE *tnode = NULL;

    if(hibase && urlid >= 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            if(parentid > 0 && parentid < hibase->urlnodeio.total 
                && urlnode[parentid].nchilds > 0) 
                    x = urlnode[parentid].first;
            if(nodeid > 0 && nodeid < hibase->tnodeio.total
                    && (tnode = HIO_MAP(hibase->tnodeio, TNODE)) 
                    && tnode[nodeid].nurlnodes > 0)
                    z = tnode[nodeid].urlnode_first;
            while(x > 0 || z > 0)
            {
                if(x > 0)
                {
                    if(urlnode[x].urlid == urlid)
                    {
                        urlnodeid = x;
                        goto end;
                        break;
                    }
                    else x = urlnode[x].next;
                }
                if(z > 0)
                {
                    if(urlnode[z].urlid == urlid)
                    {
                        urlnodeid = z;
                        goto end;
                        break;
                    }
                    else z = urlnode[z].node_next;
                }
            }
        }   
end:
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}

int hibase_add_urlnode(HIBASE *hibase, int nodeid, int parentid, int urlid, int level)
{
    int urlnodeid = -1, x = 0, *px = NULL;
    URLNODE urlnode = {0}, parent = {0};
    TNODE tnode = {0};

    if(hibase && nodeid > 0 && parentid >= 0 && urlid >= 0 
            && hibase_urlmap_exists(hibase, urlid, nodeid, parentid) == 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->urlnodeio.left == 0)
        {
            HIO_MMAP(hibase->urlnodeio, ITEMPLATE, TEMPLATE_INCRE_NUM);
            UPDATE_STATE(hibase, urlnodeio);
        }
        px = &x;
        if(FQTOTAL(hibase->qurlnode) > 0 && FQUEUE_POP(hibase->qurlnode, int, px) == 0)
        {
            urlnodeid = x;
        }
        else
        {
            urlnodeid = ++(hibase->urlnodeio.current);
            UPDATE_STATE(hibase, urlnodeio);
        }

        if(urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
        {
            fprintf(stdout, "%d::nodeid:%d parentid:%d urlid:%d level:%d id:%d\n", __LINE__, nodeid, parentid, urlid, level, urlnodeid);
            urlnode.status = URLNODE_STATUS_OK;
            if(level >= 0) urlnode.level = level;
            if(level > 0) 
            {
                px = &urlnodeid;
                FQUEUE_PUSH(hibase->qtask, int, px);
            }
            urlnode.id = urlnodeid;
            urlnode.parentid = parentid;
            urlnode.urlid = urlid;
            urlnode.nodeid = nodeid;
            if(parentid >= 0 && parentid < hibase->urlnodeio.total 
                    && pread(hibase->urlnodeio.fd , &parent, sizeof(URLNODE), 
                        (off_t)sizeof(URLNODE) * (off_t) parentid) > 0)
            {
                if(parent.nchilds == 0)
                {
                    parent.first = parent.last = urlnodeid;
                }
                else
                {
                    x = parent.last;
                    urlnode[x].next = urlnodeid;
                    urlnode.prev = x;
                    parent.last = urlnodeid;
                    pwrite(hibase->urlnodeio.fd , &urlnodeid, sizeof(int), 
                            (off_t)sizeof(URLNODE) * (off_t) x  + (&(urlnode.next) - &urlnode));
                }
                parent.nchilds++;
                pwrite(hibase->urlnodeio.fd , &parent, sizeof(URLNODE), 
                        (off_t)sizeof(URLNODE) * (off_t) parentid) 
                    if(nodeid > 0 && nodeid < hibase->tnodeio.total
                            && (tnode = HIO_MAP(hibase->tnodeio, TNODE)))
                    {
                        if(tnode[nodeid].nurlnodes == 0)
                            tnode[nodeid].urlnode_first = tnode[nodeid].urlnode_last = urlnodeid;
                        else
                        {
                            x = tnode[nodeid].urlnode_last;
                            urlnode.node_prev = x;
                            tnode[nodeid].urlnode_last = urlnodeid;
                            pwrite(hibase->urlnodeio.fd , &urlnodeid, sizeof(int), 
                                    (off_t)sizeof(URLNODE) * (off_t) x  + (&(urlnode.node_next) - &urlnode));

                        }
                        tnode[nodeid].nurlnodes++;
                    }
                pwrite(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE),
                        (off_t)urlnodeid * (off_t)sizeof(URLNODE)) 
                    //write urlnode[parentid]->first/last
                    //write tnode[tnodeid]->first/last
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}
/* addd url node */
int hibase_add_urlnode2(HIBASE *hibase, int nodeid, int parentid, int urlid, int level)
{
    int urlnodeid = -1, x = 0, *px = NULL;
    URLNODE *urlnode = NULL, *purlnode = NULL;
    TNODE *tnode = NULL;

    if(hibase && nodeid > 0 && parentid >= 0 && urlid >= 0 
            && hibase_urlmap_exists(hibase, urlid, nodeid, parentid) == 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->urlnodeio.left == 0)
        {
            HIO_MMAP(hibase->urlnodeio, ITEMPLATE, TEMPLATE_INCRE_NUM);
        }
        px = &x;
        if(FQTOTAL(hibase->qurlnode) > 0 && FQUEUE_POP(hibase->qurlnode, int, px) == 0)
            urlnodeid = x;
        else
            urlnodeid = ++(hibase->urlnodeio.current);
        if((urlnode = HIO_MAP(hibase->urlnodeio, URLNODE))
            && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
        {
        fprintf(stdout, "%d::nodeid:%d parentid:%d urlid:%d level:%d id:%d\n", __LINE__, nodeid, parentid, urlid, level, urlnodeid);
            urlnode[urlnodeid].status = URLNODE_STATUS_OK;
            if(level >= 0) urlnode[urlnodeid].level = level;
            if(level > 0) 
            {
                px = &urlnodeid;
                FQUEUE_PUSH(hibase->qtask, int, px);
            }
            urlnode[urlnodeid].id = urlnodeid;
            urlnode[urlnodeid].parentid = parentid;
            urlnode[urlnodeid].urlid = urlid;
            urlnode[urlnodeid].nodeid = nodeid;
            if(parentid >= 0 && parentid < hibase->urlnodeio.total)
            {
                purlnode = &(urlnode[parentid]);
                if(purlnode->nchilds == 0)
                {
                    purlnode->first = purlnode->last = urlnodeid;
                }
                else
                {
                    x = purlnode->last;
                    urlnode[x].next = urlnodeid;
                    urlnode[urlnodeid].prev = x;
                    purlnode->last = urlnodeid;
                }
                purlnode->nchilds++;
            }
            if(nodeid > 0 && nodeid < hibase->tnodeio.total
                && (tnode = HIO_MAP(hibase->tnodeio, TNODE)))
            {
                if(tnode[nodeid].nurlnodes == 0)
                    tnode[nodeid].urlnode_first = tnode[nodeid].urlnode_last = urlnodeid;
                else
                {
                    x = tnode[nodeid].urlnode_last;
                    urlnode[x].node_next = urlnodeid;
                    urlnode[urlnodeid].node_prev = x;
                    tnode[nodeid].urlnode_last = urlnodeid;
                }
                tnode[nodeid].nurlnodes++;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return  urlnodeid;
}

/* update urlnode level */
int hibase_update_urlnode(HIBASE *hibase, int urlnodeid, int level)
{
    URLNODE urlnode = {0};
    int ret = -1, *px = NULL;

    if(hibase && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total && level >= 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(level >= 0) 
        {
            pwrite(hibase->urlnodeio.fd, &level, sizeof(level), 
                    (off_t)urlnodeid * (off_t)sizeof(URLNODE) 
                    + ((char *)&(url.level) - (char *)&urlnode) > 0)
        }
        if(level > 0)
        {
            px = &urlnodeid;
            FQUEUE_PUSH(hibase->qtask, int, px);
        }
        ret = urlnodeid;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* reset urlnode */
int hibase_reset_urlnode(HIBASE *hibase, URLNODE *urlnodes, int urlnodeid)
{
    int x = 0, z = 0, *px = NULL, nodeid = 0;
    TNODE *tnode = NULL;

    if(urlnodes && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
    {
        if(urlnodes[urlnodeid].nchilds > 0)
        {
            x = urlnodes[urlnodeid].first;
            while(x > 0 && x < hibase->urlnodeio.total)
            {
                if(urlnodes[x].nchilds > 0) 
                    hibase_reset_urlnode(hibase, urlnodes, x);
                x = urlnodes[x].next;
            }
        }
        if((x = urlnodes[urlnodeid].next) > 0 && x < hibase->urlnodeio.total)
        {
            urlnodes[x].prev = urlnodes[urlnodeid].prev;
        }
        if((x = urlnodes[urlnodeid].prev) > 0 && x < hibase->urlnodeio.total)
        {
            urlnodes[x].next = urlnodes[urlnodeid].next;
        }
        z = urlnodes[urlnodeid].parentid;
        if(urlnodes[z].first == urlnodeid)
            urlnodes[z].first = urlnodes[urlnodeid].next;
        if(urlnodes[z].last == urlnodeid)
            urlnodes[z].last = urlnodes[urlnodeid].prev;
        urlnodes[z].nchilds--;
        if((nodeid = urlnodes[urlnodeid].nodeid) > 0 
                && nodeid < hibase->tnodeio.total
                && (tnode = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            if(tnode[nodeid].urlnode_first == urlnodeid)
                tnode[nodeid].urlnode_first = urlnodes[urlnodeid].node_next;
            if(tnode[nodeid].urlnode_last == urlnodeid)
                tnode[nodeid].urlnode_last = urlnodes[urlnodeid].node_prev;
            tnode[nodeid].nurlnodes--;
        }
        memset(&(urlnodes[urlnodeid]), 0, sizeof(URLNODE));
        px = &urlnodeid;
        FQUEUE_PUSH(hibase->qurlnode, int, px);
    }
    return urlnodeid;
}

/* delete urlnode */
int hibase_delete_urlnode(HIBASE *hibase, int urlnodeid)
{
    URLNODE *urlnode = NULL;
    int ret = -1;
    if(hibase && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            hibase_reset_urlnode(hibase, urlnode, urlnodeid);
            ret = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get urlnode */
int hibase_get_urlnode(HIBASE *hibase, int urlnodeid, URLNODE *urlnode)
{
    int ret = -1;

    if(hibase && urlnode && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if(pread(hibase->urlnodeio.fd, urlnode, sizeof(URLNODE), 
                    (off_t)sizeof(URLNODE) * (off_t) urlnodeid) > 0)
        {
            ret = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get urlnode childs */
int hibase_get_urlnode_childs(HIBASE *hibase, int urlnodeid, URLNODE **childs)
{
    URLNODE *urlnodes = NULL, *p = NULL;
    int n = -1, x = 0;

    if(hibase && childs && urlnodeid >= 0 && urlnodeid < hibase->urlnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnodes = HIO_MAP(hibase->urlnodeio, URLNODE))
            && urlnodes[urlnodeid].nchilds > 0 
            && (p = *childs = (URLNODE *)calloc(urlnodes[urlnodeid].nchilds, sizeof(URLNODE))))
        {
            x = urlnodes[urlnodeid].first;
            n = 0;
            while(x > 0 && n < urlnodes[urlnodeid].nchilds)
            {
                memcpy(p, &(urlnodes[x]), sizeof(URLNODE));
                x = urlnodes[x].next;
                ++p;
                ++n;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* free urlnodes childs */
void hibase_free_urlnodes(URLNODE *urlnodes)
{
    if(urlnodes) free(urlnodes);
    return ;
}

/* get urlnodes with tnodeid */
int hibase_get_tnode_urlnodes(HIBASE *hibase, int nodeid, URLNODE **purlnodes)
{
    TNODE *tnodes = NULL;
    URLNODE *urlnodes = NULL, *p = NULL;
    int n = -1, x = 0;

    if(hibase && nodeid > 0 && nodeid < hibase->tnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnodes = HIO_MAP(hibase->tnodeio, TNODE)) && tnodes[nodeid].nurlnodes > 0
            && (x = tnodes[nodeid].urlnode_first) > 0
            && x < hibase->urlnodeio.total
            && (urlnodes = HIO_MAP(hibase->urlnodeio, URLNODE))
            && (p = *purlnodes = (URLNODE *)calloc(tnodes[nodeid].nurlnodes, sizeof(URLNODE))))
        {
            n = 0;
            while(x > 0 && n < tnodes[nodeid].nurlnodes)
            {
                memcpy(p, &(urlnodes[x]), sizeof(URLNODE));
                x = urlnodes[x].node_next;
                ++p;
                ++n;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* pop urlnodeid for download task */
int hibase_pop_urlnode(HIBASE *hibase, URLNODE *urlnode)
{
    int urlnodeid = -1, x = 0, *px = NULL;
    URLNODE *purlnode = NULL;

    if(hibase && hibase->qtask && hibase->istate && urlnode)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->urlnodeio.total > 0 && hibase->urlnodeio.current > 0 
            && (purlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            //fprintf(stdout, "%s::%d qtotal:%d urlnodeid:%d current:%d total:%d task_current:%d\n",
            //        __FILE__, __LINE__,  FQTOTAL(hibase->qtask), 
            //        hibase->istate->urlnode_task_current, 
            //        hibase->urlnodeio.current, hibase->urlnodeio.total,
            //         hibase->istate->urlnode_task_current);
            px = &x;
            if(FQTOTAL(hibase->qtask)>0 && FQUEUE_POP(hibase->qtask, int, px) == 0)
            {
                urlnodeid = x;
                pread(hibase->urlnodeio.fd, urlnode, sizeof(URLNODE), 
                        (off_t)sizeof(URLNODE) * (off_t)urlnodeid);
                //memcpy(urlnode, &(purlnode[urlnodeid]), sizeof(URLNODE));
            }
            /*
               while(FQTOTAL(hibase->qtask)>0||(hibase->urlnodeio.current < hibase->urlnodeio.total
               && hibase->istate->urlnode_task_current <= hibase->urlnodeio.current))
               {
               px = &x;
               if(FQUEUE_POP(hibase->qtask, int, px) == 0)
               {
               urlnodeid = x;
               }
               else if(hibase->istate->urlnodeio_current <= hibase->urlnodeio.current)
               {
               urlnodeid = hibase->istate->urlnode_task_current++;
               if(purlnode[urlnodeid].level > 0) continue;
               }
            //fprintf(stdout, "%s::%d::urlnodeid:%d current:%d\n", __FILE__, __LINE__, urlnodeid, hibase->urlnodeio.current);
            if(urlnodeid > 0 &&  purlnode[urlnodeid].status > 0)
            {
            memcpy(urlnode, &(purlnode[urlnodeid]), sizeof(URLNODE));
            break;
            }
            else urlnodeid = -1;
            //fprintf(stdout, "%s::%d::urlnodeid:%d\n", __FILE__, __LINE__, urlnodeid);
            }*/
            //fprintf(stdout, "%s::%d::urlnodeid:%d\n", __FILE__, __LINE__, urlnodeid);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}

/* push urlnodeid from wait queue */
int hibase_push_task_urlnodeid(HIBASE *hibase, int urlnodeid)
{
    int id = -1, *px = NULL;

    if(hibase && urlnodeid > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        px = &urlnodeid;
        if(hibase->qwait)
        {
            FQUEUE_PUSH(hibase->qwait, int, px);
            id = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* pop urlnodeid from wait queue */
int hibase_pop_task_urlnodeid(HIBASE *hibase)
{
    int urlnodeid = -1, x = 0, *px = NULL;

    if(hibase)
    {
        MUTEX_LOCK(hibase->mutex);
        px = &x;
        if(hibase->qwait && FQTOTAL(hibase->qwait) > 0
            && FQUEUE_POP(hibase->qwait, int, px) == 0)
        {
            urlnodeid = x;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}

/* clean */
void hibase_clean(HIBASE **phibase)
{
    if(phibase && *phibase)
    {
        TRIETAB_CLEAN((*phibase)->mdb);
        TRIETAB_CLEAN((*phibase)->mtnode);
        HIO_CLEAN((*phibase)->tableio);
        HIO_CLEAN((*phibase)->templateio);
        HIO_CLEAN((*phibase)->tnodeio);
        HIO_CLEAN((*phibase)->urlnodeio);
        FQUEUE_CLEAN((*phibase)->qtnode);
        FQUEUE_CLEAN((*phibase)->qtemplate);
        FQUEUE_CLEAN((*phibase)->qurlnode);
        FQUEUE_CLEAN((*phibase)->qtask);
        FQUEUE_CLEAN((*phibase)->qwait);
        if((*phibase)->istate) 
        {
            msync((*phibase)->istate, sizeof(ISTATE), MS_SYNC);
            munmap((*phibase)->istate, sizeof(ISTATE));
        }
        MUTEX_DESTROY((*phibase)->mutex);
        //fprintf(stdout, "%s::%d::OK\n", __FILE__, __LINE__);
        free(*phibase);
        *phibase = NULL;
    }
    return ;
}

/* initialize */
HIBASE * hibase_init()
{
    HIBASE *hibase = NULL;

    if((hibase = (HIBASE *)calloc(1, sizeof(HIBASE))))
    {
        TRIETAB_INIT(hibase->mdb);
        TRIETAB_INIT(hibase->mtnode);
        MUTEX_INIT(hibase->mutex);
        hibase->set_basedir         = hibase_set_basedir;
        hibase->db_uid_exists       = hibase_db_uid_exists;
        hibase->add_table           = hibase_add_table;
        hibase->get_table           = hibase_get_table;
        hibase->rename_table        = hibase_rename_table;
        hibase->delete_table        = hibase_delete_table;
        hibase->view_table          = hibase_view_table;
        hibase->list_table          = hibase_list_table;
        hibase->view_database       = hibase_view_database;
        hibase->add_field           = hibase_add_field;
        hibase->update_field        = hibase_update_field;
        hibase->delete_field        = hibase_delete_field;
        hibase->add_tnode           = hibase_add_tnode;
        hibase->get_tnode           = hibase_get_tnode;
        hibase->get_tnode_templates = hibase_get_tnode_templates;
        hibase->free_templates      = hibase_free_templates;
        hibase->get_tnode_childs    = hibase_get_tnode_childs;
        hibase->view_tnode_childs   = hibase_view_tnode_childs;
        hibase->update_tnode        = hibase_update_tnode;
        hibase->delete_tnode        = hibase_delete_tnode;
        hibase->add_template        = hibase_add_template;
        hibase->get_template        = hibase_get_template;
        hibase->update_template     = hibase_update_template;
        hibase->delete_template     = hibase_delete_template;
        hibase->view_templates      = hibase_view_templates;
        hibase->add_urlnode         = hibase_add_urlnode;
        hibase->update_urlnode      = hibase_update_urlnode;
        hibase->delete_urlnode      = hibase_delete_urlnode;
        hibase->get_urlnode         = hibase_get_urlnode;
        hibase->get_urlnode_childs  = hibase_get_urlnode_childs;
        hibase->get_tnode_urlnodes  = hibase_get_tnode_urlnodes;
        hibase->free_urlnodes       = hibase_free_urlnodes;
        hibase->pop_urlnode         = hibase_pop_urlnode;
        hibase->push_task_urlnodeid = hibase_push_task_urlnodeid;
        hibase->pop_task_urlnodeid  = hibase_pop_task_urlnodeid;
        hibase->clean               = hibase_clean;
    }
    return hibase;
}

#ifdef _DEBUG_HIBASE
//gcc -o hibase hibase.c utils/trie.c -Iutils -D_DEBUG_HIBASE 
int main(int argc, char **argv)
{
    HIBASE *hibase = NULL;
    ITABLE *tab = NULL, table = {0};
    ITEMPLATE *temp = NULL, template = {0};
    int i = 0, j = 0, rand = 0, x = 0, n = 0, 
        tabid = 0, fieldid = 0, type = 0, 
        flag = 0, table_num = 13;
    char name[TABLE_NAME_MAX], block[HI_BUF_SIZE];

    if((hibase = hibase_init()))
    {
        hibase->set_basedir(hibase, "/tmp/hibase");
#ifdef  _DEBUG_TABLE
        //add table
        for(i = 0; i < table_num; i++)
        {
            sprintf(name, "table_%d", i);
            if((tabid = hibase_add_table(hibase, name)) >= 0)
            {
                rand = random() % FIELD_NUM_MAX;
                for(j = 0; j < rand; j++)
                {
                    sprintf(name, "field_%d", j);
                    x = j % 4;
                    if(x == 0 )type = FTYPE_INT;
                    else if (x == 1) type = FTYPE_FLOAT;
                    else if (x == 2) type = FTYPE_TEXT;
                    else  type = FTYPE_BLOB;
                    flag = F_IS_NULL;
                    if(j % 2) flag |= F_IS_INDEX; 
                    if((fieldid = hibase_add_field(hibase, tabid, name, type, flag)) > 0)
                    {
                        if((j % 9))
                        {
                            if(j % 5)flag = 0;
                            sprintf(name, "field[%d][%d]", i, j);
                            fieldid = hibase_update_field(hibase, tabid, fieldid, name, type, flag);
                        }
                        if((j % 10))
                        {
                            fieldid = hibase_delete_field(hibase, tabid, fieldid);
                        }
                    }
                }
                if(i % 2)
                {
                    sprintf(name, "table[%d]", i);
                    hibase_rename_table(hibase, tabid, name);
                }
                memset(block, 0, HI_BUF_SIZE);
                hibase_view_table(hibase, tabid, block);
                fprintf(stdout, "%s\n", block); 
                if(i % 5)
                {
                    hibase_delete_table(hibase, tabid);
                }
            }
        }
        memset(block, 0, HI_BUF_SIZE);
        hibase_list_table(hibase, block);
        fprintf(stdout, "%s\n", block);
        //update
#endif
#ifdef _DEBUG_TNODE
        //add tnode 
        char name[1024];
        for(i = 1; i < 10000; i++)
        {
            sprintf(name, "node_%d", i);
            x = random()%i;
            if((n = hibase_add_tnode(hibase, x, name)) >= 0)
            {
                fprintf(stdout, "%d::add node:%d to node:%d\n", n, i, x);
            }
        }
        //hibase_list_tnode(hibase, 0, stdout);
        TNODE tnode = {0};
        x = random()%10000;
        fprintf(stdout, "ready updated node_%d\n", x);
        if((n = sprintf(name, "node_%d", x)) > 0 
                && (x = hibase_tnode_exists(hibase, name, n)) > 0 
                && (n = sprintf(name, "my_node_%d", x)) > 0
                && (n = hibase_update_tnode(hibase, x, name)) == x)
        {
            fprintf(stdout, "%d:: updated node:%d name:%s\n", __LINE__, n, name);
            if((x = hibase_get_tnode(hibase, n, &tnode)) >= 0)
            {
                fprintf(stdout, "%d::node:%d name:%s level:%d nchilds:%d\n",
                        __LINE__, tnode.id, tnode.name, tnode.level, tnode.nchilds);
            }
        }
        if(hibase_get_tnode(hibase, 215, &tnode) >= 0)
        {
            fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                    __LINE__, tnode.id, tnode.level, tnode.nchilds);
        }
        if((n = hibase_delete_tnode(hibase, 215)) > 0)
        {
            fprintf(stdout, "%d:: delete node:%d\n", __LINE__, n);
            if((x = hibase_get_tnode(hibase, n, &tnode)) >= 0)
            {
                fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                        __LINE__, tnode.id, tnode.level, tnode.nchilds);
            }
        }
        if((n = hibase_delete_tnode(hibase, 8925)) > 0)
        {
            fprintf(stdout, "%d:: delete node:%d\n", __LINE__, n);
            if((x = hibase_get_tnode(hibase, n, &tnode)) >= 0)
            {
                fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                        __LINE__, tnode.id, tnode.level, tnode.nchilds);
            }
        }
        TNODE tnodes[10000];
        memset(tnodes, 0, 10000 * sizeof(TNODE));
        if((n = hibase_get_tnode_childs(hibase, 0, tnodes)) > 0)
        {
            for(i = 0; i < n; i++)
            {
                fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n", 
                        i, tnodes[i].id, tnodes[i].level, tnodes[i].nchilds);
            }
        }
        fprintf(stdout, "%d::hibase->tnode_childs_max:%d\n", __LINE__, hibase->tnode_childs_max);
        hibase_list_tnode(hibase, 0, stdout);
#endif
        hibase->clean(&hibase);
    }
    return 0;
}
//gcc -o hibase hibase.c utils/*.c -I utils -D_DEBUG_HIBASE -D_DEBUG_TNODE -lz 
#endif
