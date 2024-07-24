#ifndef PORT_H
#define PORT_H

#define INIT_NUMPORTS 32
#define INIT_HASH_SIZE 128

#define NUMOFVLAN 4095
#define NOVLAN 0xfff

#define P_GETFLAG 0
#define P_SETFLAG 1
#define P_ADDFLAG 2
#define P_CLRFLAG 3

#define DISCARDING 0
#define LEARNING   1
/* forwarding implies learning */
#define FORWARDING 3

#define ETH_ALEN 6

#define ETH_HEADER_SIZE 14
/* a full ethernet 802.3 frame */
struct ethheader {
	unsigned char dest[ETH_ALEN];
	unsigned char src[ETH_ALEN];
	unsigned char proto[2];
};

struct packet {
	struct ethheader header;
  unsigned char data[1504]; /*including trailer, IF ANY */
};

struct bipacket {
	char filler[4];
	struct packet p;
};

#define pgetprio(X) ((X)[0] >> 5)
#define pgetcfi(X)  (((X)[0] >> 4) & 1)
#define pgetvlan(X) (((X)[0] & 0xf) << 8 + (X)[1])
#define psetprio(X,V) ((X)[0]= ((X)[0] & 0x1f) | (V)<<5)
#define psetcfi(X,V)  ((X)[0]= ((X)[0] & 0xef) | (V&1)<<4)
#define psetvlan(X,V) ({(X)[1]=(V)&0xff;(X)[0]=((X)[0] & 0xf0) | ((V)>>8) & 0xf; (V); })

typedef struct mod_support {
    char *modname;
	int (*sender)(int fd_ctl, int fd_data, void *packet, int len, int port);
	void (*delep)(int fd_ctl, int fd_data, void *descr);
} mod_support;

#define PACKETFILTER(CL, PORT, BUF, LEN)  (LEN)  /* no filtering */

struct endpoint;

extern int ep_get_port(struct endpoint *ep);

extern void setup_description(struct endpoint *ep, char *descr);

extern int close_ep(struct endpoint *ep);

extern struct endpoint *setup_ep(int portno, int fd_ctl,
		int fd_data,
		uid_t user,
		struct mod_support *modfun);

extern void handle_in_packet(struct endpoint *ep, struct packet *packet, int len);

#endif