/*
 * dns.h: header file for the dns module
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: dns.h 611 2005-11-22 10:32:23Z wd $
 */

#ifndef DNS_DNS_H
#define DNS_DNS_H

/*
 * This file defines the various structures useful for communicating with a DNS
 * server.  Most of these are not alltogether useful from an outside
 * perspective, but are provided here anyways to keep things in order.  I
 * gleaned most of this from a cross of the bind headers and reading RFC1035,
 * with a little bit of ingenuity on my own part.
 */

/* First off, some generic definitions and enumerations which should be useful
 * for us.  This includes the rr type and rr class variants, and some size
 * limitations. */

#define DNS_MAX_PACKET_SIZE 512     /* maximum size of a dns packet */
#define DNS_MAX_NAMELEN     1024    /* maximum length of an FQDN */
#define DNS_MAX_SEGLEN      63      /* maximum length of an FQDN segment. */
#define DNS_HEADER_LEN      12      /* size of a query/answer header. */
#define DNS_DEFAULT_PORT    53      /* default DNS server port */

/* various query classes available.  I've never seen a use for anything but the
 * 'IN' class, but we include these for completeness. */
typedef enum {
    DNS_C_IN = 1,                   /* INternet class */
    DNS_C_CHAOS = 3,                /* MIT CHAOS net */
    DNS_C_HESIOD = 4,               /* MIT Hesiod (..zuh?) */
    DNS_C_NONE = 254,               /* classless requests */
    DNS_C_ANY = 255                 /* wildcard class */
} dns_class_t;
extern const char *const dns_class_strlist[];

#define dns_class_conv_str(x) dns_class_strlist[(x)]
dns_class_t dns_str_conv_class(const char *);

/* and the various query types. */
typedef enum {
    DNS_T_A = 1,                    /* Address */
    DNS_T_NS = 2,                   /* Name Server */
    DNS_T_MD = 3,                   /* Mail Destination */
    DNS_T_MF = 4,                   /* Mail Forwarder */
    DNS_T_CNAME = 5,                /* Canonical name (alias) */
    DNS_T_SOA = 6,                  /* Start of authority */
    DNS_T_MB = 7,                   /* MailBox name */
    DNS_T_MG = 8,                   /* Mail Group */
    DNS_T_MR = 9,                   /* Mail Rename */
    DNS_T_NULL = 10,                /* null record (?) */
    DNS_T_WKS = 11,                 /* Well Known Service */
    DNS_T_PTR = 12,                 /* IPv4 address to name pointer */
    DNS_T_HINFO = 13,               /* Host Info */
    DNS_T_MINFO = 14,               /* Mailbox Info */
    DNS_T_MX = 15,                  /* Mail eXchange */
    DNS_T_TXT = 16,                 /* text storage */
    DNS_T_RP = 17,                  /* Responsible Person */
    DNS_T_AFSDB = 18,               /* AFS Database (?) */
    DNS_T_X25 = 19,                 /* X.25 address */
    DNS_T_ISDN = 20,                /* ISDN address */
    DNS_T_RT = 21,                  /* Router */
    DNS_T_NSAP = 22,                /* NSAP address */
    /* DNS_T_NSAP_PTR (deprecated) */
    DNS_T_SIG = 24,                 /* Security SIGnature */
    DNS_T_KEY = 25,                 /* Security key */
    DNS_T_PX = 26,                  /* X.400 mail mapping */
    /* DNS_T_GPOS (withdrawn) */
    DNS_T_AAAA = 28,                /* IPv6 address */
    DNS_T_LOC = 29,                 /* LOCation information */
    DNS_T_NXT = 30,                 /* NeXT domain (security) (?) */
    DNS_T_EID = 31,                 /* Endpoint IDentifier */
    DNS_T_NIMLOC = 32,              /* NIMrod LOCator (..zuh?) */
    DNS_T_SRV = 33,                 /* Server selection */
    DNS_T_ATMA = 34,                /* ATM Address */
    DNS_T_NAPTR = 35,               /* Naming Authority Pointer */
    DNS_T_ANY = 255,                /* Wildcard match. */
} dns_type_t;
extern const char *const dns_type_strlist[];
#define dns_type_conv_str(x) dns_type_strlist[(x)]
dns_type_t dns_str_conv_type(const char *);

/* response codes we might receive from the server */
typedef enum {
    DNS_R_OK = 0,                   /* no error */
    DNS_R_BADFORMAT = 1,            /* badly formatted packet */
    DNS_R_SERVFAIL = 2,             /* server failure */
    DNS_R_NXDOMAIN = 3,             /* nonexistant domain */
    DNS_R_NOTIMP = 4,               /* function not implemented */
    DNS_R_REFUSED = 5               /* operation refused */
} dns_rescode_t;

/*
 * Next we define the structures for dns packet headers and dns RRs (resource
 * records).  External users will probably not interact with dns packet
 * headers, and the structure may be moved.
 */
#if SIZEOF_INT == 4
# define dns_int unsigned int
#else
# define dns_int uint32_t
#endif

struct dns_packet_header {
    dns_int id:16;                  /* question ID */

    /* Below here are the flags, which come in a different order depending on
     * endian-ness. */
#if BYTE_ORDER == BIG_ENDIAN
    dns_int qr:1;                   /* response flag */
    dns_int opcode:4;               /* operation code */
    dns_int aa:1;                   /* authoritative answer flag */
    dns_int tc:1;                   /* truncated message flag */
    dns_int rd:1;                   /* recursion desired */
    dns_int ra:1;                   /* recursion available */
    dns_int unused:3;               /* unused (by us) flags */
    dns_int rcode:4;                /* response code */
#else
    dns_int rd:1;
    dns_int tc:1;
    dns_int aa:1;
    dns_int opcode:4;
    dns_int qr:1;
    dns_int rcode:4;
    dns_int unused:3;
    dns_int ra:1;
#endif

    dns_int qdcount:16;             /* question count */
    dns_int ancount:16;             /* answer count */
    dns_int nscount:16;             /* authority count */
    dns_int adcount:16;             /* additional count */
};

#undef dns_int

/*
 * Here we define the query and RR (resource record) types.  Appropriate
 * functions are provided to extract these from a raw dns packet.
 */
struct dns_query {
    unsigned char name[DNS_MAX_NAMELEN + 1];/* name being queried */
    uint16_t type;                  /* type desired */
    uint16_t class;                 /* and class desired */
};

struct dns_rr {
    unsigned char name[DNS_MAX_NAMELEN + 1];/* name (if expanded) */
    dns_type_t type;                /* type of RR */
    dns_class_t class;              /* and class */
    uint32_t ttl;                   /* time to live */
    uint16_t rdlen;                 /* length of response data */
    union {
        unsigned char *txt;         /* textual data */
        struct dns_rr_hinfo *hinfo; /* Host Info answer */
        struct dns_rr_minfo *minfo; /* Mail Info answer */
        struct dns_rr_mx *mx;       /* MX answer */
        struct dns_rr_soa *soa;     /* SOA answer */
        struct dns_rr_wks *wks;     /* Well Known Service answer */
    } rdata;

    LIST_ENTRY(dns_rr) lp;
};

/* Below here are the special structures defined to handle certain RR types
 * that cannot be easily expressed as textual data. */
struct dns_rr_hinfo {
    unsigned char cpu[DNS_MAX_NAMELEN + 1];/* The CPU type */
    unsigned char os[DNS_MAX_NAMELEN + 1];/* The OS type */
};
struct dns_rr_minfo {
    unsigned char rmailbx[DNS_MAX_NAMELEN + 1];/* 'Responsible' mailbox */
    unsigned char emailbx[DNS_MAX_NAMELEN + 1];/* 'Error' mailbox */
};
struct dns_rr_mx {
    uint16_t preference;            /* Preference rating */
    unsigned char exchange[DNS_MAX_NAMELEN + 1];/* Mail host */
};
struct dns_rr_soa {
    unsigned char mname[DNS_MAX_NAMELEN + 1];/* name of the master server */
    unsigned char rname[DNS_MAX_NAMELEN + 1];/* name of the responsible party */
    uint32_t serial;
    uint32_t refresh;
    uint32_t retry;
    uint32_t expire;
    uint32_t minimum;
};
struct dns_rr_wks {
    unsigned char address[IPADDR_MAXLEN + 1];/* IP address */
    unsigned char protocol;                    /* IP protocol number */
    unsigned char map[8192];                    /* bit map of available ports */
};

/*
 * Here is the module's global data.  This is declared in dns.c and used to
 * hold a variety of settings.
 */
struct dns_lookup;                    /* forward declarations */
TAILQ_HEAD(dns_lookup_tailq, dns_lookup);
extern struct dns_data_struct {
    conf_list_t **confdata;         /* configuration data */
    isocket_t *sock;                /* server socket */
    /* this sub structure holds pending lookups. */
    struct {
        time_t timeout;             /* timeout for lookups */
        unsigned int retries;       /* number of retries */
        time_t *retry_times;        /* table of retry times */
        uint16_t idn;               /* rolling lookup id counter */
        int max;                    /* maximum number of CONCURRENT pending
                                       lookups */
        int acount;                 /* count of active lookups */
        struct dns_lookup_tailq alist; /* list of active lookups */
        struct dns_lookup_tailq wlist; /* list of waiting lookups */
    } pending;
    /* and this is the sub structure for cached lookups */
    struct {
        int failure;                /* cache failures? */
        time_t expire;              /* maximum length of cache-time */
        int max;                    /* maximum number of cached entries */
        int count;                  /* current number of cached entries */
        struct dns_lookup_tailq list; /* list of lookups */
    } cache;
} dns;

/* the borrowed dn_* functions from bind */
int dn_expand(unsigned char *msg, unsigned char *eomorig,
        unsigned char *comp_dn, unsigned char *exp_dn, int length);
int dn_comp(unsigned char *exp_dn, unsigned char *comp_dn, int length,
        unsigned char **dnptrs, unsigned char **lastdnptr);
int dn_skipname(unsigned char *comp_dn, unsigned char *eom);

/* and the external functions from packet.c */
void dns_packet_parse(unsigned char *, size_t);
int dns_lookup_send(void);
HOOK_FUNCTION(dns_timer_hook);

/* set the logging name.. */
#undef LOG_MODULENAME
#define LOG_MODULENAME "dns"

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
