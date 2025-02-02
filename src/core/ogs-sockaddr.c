/*
 * Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ogs-core.h"

#undef OGS_LOG_DOMAIN
#define OGS_LOG_DOMAIN __ogs_sock_domain

int ogs_getaddrinfo(ogs_sockaddr_t **sa_list, 
        int family, const char *hostname, uint16_t port, int flags)
{
    *sa_list = NULL;
    return ogs_addaddrinfo(sa_list, family, hostname, port, flags);
}

int ogs_freeaddrinfo(ogs_sockaddr_t *sa_list)
{
    ogs_sockaddr_t *next = NULL, *addr = NULL;

    addr = sa_list;
    while (addr) {
        next = addr->next;
        ogs_free(addr);
        addr = next;
    }

    return OGS_OK;
}

int ogs_addaddrinfo(ogs_sockaddr_t **sa_list, 
        int family, const char *hostname, uint16_t port, int flags)
{
    int rc;
    char service[NI_MAXSERV];
    struct addrinfo hints, *ai, *ai_list;
    ogs_sockaddr_t *prev;
    char buf[OGS_ADDRSTRLEN];

    ogs_assert(sa_list);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = flags;

    ogs_snprintf(service, sizeof(service), "%u", port);

    rc = getaddrinfo(hostname, service, &hints, &ai_list);
    if (rc != 0) {
        ogs_log_message(OGS_LOG_ERROR, ogs_socket_errno,
                "getaddrinfo(%d:%s:%d:0x%x) failed",
                family, hostname, port, flags);
        return OGS_ERROR;
    }

    prev = NULL;
    if (*sa_list) {
        prev = *sa_list;
        while(prev->next) prev = prev->next;
    }
    for (ai = ai_list; ai; ai = ai->ai_next) {
        ogs_sockaddr_t *new;
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
            continue;

        new = ogs_calloc(1, sizeof(ogs_sockaddr_t));
        memcpy(&new->sa, ai->ai_addr, ai->ai_addrlen);
        new->ogs_sin_port = htobe16(port);
        ogs_trace("addr:%s, port:%d", OGS_ADDR(new, buf), port);

        if (!prev)
            *sa_list = new;
        else
            prev->next = new;

        prev = new;
    }

    freeaddrinfo(ai_list);

    if (prev == NULL) {
        ogs_log_message(OGS_LOG_ERROR, ogs_socket_errno,
                "ogs_getaddrinfo(%d:%s:%d:%d) failed",
                family, hostname, port, flags);
        return OGS_ERROR;
    }

    return OGS_OK;
}

int ogs_filteraddrinfo(ogs_sockaddr_t **sa_list, int family)
{
    ogs_sockaddr_t *addr = NULL, *prev = NULL, *next = NULL;

    ogs_assert(sa_list);

    prev = NULL;
    addr = *sa_list;
    while (addr) {
        next = addr->next;

        if (addr->ogs_sa_family != family) {
            if (prev)
                prev->next = addr->next;
            else
                *sa_list = addr->next;
            ogs_free(addr);

        } else {
            prev = addr;
        }

        addr = next;
    }

    return OGS_OK;
}

int ogs_copyaddrinfo(ogs_sockaddr_t **dst, const ogs_sockaddr_t *src)
{
    ogs_sockaddr_t *d;
    const ogs_sockaddr_t *s;

    for (*dst = d = NULL, s = src; s; s = s->next)
    {
        if (!d)
        {
            d = ogs_calloc(1, sizeof *s);
            *dst = memcpy(d, s, sizeof *s);
        }
        else
        {
            d->next = ogs_calloc(1, sizeof(ogs_sockaddr_t));
            d = memcpy(d->next, s, sizeof *s);
        }
    }
    return OGS_OK;
}

int ogs_sortaddrinfo(ogs_sockaddr_t **sa_list, int family)
{
    ogs_sockaddr_t *head = NULL, *addr = NULL, *new = NULL, *old = NULL;

    ogs_assert(sa_list);

    old = *sa_list;
    while (old) {
        addr = old;

        old = old->next;

        if (head == NULL || addr->ogs_sa_family == family) {
            addr->next = head;
            head = addr;
        } else {
            new = head;
            while(new->next != NULL && new->next->ogs_sa_family != family) {
                new = new->next;
            }
            addr->next = new->next;
            new->next = addr;
        }
    }
    
    *sa_list = head;

    return OGS_OK;
}

ogs_sockaddr_t *ogs_link_local_addr_by_dev(const char *dev)
{
#if defined(HAVE_GETIFADDRS)
	struct ifaddrs *iflist, *cur;
    int rc;

    ogs_assert(dev);

	rc = getifaddrs(&iflist);
    if (rc != 0) {
        ogs_log_message(OGS_LOG_ERROR, ogs_socket_errno, "getifaddrs failed");
        return NULL;
    }

	for (cur = iflist; cur != NULL; cur = cur->ifa_next) {
        ogs_sockaddr_t *addr = NULL;

		if (cur->ifa_addr == NULL) /* may happen with ppp interfaces */
			continue;

        if (strcmp(dev, cur->ifa_name) != 0)
            continue;

        if (cur->ifa_addr->sa_family == AF_INET)
            continue;

        addr = (ogs_sockaddr_t *)cur->ifa_addr;
        if (!IN6_IS_ADDR_LINKLOCAL(&addr->sin6.sin6_addr)) 
            continue;

        addr = ogs_calloc(1, sizeof(ogs_sockaddr_t));
        ogs_assert(addr);
        memcpy(&addr->sa, cur->ifa_addr, ogs_sockaddr_len(cur->ifa_addr));

        freeifaddrs(iflist);
        return addr;
	}

	freeifaddrs(iflist);
#endif
    return NULL;
}

int ogs_filter_ip_version(ogs_sockaddr_t **addr, 
        int no_ipv4, int no_ipv6, int prefer_ipv4)
{
    int rv;
    
    if (no_ipv4 == 1) {
        rv = ogs_filteraddrinfo(addr, AF_INET6);
        ogs_assert(rv == OGS_OK);
    }
    if (no_ipv6 == 1) {
        rv = ogs_filteraddrinfo(addr, AF_INET);
        ogs_assert(rv == OGS_OK);
    }

    if (prefer_ipv4 == 1) {
        rv = ogs_sortaddrinfo(addr, AF_INET);
        ogs_assert(rv == OGS_OK);
    } else {
        rv = ogs_sortaddrinfo(addr, AF_INET6);
        ogs_assert(rv == OGS_OK);
    }

    return OGS_OK;
}


const char *ogs_inet_ntop(void *sa, char *buf, int buflen)
{
    int family;
    ogs_sockaddr_t *sockaddr = NULL;

    sockaddr = sa;
    ogs_assert(sockaddr);
    ogs_assert(buf);
    ogs_assert(buflen >= OGS_ADDRSTRLEN);

    family = sockaddr->ogs_sa_family;
    switch(family) {
    case AF_INET:
        return inet_ntop(family, &sockaddr->sin.sin_addr, buf,
                INET_ADDRSTRLEN);
    case AF_INET6:
        return inet_ntop(family, &sockaddr->sin6.sin6_addr, buf,
                INET6_ADDRSTRLEN);
    default:
        ogs_fatal("Unknown family(%d)", family);
        ogs_abort();
        return NULL;
    }
}

int ogs_inet_pton(int family, const char *src, void *sa)
{
    ogs_sockaddr_t *dst = NULL;

    ogs_assert(src);
    dst = sa;
    ogs_assert(dst);

    dst->ogs_sa_family = family;
    switch(family) {
    case AF_INET:
        return inet_pton(family, src, &dst->sin.sin_addr) == 1 ?
            OGS_OK : OGS_ERROR;
    case AF_INET6:
        return inet_pton(family, src, &dst->sin6.sin6_addr) == 1 ?
             OGS_OK : OGS_ERROR;
    default:
        ogs_fatal("Unknown family(%d)", family);
        ogs_abort();
        return OGS_ERROR;
    }
}

socklen_t ogs_sockaddr_len(const void *sa)
{
    const ogs_sockaddr_t *sockaddr = sa;

    ogs_assert(sa);

    switch(sockaddr->ogs_sa_family) {
    case AF_INET:
        return sizeof(struct sockaddr_in);
    case AF_INET6:
        return sizeof(struct sockaddr_in6);
    default:
        ogs_fatal("Unknown family(%d)", sockaddr->ogs_sa_family);
        ogs_abort();
        return OGS_ERROR;
    }
}

bool ogs_sockaddr_is_equal(void *p, void *q)
{
    ogs_sockaddr_t *a, *b;

    a = p;
    ogs_assert(a);
    b = q;
    ogs_assert(b);

    if (a->ogs_sa_family != b->ogs_sa_family)
        return false;

    if (a->ogs_sa_family == AF_INET && memcmp(
        &a->sin.sin_addr, &b->sin.sin_addr, sizeof(struct in_addr)) == 0)
        return true;
    else if (a->ogs_sa_family == AF_INET6 && memcmp(
        &a->sin6.sin6_addr, &b->sin6.sin6_addr, sizeof(struct in6_addr)) == 0)
        return true;
    else {
        ogs_fatal("Unknown family(%d)", a->ogs_sa_family);
        ogs_abort();
    }

    return false;
}

static int parse_network(ogs_ipsubnet_t *ipsub, const char *network)
{
    /* legacy syntax for ip addrs: a.b.c. ==> a.b.c.0/24 for example */
    int shift;
    char *s, *t;
    int octet;
    char buf[sizeof "255.255.255.255"];

    if (strlen(network) < sizeof buf)
        strcpy(buf, network);
    else
        return OGS_ERROR;

    /* parse components */
    s = buf;
    ipsub->sub[0] = 0;
    ipsub->mask[0] = 0;
    shift = 24;
    while (*s) {
        t = s;
        if (!isdigit(*t))
            return OGS_ERROR;

        while (isdigit(*t))
            ++t;

        if (*t == '.')
            *t++ = 0;
        else if (*t)
            return OGS_ERROR;

        if (shift < 0)
            return OGS_ERROR;

        octet = atoi(s);
        if (octet < 0 || octet > 255)
            return OGS_ERROR;

        ipsub->sub[0] |= octet << shift;
        ipsub->mask[0] |= 0xFFUL << shift;
        s = t;
        shift -= 8;
    }
    ipsub->sub[0] = be32toh(ipsub->sub[0]);
    ipsub->mask[0] = be32toh(ipsub->mask[0]);
    ipsub->family = AF_INET;

    return OGS_OK;
}

/* return values:
 * CORE_EINVAL     not an IP address; caller should see
 *                 if it is something else
 * CORE_BADIP      IP address portion is is not valid
 * CORE_BADMASK    mask portion is not valid
 */
static int parse_ip(
        ogs_ipsubnet_t *ipsub, const char *ipstr, int network_allowed)
{
    /* supported flavors of IP:
     *
     * . IPv6 numeric address string (e.g., "fe80::1")
     * 
     *   IMPORTANT: Don't store IPv4-mapped IPv6 address as an IPv6 address.
     *
     * . IPv4 numeric address string (e.g., "127.0.0.1")
     *
     * . IPv4 network string (e.g., "9.67")
     *
     *   IMPORTANT: This network form is only allowed if network_allowed is on.
     */
    int rc;

    rc = inet_pton(AF_INET6, ipstr, ipsub->sub);
    if (rc == 1) {
        if (IN6_IS_ADDR_V4MAPPED((struct in6_addr *)ipsub->sub)) {
            /* ipsubnet_test() assumes that we don't create IPv4-mapped IPv6
             * addresses; this of course forces the user to specify 
             * IPv4 addresses in a.b.c.d style instead of ::ffff:a.b.c.d style.
             */
            ogs_error("Cannot support IPv4-mapped IPv6: "
                    "Use IPv4 address in a.b.c.d style "
                    "instead of ::ffff:a.b.c.d style");
            return OGS_ERROR;
        }
        ipsub->family = AF_INET6;
    } else {
        rc = inet_pton(AF_INET, ipstr, ipsub->sub);
        if (rc == 1) {
            ipsub->family = AF_INET;
        }
    }

    if (rc != 1) {
        if (network_allowed)
            return parse_network(ipsub, ipstr);
        else
            return OGS_ERROR;
    }
    return OGS_OK;
}

static int looks_like_ip(const char *ipstr)
{
    if (strlen(ipstr) == 0)
        return 0;
    
    if (strchr(ipstr, ':')) {
        /* definitely not a hostname;
         * assume it is intended to be an IPv6 address */
        return 1;
    }

    /* simple IPv4 address string check */
    while ((*ipstr == '.') || isdigit(*ipstr))
        ipstr++;

    return (*ipstr == '\0');
}

static void fix_subnet(ogs_ipsubnet_t *ipsub)
{
    /* in case caller specified more bits in network address than are
     * valid according to the mask, turn off the extra bits
     */
    int i;

    for (i = 0; i < sizeof ipsub->mask / sizeof(int32_t); i++)
        ipsub->sub[i] &= ipsub->mask[i];
}

/* be sure not to store any IPv4 address as a v4-mapped IPv6 address */
int ogs_ipsubnet(ogs_ipsubnet_t *ipsub,
        const char *ipstr, const char *mask_or_numbits)
{
    int rv;
    char *endptr;
    long bits, maxbits = 32;

    ogs_assert(ipsub);
    ogs_assert(ipstr);

    /* filter out stuff which doesn't look remotely like an IP address;
     * this helps callers like mod_access which have a syntax allowing 
     * hostname or IP address;
     * CORE_EINVAL tells the caller that it was probably not intended 
     * to be an IP address
     */
    if (!looks_like_ip(ipstr)) {
        ogs_error("looks_like_ip() is failed");
        return OGS_ERROR;
    }

    /* assume ipstr is an individual IP address, not a subnet */
    memset(ipsub->mask, 0xFF, sizeof ipsub->mask);

    rv = parse_ip(ipsub, ipstr, mask_or_numbits == NULL);
    if (rv != OGS_OK) {
        ogs_error("parse_ip() is failed");
        return rv;
    }

    if (mask_or_numbits) {
        if (ipsub->family == AF_INET6) {
            maxbits = 128;
        }
        bits = strtol(mask_or_numbits, &endptr, 10);
        if (*endptr == '\0' && bits > 0 && bits <= maxbits) {
            /* valid num-bits string; fill in mask appropriately */
            int cur_entry = 0;
            int32_t cur_bit_value;

            memset(ipsub->mask, 0, sizeof ipsub->mask);
            while (bits > 32) {
                ipsub->mask[cur_entry] = 0xFFFFFFFF; /* all 32 bits */
                bits -= 32;
                ++cur_entry;
            }
            cur_bit_value = 0x80000000;
            while (bits) {
                ipsub->mask[cur_entry] |= cur_bit_value;
                --bits;
                cur_bit_value /= 2;
            }
            ipsub->mask[cur_entry] = htobe32(ipsub->mask[cur_entry]);
        }
        else if (inet_pton(AF_INET, mask_or_numbits, ipsub->mask) == 1 &&
            ipsub->family == AF_INET) {
            /* valid IPv4 netmask */
        } else {
            ogs_error("Bad netmask");
            return OGS_ERROR;
        }
    }

    fix_subnet(ipsub);

    return OGS_OK;
}
