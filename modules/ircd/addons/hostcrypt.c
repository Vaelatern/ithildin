/*
 * hostcrypt.c: usermode based hostname encryption
 * 
 * Copyright 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module adds a usermode (typically 'x') for users to set which will
 * encrypt or decrypt their hostname.  The following encryption schemes are
 * supported:
 * - austhex: austhex-compatible encryption
 * - md5: md5 IP encryption.  This won't work right if your network doesn't
 *   pass IP address data!
 *
 * There are some 'issues' with this implementation:
 * - Each server has to crypt the hostname (yuck)
 * - Therefore each server, in order to decrypt, has to store the 'alternate'
 *   hostname form so it can switch back.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: hostcrypt.c 823 2008-11-07 04:27:12Z wd $");

MODULE_REGISTER("$Rev: 823 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/protocols/rfc1459
*/

typedef char *(*hostcrypt_func_t)(client_t *);
static struct {
    struct mdext_item *mdext;
    hostcrypt_func_t crypter;
    unsigned char mode;
    int            see_priv;
    char    *prefix;
    char    *suffix;

    char    *operhost; /* hostname to give to operators.  supercedes encryption
                          of the host. */

    /* XXX: this (more than the entire module) is a hack.  a really awful hack.
     * I was coerced into doing this.  I don't condone abuses of this sort, but
     * hey. */
    protocol_t *proto;
    protocol_t *rfc1459_proto;
} hostcrypt;
    
HOOK_FUNCTION(hostcrypt_conf_hook);
HOOK_FUNCTION(hostcrypt_rc_hook);
HOOK_FUNCTION(hostcrypt_oper_hook);
void hostcrypt_check_proto(client_t *);
struct send_msg *hostcrypt_output_hack(struct protocol_sender *, char *,
        char *, char *, va_list);
USERMODE_FUNC(hostcrypt_usermode_handler);
static void hostcrypt_encrypt(client_t *);
static void hostcrypt_decrypt(client_t *);

static char *hostcrypt_austhex(client_t *);
static char *hostcrypt_md5(client_t *);

MODULE_LOADER(hostcrypt) {
    int64_t i64 = 0;
    client_t *cli;
    bool recrypt = false;

    if (!get_module_savedata(savelist, "hostcrypt", &hostcrypt)) {
        hostcrypt.mdext = create_mdext_item(ircd.mdext.client, HOSTLEN + 1);
        EXPORT_SYM(hostcrypt_usermode_handler);
        usermode_request('x', &hostcrypt.mode, USERMODE_FL_GLOBAL, -1,
                "hostcrypt_usermode_handler");
        hostcrypt.see_priv = create_privilege("see-decrypted-host",
                PRIVILEGE_FL_BOOL, &i64, NULL);
    } else
        recrypt = true;
    hostcrypt.crypter = NULL;

    hostcrypt_conf_hook(NULL, NULL);

    add_hook_before(ircd.events.register_client, hostcrypt_rc_hook, NULL);
    add_hook(me.events.read_conf, hostcrypt_conf_hook);
    add_hook(ircd.events.client_oper, hostcrypt_oper_hook);
    add_hook(ircd.events.client_deoper, hostcrypt_oper_hook);

    /* now (re)crypt as necessary. */
    LIST_FOREACH(cli, ircd.lists.clients, lp) {
        if (!CLIENT_REGISTERED(cli))
            continue;

        cli->orighost = mdext(cli, hostcrypt.mdext);
        if (usermode_isset(cli, hostcrypt.mode))
            /* re-encrypt the hostname .. */
            hostcrypt_encrypt(cli);
        else
            strcpy(cli->orighost, cli->host);

        hostcrypt_check_proto(cli);
    }

    return 1;
}

MODULE_UNLOADER(hostcrypt) {
    client_t *cli;

    if (reload)
        add_module_savedata(savelist, "hostcrypt", sizeof(hostcrypt),
                &hostcrypt);
    else {
        /* decrypt all hostnames if we're not reloading */
        LIST_FOREACH(cli, ircd.lists.clients, lp) {
            if (usermode_isset(cli, hostcrypt.mode))
                hostcrypt_decrypt(cli);
            cli->orighost = cli->host;
        }

        destroy_privilege(hostcrypt.see_priv);
        usermode_release(hostcrypt.mode);
        destroy_mdext_item(ircd.mdext.client, hostcrypt.mdext);
    }

    remove_hook(ircd.events.register_client, hostcrypt_rc_hook);
    remove_hook(me.events.read_conf, hostcrypt_conf_hook);
    remove_hook(ircd.events.client_oper, hostcrypt_oper_hook);
    remove_hook(ircd.events.client_deoper, hostcrypt_oper_hook);
}

HOOK_FUNCTION(hostcrypt_conf_hook) {
    char *s;

    /* Now configure ourselves. */
    if ((s = conf_find_entry("hostcrypt", *ircd.confhead, 1)) == NULL)
        s = "md5";

    if (!strcasecmp(s, "austhex"))
        hostcrypt.crypter = hostcrypt_austhex;
    else {
        if (strcasecmp(s, "md5") != 0)
            log_warn("hostname encryption method %s is not valid.  using "
                    "default instead.", s);
        hostcrypt.crypter = hostcrypt_md5;
    }
    if ((hostcrypt.prefix = conf_find_entry("hostcrypt-prefix",
                    *ircd.confhead, 1)) == NULL)
        hostcrypt.prefix = "";
    if ((hostcrypt.suffix = conf_find_entry("hostcrypt-suffix",
                    *ircd.confhead, 1)) == NULL)
        hostcrypt.suffix = "";
    hostcrypt.operhost = conf_find_entry("hostcrypt-operhost",
            *ircd.confhead, 1);

    log_debug("using hostcrypt method %s.", s);

    /* ...ugh */
    if (hostcrypt.proto == NULL)
        hostcrypt.proto = malloc(sizeof(protocol_t));
    hostcrypt.rfc1459_proto = find_protocol("rfc1459");
    memcpy(hostcrypt.proto, hostcrypt.rfc1459_proto, sizeof(protocol_t));
    hostcrypt.proto->output = hostcrypt_output_hack;
    hostcrypt.proto->flags |= PROTOCOL_MFL_NOCACHE; /* efficiency?  pfft. */

    return NULL;
}

HOOK_FUNCTION(hostcrypt_rc_hook) {
    client_t *cli = (client_t *)data;

    /* save their original hostname */
    cli->orighost = mdext(cli, hostcrypt.mdext);
    strcpy(cli->orighost, cli->host);

    /* if the client is already +x (shouldn't happen, but could..) do the
     * hostname encrypting. */
    if (usermode_isset(cli, hostcrypt.mode))
        hostcrypt_encrypt(cli);

    hostcrypt_check_proto(cli);

    return NULL;
}

HOOK_FUNCTION(hostcrypt_oper_hook) {
    client_t *cli = (client_t *)data;

    hostcrypt_check_proto(cli);

    if (usermode_isset(cli, hostcrypt.mode) && hostcrypt.operhost != NULL) {
        if (ep == ircd.events.client_oper)
            strlcpy(cli->host, hostcrypt.operhost, HOSTLEN + 1);
        else
            strlcpy(cli->host, hostcrypt.crypter(cli), HOSTLEN + 1);
    }

    return NULL;
}

void hostcrypt_check_proto(client_t *cli) {

    if (cli->conn == NULL)
        return;

    if (cli->conn->proto == hostcrypt.proto &&
            !BPRIV(cli, ircd.privileges.priv_srch))
        cli->conn->proto = hostcrypt.rfc1459_proto;
    else if (cli->conn->proto == hostcrypt.rfc1459_proto &&
            BPRIV(cli, ircd.privileges.priv_srch))
        cli->conn->proto = hostcrypt.proto;
}

#define RFC1459_PKT_LEN 512
struct send_msg *hostcrypt_output_hack(struct protocol_sender *from, char *cmd,
        char *to, char *msg, va_list args) {
    static char buf[RFC1459_PKT_LEN];
    static struct send_msg sm = {buf, 0};

    if (to != NULL && *to == '\0') /* handle numerics for unreged clients */
        to = "*";

    if (from->client != NULL)
        sm.len = sprintf(buf, ":%s!%s@%s %s %s%s", from->client->nick,
                from->client->user, from->client->orighost, cmd,
                (to != NULL ? to : ""), (to != NULL ? " " : ""));
    else if (from->server != NULL)
        sm.len = sprintf(buf, ":%s %s %s%s", from->server->name, cmd, 
                (to != NULL ? to : ""), (to != NULL ? " " : ""));
    else
        sm.len = sprintf(buf, "%s %s%s", cmd,
                (to != NULL ? to : ""), (to != NULL ? " " : ""));
    if (msg != NULL)
        sm.len += vsnprintf(&buf[sm.len], RFC1459_PKT_LEN - 3 - sm.len, msg,
                args);

    strcpy(&buf[sm.len], "\r\n");
    sm.len += 2;

    return &sm;
}
        
USERMODE_FUNC(hostcrypt_usermode_handler) {

    if (cli->orighost == cli->host)
        return 1; /* don't do anything here.. wait til the rc hook */

    if (set)
        hostcrypt_encrypt(cli);
    else {
        /* Do not decrypt their hostname if they've been added to the
         * history section.  This is indicative of an automated unsetting,
         * not a manual one. */
        if (!(cli->flags & IRCD_CLIENT_HISTORY))
            hostcrypt_decrypt(cli);
    }

    return 1;
}

/* This function wraps the encryption of hosts.  It handles shuffling of
 * alternate names, etc. */
static void hostcrypt_encrypt(client_t *cli) {

    if (OPER(cli) && hostcrypt.operhost != NULL)
        strlcpy(cli->host, hostcrypt.operhost, HOSTLEN + 1);
    else
        strlcpy(cli->host, hostcrypt.crypter(cli), HOSTLEN + 1);
}

/* This function wraps the decryption of hosts.  It's similar to the above. */
static void hostcrypt_decrypt(client_t *cli) {

    strcpy(cli->host, cli->orighost);
}

/*****************************************************************************
 * AustHex-style encrypter (not 1:1 compatible)                              *
 *****************************************************************************/
#define AUSTHEX_SEED 150 /* XXX: should be configurable at some point */
#define AUSTHEX_HASH_BIG 40000 /* maximum total hash value */
#define AUSTHEX_HASH_SMALL 300 /* maximum partial hash value */

/* used to hash strings */
static unsigned int austhex_hash(char *str) {
    unsigned int ret = 0;

    while (*str != '\0')
        ret = AUSTHEX_SEED * ret + *str++;

    return ret;
}

/* the actual crypter.  here's how it works:
 * hash the total hostname in ascii form
 * if the hostname is an IP address, take the last two segments and replace
 * them with the partial hash of the previous segments (minus the separators)
 * and the total hash.
 * if the hostname is a normal host, take the first segment off and replace it
 * with the hash and (if desired) a prefix given by the user. */
static char *hostcrypt_austhex(client_t *cli) {
    static char buf[HOSTLEN + 1], *bptr;
    unsigned int hash, phash;
    char *s;
    int family = PF_INET; /* ugh. */
    bool ip = false;
#ifdef INET6
    struct in6_addr i6a;
#endif
    struct in_addr ia;
    char quads[4][4]; /* four parts of the dotted quad */

    hash = austhex_hash(cli->orighost) % AUSTHEX_HASH_BIG;

    /* See if it is an IP address.. */
#ifdef INET6
    if ((s = strchr(cli->orighost, ':')) != NULL &&
            inet_pton(PF_INET6, cli->orighost, &i6a) == 1) {
        ip = true;
        family = PF_INET6;
    } else
#endif
    if ((s = strrchr(cli->orighost, '.')) != NULL &&
            inet_pton(PF_INET, cli->orighost, &ia) == 1)
    ip = true;

    if (!ip) {
        /* this is the easy case.. */
        if ((s = strchr(cli->orighost, '.')) != NULL)
            sprintf(buf, "%s%u%s%s", hostcrypt.prefix, hash, s,
                    hostcrypt.suffix);
        else
            sprintf(buf, "%s%u.%s%s", hostcrypt.prefix, hash, cli->orighost,
                    hostcrypt.suffix);

        return buf;
    }

    /* otherwise it's an IP address.. we have to do some pretty ridiculous
     * mucking around here to get this right...  In the IPv6 case I've opted to
     * trim off the lower 8 bytes (64 bits) of the address completely, and
     * replace them with the total hash.  In the IPv4 case we do it as
     * described above. */
#ifdef INET6
    if (family == PF_INET6) {
        s = (char *)&i6a;
        memset(s + 8, 0, 8); /* blech. ;) */
        inet_ntop(PF_INET6, &i6a, buf, HOSTLEN + 1);
        sprintf(buf + strlen(buf), "%u", hash);

        return buf;
    }
#endif
    
    /* calculate the hash values .. */
    strcpy(buf, cli->orighost);
    bptr = buf;
    phash = 0;
    while ((s = strsep(&bptr, ".")) != NULL)
        strcpy(quads[phash++], s);

    phash = (austhex_hash(quads[0]) + austhex_hash(quads[1]) +
            austhex_hash(quads[2])) % AUSTHEX_HASH_SMALL;
    sprintf(buf, "%s.%s.%u.%u", quads[0], quads[1], phash, hash);

    return buf;
}

/*****************************************************************************
 * MD5 encrypter                                                             *
 *****************************************************************************/

/* The md5 crypter is very simple.  It hashes the IP address (in network byte
 * order, natch) in two ways.  First it hashes the whole address, then it
 * and places the top 64 bits (16 bytes of hex-coded data) in two eight-byte
 * portions.  Then it hashes the 'top half' of the address (2 or 8 bytes
 * depending on address family) and places the middle 64 bits of data in two
 * more eight byte portions. */
static char *hostcrypt_md5(client_t *cli) {
    static char buf[HOSTLEN + 1];
    char md5buf[65];
    char addr[IPADDR_SIZE];
    size_t alen;
    char *s;
    bool ip = false;
    int family = PF_INET; 
#ifdef INET6
    struct in6_addr i6a;
#endif
    struct in_addr ia;

    /* See if it is an IP address.. */
#ifdef INET6
    if ((s = strchr(cli->orighost, ':')) != NULL &&
            inet_pton(PF_INET6, cli->orighost, &i6a) == 1) {
        ip = true;
        family = PF_INET6;
    } else
#endif
    if ((s = strrchr(cli->orighost, '.')) != NULL &&
            inet_pton(PF_INET, cli->orighost, &ia) == 1)
    ip = true;

    if (ip) {
        inet_pton(family, cli->orighost, addr);
#ifdef INET6
        if (family == PF_INET6)
            alen = IPADDR_SIZE;
        else
#endif
            alen = 4;

        md5_data(addr, alen, md5buf);
        memset(addr + (alen / 2), 0, alen / 2);
        md5_data(addr, alen, md5buf + 32);
    } else {
        alen = strlen(cli->orighost);
        md5_data(cli->orighost, alen, md5buf);
        
        /* extract only the interesting part of the hostname (domain.tld).
         * For hostnames which are merely domain.tld to begin with, we
         * provide the first set of md5 data as additional "seed" for them. */
        s = strrchr(cli->orighost, '.');
        if (s == NULL)
            /* singleton string like 'localhost' */
            sprintf(buf, "%32s.%s", md5buf, cli->orighost);
        else {
            while (*s != '.' && s > cli->orighost)
                s--; /* walk backwards to find the . or beginning of string */
            if (s == cli->orighost)
                /* host is in pure domain.tld form */
                sprintf(buf, "%32s.%s", md5buf, cli->orighost);
            else
                /* s is the second to last . in the string now */
                sprintf(buf, "%s", s + 1);
        }
        /* buf now contains the host we wish to md5 on */
        alen = strlen(buf);
        md5_data(buf, alen, md5buf + 32);
    }
    
    /* okay, md5buf is now a 64 byte long base16 encoded string.  decode it
     * into itself to get the raw data, then base32 encode the result in four
     * chunks into a new address.  this will be just shy of the maximum
     * hostname length. */
    str_base_decode(BASE16_ENCODING, md5buf, md5buf, 64);
    alen = str_base_encode(BASE32_ENCODING, buf, md5buf, 8);
    buf[alen++] = '.';
    alen += str_base_encode(BASE32_ENCODING, buf + alen, md5buf + 8, 8);
    buf[alen++] = '.';
    alen += str_base_encode(BASE32_ENCODING, buf + alen, md5buf + 16, 8);
    buf[alen++] = '.';
    alen += str_base_encode(BASE32_ENCODING, buf + alen, md5buf + 24, 8);
    buf[alen++] = '\0';

    return buf;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
