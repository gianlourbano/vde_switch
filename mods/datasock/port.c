#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <ctype.h>

#include "bitarray.h"
#include "port.h"
#include "debug.h"
#include "hash.h"
#include "qtimer.h"

typedef struct endpoint {
    int port;
    int fd_ctl;
    int fd_data;
    char *descr;
    struct endpoint *next;
} endpoint;

typedef struct port {
    struct endpoint *ep;
    int flag;

    struct mod_support *ms;

    int (*sender)(int fd_ctl, int fd_data, void* packet, int len, int port);
    int vlanuntag;
    uid_t user;
    gid_t group;
    uid_t curuser;

} port;

/* VLAN MANAGEMENT:
 * table the vlan table (also for inactive ports)
 * vlan bctag is the vlan table -- only tagged forwarding ports mapping
 * vlan bcuntag is the vlan table -- only untagged forwarding ports mapping
 * validvlan is the table of valid vlans
 */

struct {
	bitarray table;
	bitarray bctag;
	bitarray bcuntag;
	bitarray notlearning;
} vlant[NUMOFVLAN+1];
bitarray validvlan;

#define IS_BROADCAST(addr) ((addr[0] & 1) == 1)

#define NOTINPOOL 0x8000

static int pflag = 0;
static int numports;
static struct port** portv;

static int alloc_port(unsigned int portno)
{
	int i=portno;
	if (i==0) {
		/* take one */
		for (i=1;i<numports && portv[i] != NULL && 
				(portv[i]->ep != NULL || portv[i]->flag & NOTINPOOL) ;i++)
			;
	} else if (i<0) /* special case MGMT client port */
		i=0;
	if (i >= numports)
		return -1;
	else {
		if (portv[i] == NULL) {
			struct port *port;
			if ((port = malloc(sizeof(struct port))) == NULL){
				WARN("malloc port %s",strerror(errno));
				return -1;
			} else
			{

				portv[i]=port;
				port->ep=NULL;
				port->user=port->group=port->curuser=-1;

				port->flag=0;
				port->sender=NULL;
				port->vlanuntag=0;
				ba_set(vlant[0].table,i);
			}
		}
		return i;
	}
}

static void free_port(unsigned int portno)
{
	if (portno < numports) {
		struct port *port=portv[portno];
		if (port != NULL && port->ep==NULL) {
			portv[portno]=NULL;
			int i;
			/* delete completely the port. all vlan defs zapped */
			bac_FORALL(validvlan,NUMOFVLAN,ba_clr(vlant[i].table,portno),i);
			free(port);
		}
	}
}

static int user_belongs_to_group(uid_t uid, gid_t gid)
{
	struct passwd *pw=getpwuid(uid);
	if (pw == NULL) 
		return 0;
	else {
		if (gid==pw->pw_gid)
			return 1;
		else {
			struct group *grp;
			setgrent();
			while ((grp = getgrent())) {
				if (grp->gr_gid == gid) {
					int i;
					for (i = 0; grp->gr_mem[i]; i++) {
						if (strcmp(grp->gr_mem[i], pw->pw_name)==0) {
							endgrent();
							return 1;
						}
					}
				}
			}
			endgrent();
			return 0;
		}
	}
}

/* Access Control check:
	 returns 0->OK -1->Permission Denied */
static int checkport_ac(struct port *port, uid_t user)
{
	/*unrestricted*/
	if (port->user == -1 && port->group == -1)
		return 0;
	/*root or restricted to a specific user*/
	else if (user==0 || (port->user != -1 && port->user==user))
		return 0;
	/*restricted to a group*/
	else if (port->group != -1 && user_belongs_to_group(user,port->group))
		return 0;
	else {
		errno=EPERM;
		return -1;
	}
}

struct endpoint *setup_ep(int portno, int fd_ctl, int fd_data, uid_t user,
		struct mod_support *modfun)
{
	struct port *port;
	struct endpoint *ep;

	if ((portno = alloc_port(portno)) >= 0) {
		port=portv[portno];	
		if (port->ep == NULL && checkport_ac(port,user)==0)
			port->curuser=user;
		if (port->curuser == user &&
				(ep=malloc(sizeof(struct endpoint))) != NULL) {
			port->ms=modfun;
			port->sender=modfun->sender;
			ep->port=portno;
			ep->fd_ctl=fd_ctl;
			ep->fd_data=fd_data;
			ep->descr=NULL;

			if(port->ep == NULL) {/* WAS INACTIVE */
				int i;
				/* copy all the vlan defs to the active vlan defs */
				ep->next=port->ep;
				port->ep=ep;
				bac_FORALL(validvlan,NUMOFVLAN,
						({if (ba_check(vlant[i].table,portno)) {
						 ba_set(vlant[i].bctag,portno);

						 }
						 }),i);
				if (port->vlanuntag != NOVLAN) {
					ba_set(vlant[port->vlanuntag].bcuntag,portno);
					ba_clr(vlant[port->vlanuntag].bctag,portno);
					ba_clr(vlant[port->vlanuntag].notlearning,portno);
				}
			} else {
				ep->next=port->ep;
				port->ep=ep;
			}
			return ep;
		}
		else {
			if (port->curuser != user)
				errno=EADDRINUSE;
			else 
				errno=ENOMEM;
			return NULL;
		}
	}
	else {
		errno=ENOMEM;
		return NULL;
	}
}

int ep_get_port(struct endpoint *ep) {
	return ep->port;
}

void setup_description(struct endpoint *ep, char *descr)
{
	ep->descr=descr;
}

static int rec_close_ep(struct endpoint **pep, int fd_ctl)
{
	struct endpoint *this=*pep;
	if (this != NULL) {
		if (this->fd_ctl==fd_ctl) {
			*pep=this->next;
			if (portv[this->port]->ms->delep)
				portv[this->port]->ms->delep(this->fd_ctl,this->fd_data,this->descr);
			free(this);
			return 0;
		} else
			return rec_close_ep(&(this->next),fd_ctl);
	} else
		return ENXIO;
}

static int close_ep_port_fd(int portno, int fd_ctl)
{
	if (portno >=0 && portno < numports) {
		struct port *port=portv[portno];
		if (port != NULL) {
			int rv=rec_close_ep(&(port->ep),fd_ctl);
			if (port->ep == NULL) {
				hash_delete_port(portno);
				port->ms=NULL;
				port->sender=NULL;
				port->curuser=-1;
				int i;
				/* inactivate port: all active vlan defs cleared */
				bac_FORALL(validvlan,NUMOFVLAN,({
							ba_clr(vlant[i].bctag,portno);

							}),i);
				if (port->vlanuntag < NOVLAN) ba_clr(vlant[port->vlanuntag].bcuntag,portno);
			}
			return rv;	
		} else
			return ENXIO;
	} else
		return EINVAL;
}

int close_ep(struct endpoint *ep)
{
	return close_ep_port_fd(ep->port, ep->fd_ctl);
}


int portflag(int op,int f)
{
	int oldflag=pflag;
	switch(op)  {
		case P_GETFLAG: oldflag = pflag & f; break;
		case P_SETFLAG: pflag=f; break;
		case P_ADDFLAG: pflag |= f; break;
		case P_CLRFLAG: pflag &= ~f; break;
	}
	return oldflag;
}

/*********************** sending macro used by Core ******************/

#define SEND_PACKET_PORT(PORT,PORTNO,PACKET,LEN) \
	({\
	 struct port *Port=(PORT); \
	 if (PACKETFILTER(PKTFILTOUT,(PORTNO),(PACKET), (LEN))) {\
	 struct endpoint *ep; \
	 for (ep=Port->ep; ep != NULL; ep=ep->next) \
	 Port->ms->sender(ep->fd_ctl, ep->fd_data, (PACKET), (LEN), ep->port); \
	 } \
	 })

/************************************ CORE PACKET MGMT *****************************/

/* TAG2UNTAG packet:
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *             | Destination     |    Source       |81 00|pvlan| L/T | data
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *                         | Destination     |    Source       | L/T | data
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Destination/Source: 4 byte right shift
 * Length -4 bytes
 * Pointer to the packet: +4 bytes
 * */

#define TAG2UNTAG(P,LEN) \
	({ memmove((char *)(P)+4,(P),2*ETH_ALEN); LEN -= 4 ; \
	 (struct packet *)((char *)(P)+4); })

/* TAG2UNTAG packet:
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *             | Destination     |    Source       | L/T | data
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * 
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * | Destination     |    Source       |81 00|pvlan| L/T | data
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * Destination/Source: 4 byte left shift
 * Length -4 bytes
 * Pointer to the packet: +4 bytes
 * The space has been allocated in advance (just in case); all the modules
 * read data into a bipacket.
 */

#define UNTAG2TAG(P,VLAN,LEN) \
	({ memmove((char *)(P)-4,(P),2*ETH_ALEN); LEN += 4 ; \
	 (P)->header.src[2]=0x81; (P)->header.src[3]=0x00;\
	 (P)->header.src[4]=(VLAN >> 8); (P)->header.src[5]=(VLAN);\
	 (struct packet *)((char *)(P)-4); })

#define HUB_TAG 0x1

void handle_in_packet(struct endpoint *ep,  struct packet *packet, int len)
{
	int tarport;
	int vlan,tagged;
	int port=ep->port;

	//DG minimum length of a packet is 60 bytes plus trailing CRC
	if (__builtin_expect(len < 60, 0)) {
		memset(packet->data+len,0,60-len);
		len=60;
	}

	if(PACKETFILTER(PKTFILTIN,port,packet,len)) {
		if (pflag & HUB_TAG) { /* this is a HUB */
			int i;
			for(i = 1; i < numports; i++)
				if((i != port) && (portv[i] != NULL))
					SEND_PACKET_PORT(portv[i],i,packet,len);

		} else { /* This is a switch, not a HUB! */
			if (packet->header.proto[0] == 0x81 && packet->header.proto[1] == 0x00) {
				tagged=1;
				vlan=((packet->data[0] << 8) + packet->data[1]) & 0xfff;
				if (! ba_check(vlant[vlan].table,port))
					return; /*discard unwanted packets*/
			} else {
				tagged=0;
				if ((vlan=portv[port]->vlanuntag) == NOVLAN)
					return; /*discard unwanted packets*/
			}

			/* The port is in blocked status, no packet received */
			if (ba_check(vlant[vlan].notlearning,port)) return; 

			/* We don't like broadcast source addresses */
			if(! (IS_BROADCAST(packet->header.src))) {

				int last = find_in_hash_update(packet->header.src,vlan,port);
				/* old value differs from actual input port */
				if(last >=0 && (port != last)){
					LOG("MAC %02x:%02x:%02x:%02x:%02x:%02x moved from port %d to port %d",packet->header.src[0],packet->header.src[1],packet->header.src[2],packet->header.src[3],packet->header.src[4],packet->header.src[5],last,port);
				}
			}
			/* static void send_dst(int port,struct packet *packet, int len) */
			if(IS_BROADCAST(packet->header.dest) || 
					(tarport = find_in_hash(packet->header.dest,vlan)) < 0 ){
				/* FST HERE! broadcast only on active ports*/
				/* no cache or broadcast/multicast == all ports *except* the source port! */
				/* BROADCAST: tag/untag. Broadcast the packet untouched on the ports
				 * of the same tag-ness, then transform it to the other tag-ness for the others*/
				if (tagged) {
					int i;

					ba_FORALL(vlant[vlan].bctag,numports,
							({if (i != port) SEND_PACKET_PORT(portv[i],i,packet,len);}),i);
					packet=TAG2UNTAG(packet,len);
					ba_FORALL(vlant[vlan].bcuntag,numports,
							({if (i != port) SEND_PACKET_PORT(portv[i],i,packet,len);}),i);

				} else { /* untagged */
					int i;

					ba_FORALL(vlant[vlan].bcuntag,numports,
							({if (i != port) SEND_PACKET_PORT(portv[i],i,packet,len);}),i);
					packet=UNTAG2TAG(packet,vlan,len);
					ba_FORALL(vlant[vlan].bctag,numports,
							({if (i != port) SEND_PACKET_PORT(portv[i],i,packet,len);}),i);

				}
			}
			else {
				/* the hash table should not generate tarport not in vlan 
				 * any time a port is removed from a vlan, the port is flushed from the hash */
				if (tarport==port)
					return; /*do not loop!*/
				if (tagged) {
					if (portv[tarport]->vlanuntag==vlan) { /* TAG->UNTAG */
						packet = TAG2UNTAG(packet,len);
						SEND_PACKET_PORT(portv[tarport],tarport,packet,len);
					} else {                               /* TAG->TAG */
						SEND_PACKET_PORT(portv[tarport],tarport,packet,len);
					}
				} else {
					if (portv[tarport]->vlanuntag==vlan) { /* UNTAG->UNTAG */
						SEND_PACKET_PORT(portv[tarport],tarport,packet,len);
					} else {                              /* UNTAG->TAG */
						packet = UNTAG2TAG(packet,vlan,len);
						SEND_PACKET_PORT(portv[tarport],tarport,packet,len);
					}
				}

			} /* if(BROADCAST) */
		} /* if(HUB) */
	} /* if(PACKETFILTER) */
}

static int vlancreate_nocheck(int vlan)
{
	int rv=0;
	vlant[vlan].table=ba_alloc(numports);
	vlant[vlan].bctag=ba_alloc(numports);
	vlant[vlan].bcuntag=ba_alloc(numports);
	vlant[vlan].notlearning=ba_alloc(numports);
	if (vlant[vlan].table == NULL || vlant[vlan].bctag == NULL || 
			vlant[vlan].bcuntag == NULL) 
		return ENOMEM;
	else {
		if (rv == 0) {
			bac_set(validvlan,NUMOFVLAN,vlan);
		}
		return rv;
	}
}

void port_init(int initnumports)
{
	if((numports=initnumports) <= 0) {
		ERROR("The switch must have at least 1 port\n");
		exit(1);
	}
	portv=calloc(numports,sizeof(struct port *));
	/* vlan_init */
	validvlan=bac_alloc(NUMOFVLAN);
	if (portv==NULL || validvlan == NULL) {
		ERROR("ALLOC port data structures");
		exit(1);
	}
	if (vlancreate_nocheck(0) != 0) {
		ERROR("ALLOC vlan port data structures");
		exit(1);
	}
}
