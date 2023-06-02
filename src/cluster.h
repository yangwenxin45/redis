#ifndef __REDIS_CLUSTER_H
#define __REDIS_CLUSTER_H

/*-----------------------------------------------------------------------------
 * Redis cluster data structures, defines, exported API.
 *----------------------------------------------------------------------------*/

#define REDIS_CLUSTER_SLOTS 16384
#define REDIS_CLUSTER_OK 0          /* Everything looks ok */
#define REDIS_CLUSTER_FAIL 1        /* The cluster can't work */
#define REDIS_CLUSTER_NAMELEN 40    /* sha1 hex length */
#define REDIS_CLUSTER_PORT_INCR 10000 /* Cluster port = baseport + PORT_INCR */

/* The following defines are amount of time, sometimes expressed as
 * multiplicators of the node timeout value (when ending with MULT). */
#define REDIS_CLUSTER_DEFAULT_NODE_TIMEOUT 15000
#define REDIS_CLUSTER_DEFAULT_SLAVE_VALIDITY 10 /* Slave max data age factor. */
#define REDIS_CLUSTER_DEFAULT_REQUIRE_FULL_COVERAGE 1
#define REDIS_CLUSTER_FAIL_REPORT_VALIDITY_MULT 2 /* Fail report validity. */
#define REDIS_CLUSTER_FAIL_UNDO_TIME_MULT 2 /* Undo fail if master is back. */
#define REDIS_CLUSTER_FAIL_UNDO_TIME_ADD 10 /* Some additional time. */
#define REDIS_CLUSTER_FAILOVER_DELAY 5 /* Seconds */
#define REDIS_CLUSTER_DEFAULT_MIGRATION_BARRIER 1
#define REDIS_CLUSTER_MF_TIMEOUT 5000 /* Milliseconds to do a manual failover. */
#define REDIS_CLUSTER_MF_PAUSE_MULT 2 /* Master pause manual failover mult. */
#define REDIS_CLUSTER_SLAVE_MIGRATION_DELAY 5000 /* Delay for slave migration */

/* Redirection errors returned by getNodeByQuery(). */
#define REDIS_CLUSTER_REDIR_NONE 0          /* Node can serve the request. */
#define REDIS_CLUSTER_REDIR_CROSS_SLOT 1    /* -CROSSSLOT request. */
#define REDIS_CLUSTER_REDIR_UNSTABLE 2      /* -TRYAGAIN redirection required */
#define REDIS_CLUSTER_REDIR_ASK 3           /* -ASK redirection required. */
#define REDIS_CLUSTER_REDIR_MOVED 4         /* -MOVED redirection required. */
#define REDIS_CLUSTER_REDIR_DOWN_STATE 5    /* -CLUSTERDOWN, global state. */
#define REDIS_CLUSTER_REDIR_DOWN_UNBOUND 6  /* -CLUSTERDOWN, unbound slot. */

struct clusterNode;

/* clusterLink encapsulates everything needed to talk with a remote node. */
// 连接节点所需的有关信息
typedef struct clusterLink {
    // 连接的创建时间
    mstime_t ctime;             /* Link creation time */
    // TCP套接字描述符
    int fd;                     /* TCP socket file descriptor */
    // 输出缓冲区，保存着等待发送给其他节点的信息
    sds sndbuf;                 /* Packet send buffer */
    // 输入缓冲区，保存着从其他节点接收到的消息
    sds rcvbuf;                 /* Packet reception buffer */
    // 与这个连接相关联的节点，如果没有的话就为NULL
    struct clusterNode *node;   /* Node related to this link if any, or NULL */
} clusterLink;

/* Cluster node flags and macros. */
#define REDIS_NODE_MASTER 1     /* The node is a master */
#define REDIS_NODE_SLAVE 2      /* The node is a slave */
#define REDIS_NODE_PFAIL 4      /* Failure? Need acknowledge */
#define REDIS_NODE_FAIL 8       /* The node is believed to be malfunctioning */
#define REDIS_NODE_MYSELF 16    /* This node is myself */
#define REDIS_NODE_HANDSHAKE 32 /* We have still to exchange the first ping */
#define REDIS_NODE_NOADDR   64  /* We don't know the address of this node */
#define REDIS_NODE_MEET 128     /* Send a MEET message to this node */
#define REDIS_NODE_MIGRATE_TO 256 /* Master elegible for replica migration. */
#define REDIS_NODE_NULL_NAME "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"

#define nodeIsMaster(n) ((n)->flags & REDIS_NODE_MASTER)
#define nodeIsSlave(n) ((n)->flags & REDIS_NODE_SLAVE)
#define nodeInHandshake(n) ((n)->flags & REDIS_NODE_HANDSHAKE)
#define nodeHasAddr(n) (!((n)->flags & REDIS_NODE_NOADDR))
#define nodeWithoutAddr(n) ((n)->flags & REDIS_NODE_NOADDR)
#define nodeTimedOut(n) ((n)->flags & REDIS_NODE_PFAIL)
#define nodeFailed(n) ((n)->flags & REDIS_NODE_FAIL)

/* Reasons why a slave is not able to failover. */
#define REDIS_CLUSTER_CANT_FAILOVER_NONE 0
#define REDIS_CLUSTER_CANT_FAILOVER_DATA_AGE 1
#define REDIS_CLUSTER_CANT_FAILOVER_WAITING_DELAY 2
#define REDIS_CLUSTER_CANT_FAILOVER_EXPIRED 3
#define REDIS_CLUSTER_CANT_FAILOVER_WAITING_VOTES 4
#define REDIS_CLUSTER_CANT_FAILOVER_RELOG_PERIOD (60*5) /* seconds. */

/* This structure represent elements of node->fail_reports. */
typedef struct clusterNodeFailReport {
    // 报告目标节点已经下线的节点
    struct clusterNode *node;  /* Node reporting the failure condition. */
    // 最后一次从node节点收到下线报告的时间
    // 程序使用这个时间戳来检查下线报告是否过期（与当前时间相差太久的下线报告会被删除）
    mstime_t time;             /* Time of the last report from this node. */
} clusterNodeFailReport;

// 节点的当前状态
typedef struct clusterNode {
    // 创建节点的时间
    mstime_t ctime; /* Node object creation time. */
    // 节点的名字，由40个十六进制字符组成
    char name[REDIS_CLUSTER_NAMELEN]; /* Node name, hex string, sha1-size */
    // 节点标识
    // 使用各种不同的标识值记录节点的角色（比如主节点或者从节点），以及节点目前所处的状态（比如在线或者下线）
    int flags;      /* REDIS_NODE_... */
    // 节点当前的配置纪元，用于实现故障转移
    uint64_t configEpoch; /* Last configEpoch observed for this node */
    // 记录节点负责处理哪些槽
    unsigned char slots[REDIS_CLUSTER_SLOTS / 8]; /* slots handled by this node */
    // 记录节点负责处理的槽的数量
    int numslots;   /* Number of slots handled by this node */
    // 正在复制这个主节点的从节点数量
    int numslaves;  /* Number of slave nodes, if this is a master */
    // 一个数组，每个数组项指向一个正在复制这个主节点的从节点的clusterNode结构
    struct clusterNode **slaves; /* pointers to slave nodes */
    // 记录这个节点正在复制的主节点
    struct clusterNode *slaveof; /* pointer to the master node. Note that it
                                    may be NULL even if the node is a slave
                                    if we don't have the master node in our
                                    tables. */
    mstime_t ping_sent;      /* Unix time we sent latest ping */
    mstime_t pong_received;  /* Unix time we received the pong */
    mstime_t fail_time;      /* Unix time when FAIL flag was set */
    mstime_t voted_time;     /* Last time we voted for a slave of this master */
    mstime_t repl_offset_time;  /* Unix time we received offset for this node */
    mstime_t orphaned_time;     /* Starting time of orphaned master condition */
    long long repl_offset;      /* Last known repl offset for this node. */
    // 节点的IP地址
    char ip[REDIS_IP_STR_LEN];  /* Latest known IP address of this node */
    // 节点的端口号
    int port;                   /* Latest known port of this node */
    // 保存连接节点所需的有关信息
    clusterLink *link;          /* TCP/IP link with this node */
    // 一个链表，记录了所有其他节点对该节点的下线报告
    list *fail_reports;         /* List of nodes signaling this as failing */
} clusterNode;

// 在当前节点的视角下，集群目前所处的状态
typedef struct clusterState {
    // 指向当前节点的指针
    clusterNode *myself;  /* This node */
    // 集群当前的配置纪元，用于实现故障转移
    uint64_t currentEpoch;
    // 集群当前的状态：是在线还是下线
    int state;            /* REDIS_CLUSTER_OK, REDIS_CLUSTER_FAIL, ... */
    // 集群中至少处理着一个槽的节点的数量
    int size;             /* Num of master nodes with at least one slot */
    // 集群节点名单（包括myself节点）
    // 字典的键为节点的名字，字典的值为节点对应的clusterNode结构
    dict *nodes;          /* Hash table of name -> clusterNode structures */
    dict *nodes_black_list; /* Nodes we don't re-add for a few seconds. */
    // 记录了当前节点正在迁移至其他节点的槽
    clusterNode *migrating_slots_to[REDIS_CLUSTER_SLOTS];
    // 记录了当前节点正在从其他节点导入的槽
    clusterNode *importing_slots_from[REDIS_CLUSTER_SLOTS];
    // 记录了集群中所有16384个槽的指派信息
    clusterNode *slots[REDIS_CLUSTER_SLOTS];
    // 保存键与槽之间的关系
    // 跳跃表每个节点的分值都是一个槽号，而每个节点的成员都是一个数据库键
    zskiplist *slots_to_keys;
    /* The following fields are used to take the slave state on elections. */
    mstime_t failover_auth_time; /* Time of previous or next election. */
    int failover_auth_count;    /* Number of votes received so far. */
    int failover_auth_sent;     /* True if we already asked for votes. */
    int failover_auth_rank;     /* This slave rank for current auth request. */
    uint64_t failover_auth_epoch; /* Epoch of the current election. */
    int cant_failover_reason;   /* Why a slave is currently not able to
                                   failover. See the CANT_FAILOVER_* macros. */
    /* Manual failover state in common. */
    mstime_t mf_end;            /* Manual failover time limit (ms unixtime).
                                   It is zero if there is no MF in progress. */
    /* Manual failover state of master. */
    clusterNode *mf_slave;      /* Slave performing the manual failover. */
    /* Manual failover state of slave. */
    long long mf_master_offset; /* Master offset the slave needs to start MF
                                   or zero if stil not received. */
    int mf_can_start;           /* If non-zero signal that the manual failover
                                   can start requesting masters vote. */
    /* The followign fields are used by masters to take state on elections. */
    uint64_t lastVoteEpoch;     /* Epoch of the last vote granted. */
    int todo_before_sleep; /* Things to do in clusterBeforeSleep(). */
    long long stats_bus_messages_sent;  /* Num of msg sent via cluster bus. */
    long long stats_bus_messages_received; /* Num of msg rcvd via cluster bus.*/
} clusterState;

/* clusterState todo_before_sleep flags. */
#define CLUSTER_TODO_HANDLE_FAILOVER (1<<0)
#define CLUSTER_TODO_UPDATE_STATE (1<<1)
#define CLUSTER_TODO_SAVE_CONFIG (1<<2)
#define CLUSTER_TODO_FSYNC_CONFIG (1<<3)

/* Redis cluster messages header */

/* Note that the PING, PONG and MEET messages are actually the same exact
 * kind of packet. PONG is the reply to ping, in the exact format as a PING,
 * while MEET is a special PING that forces the receiver to add the sender
 * as a node (if it is not already in the list). */
#define CLUSTERMSG_TYPE_PING 0          /* Ping */
#define CLUSTERMSG_TYPE_PONG 1          /* Pong (reply to Ping) */
#define CLUSTERMSG_TYPE_MEET 2          /* Meet "let's join" message */
#define CLUSTERMSG_TYPE_FAIL 3          /* Mark node xxx as failing */
#define CLUSTERMSG_TYPE_PUBLISH 4       /* Pub/Sub Publish propagation */
#define CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST 5 /* May I failover? */
#define CLUSTERMSG_TYPE_FAILOVER_AUTH_ACK 6     /* Yes, you have my vote */
#define CLUSTERMSG_TYPE_UPDATE 7        /* Another node slots configuration */
#define CLUSTERMSG_TYPE_MFSTART 8       /* Pause clients for manual failover */

/* Initially we don't know our "name", but we'll find it once we connect
 * to the first node, using the getsockname() function. Then we'll use this
 * address for all the next messages. */
// 被选中节点信息
typedef struct {
    // 节点的名字
    char nodename[REDIS_CLUSTER_NAMELEN];
    // 最后一次向该节点发送PING消息的时间戳
    uint32_t ping_sent;
    // 最后一次从该节点接收到PONG消息的时间戳
    uint32_t pong_received;
    // 节点的IP地址
    char ip[REDIS_IP_STR_LEN];  /* IP address last time it was seen */
    // 节点的端口号
    uint16_t port;              /* port last time it was seen */
    // 节点的标识值
    uint16_t flags;             /* node->flags copy */
    uint16_t notused1;          /* Some room for future improvements. */
    uint32_t notused2;
} clusterMsgDataGossip;

typedef struct {
    // 已下线节点的名字
    char nodename[REDIS_CLUSTER_NAMELEN];
} clusterMsgDataFail;

typedef struct {
    // channel参数的长度
    uint32_t channel_len;
    // message参数的长度
    uint32_t message_len;
    /* We can't reclare bulk_data as bulk_data[] since this structure is
     * nested. The 8 bytes are removed from the count during the message
     * length computation. */
    // 保存了客户端通过PUBLISH命令发送给节点的channel参数和message参数
    // 定义为8字节只是为了对齐其他消息结构，实际的长度由保存的内容决定
    unsigned char bulk_data[8];
} clusterMsgDataPublish;

typedef struct {
    uint64_t configEpoch; /* Config epoch of the specified instance. */
    char nodename[REDIS_CLUSTER_NAMELEN]; /* Name of the slots owner. */
    unsigned char slots[REDIS_CLUSTER_SLOTS/8]; /* Slots bitmap. */
} clusterMsgDataUpdate;

// 消息的正文
union clusterMsgData {
    /* PING, MEET and PONG */
    // MEET、PING、PONG消息的正文
    struct {
        /* Array of N clusterMsgDataGossip structures */
        // 每条MEET、PING、PONG消息都包含两个clusterMsgDataGossip结构
        clusterMsgDataGossip gossip[1];
    } ping;

    /* FAIL */
    // FAIL消息的正文
    struct {
        clusterMsgDataFail about;
    } fail;

    /* PUBLISH */
    // PUBLISH消息的正文
    struct {
        clusterMsgDataPublish msg;
    } publish;

    /* UPDATE */
    // 其他消息的正文
    struct {
        clusterMsgDataUpdate nodecfg;
    } update;
};

#define CLUSTER_PROTO_VER 0 /* Cluster bus protocol version. */

// 消息头
typedef struct {
    // 消息的长度（包括这个消息头的长度和消息正文的长度）
    char sig[4];        /* Siganture "RCmb" (Redis Cluster message bus). */
    // 消息的类型
    uint32_t totlen;    /* Total length of this message */
    uint16_t ver;       /* Protocol version, currently set to 0. */
    uint16_t notused0;  /* 2 bytes not used. */
    uint16_t type;      /* Message type */
    // 消息正文包含的节点信息数量
    // 只在发送MEET、PING、PONG这三种Gossip协议消息时使用
    uint16_t count;     /* Only used for some kind of messages. */
    // 发送者所处的配置纪元
    uint64_t currentEpoch;  /* The epoch accordingly to the sending node. */
    // 如果发送者是一个主节点，那么这里记录的是发送者的配置纪元
    // 如果发送者是一个从节点，那么这里记录的是发送者正在复制的主节点的配置纪元
    uint64_t configEpoch;   /* The config epoch if it's a master, or the last
                               epoch advertised by its master if it is a
                               slave. */
    uint64_t offset;    /* Master replication offset if node is a master or
                           processed replication offset if node is a slave. */
    // 发送者的名字（ID)
    char sender[REDIS_CLUSTER_NAMELEN]; /* Name of the sender node */
    // 发送者目前的槽指派信息
    unsigned char myslots[REDIS_CLUSTER_SLOTS / 8];
    // 如果发送者是一个从节点，那么这里记录的是发送者正在复制的主节点的名字
    // 如果发送者是一个主节点，那么这里记录的是REDIS_MODE_NULL_NAME
    char slaveof[REDIS_CLUSTER_NAMELEN];
    char notused1[32];  /* 32 bytes reserved for future usage. */
    // 发送者的端口号
    uint16_t port;      /* Sender TCP base port */
    // 发送者的标识值
    uint16_t flags;     /* Sender node flags */
    // 发送者所处集群的状态
    unsigned char state; /* Cluster state from the POV of the sender */
    unsigned char mflags[3]; /* Message flags: CLUSTERMSG_FLAG[012]_... */
    // 消息的正文（或者说内容）
    union clusterMsgData data;
} clusterMsg;

#define CLUSTERMSG_MIN_LEN (sizeof(clusterMsg)-sizeof(union clusterMsgData))

/* Message flags better specify the packet content or are used to
 * provide some information about the node state. */
#define CLUSTERMSG_FLAG0_PAUSED (1<<0) /* Master paused for manual failover. */
#define CLUSTERMSG_FLAG0_FORCEACK (1<<1) /* Give ACK to AUTH_REQUEST even if
                                            master is up. */

/* ---------------------- API exported outside cluster.c -------------------- */
clusterNode *getNodeByQuery(redisClient *c, struct redisCommand *cmd, robj **argv, int argc, int *hashslot, int *ask);
int clusterRedirectBlockedClientIfNeeded(redisClient *c);
void clusterRedirectClient(redisClient *c, clusterNode *n, int hashslot, int error_code);

#endif /* __REDIS_CLUSTER_H */
