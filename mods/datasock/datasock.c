#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <grp.h>
#include <stdint.h>
#include <poll.h>

#include "dynload.h"
#include "module_interop.h"
#include "cli_support.h"
#include "mgmt.h"
#include "basic_dt.h"
#include "debug.h"
#include "port.h"
#include "hash.h"

#define VDESTDSOCK "/var/run/vde.ctl"
#define VDETMPSOCK "/tmp/vde.ctl"

#define PATH_MAX 4096

DT_Methods dt;
DTPLANE_INIT(dt);

HIDDEN Module_Data data = {0};

static unsigned int ctl_type;
static unsigned int wd_type;
static unsigned int data_type;

static char *rel_ctl_socket = NULL;
static char ctl_socket[PATH_MAX];

static int mode = -1;
static int dirmode = -1;
static gid_t grp_owner = -1;


#define DATA_BUF_SIZE 131072
#define SWITCH_MAGIC 0xfeedface
#define REQBUFLEN 256
#define ETH_ALEN 6

enum request_type { REQ_NEW_CONTROL, REQ_NEW_PORT0 };

struct __attribute__((packed)) req_v1_new_control_s {
	unsigned char addr[ETH_ALEN];
	struct sockaddr_un name;
};

struct request_v1 {
	uint32_t magic;
	enum request_type type;
	union {
		struct req_v1_new_control_s new_control;
	} u;
	char description[];
} __attribute__((packed));

struct request_v3 {
	uint32_t magic;
	uint32_t version;
	enum request_type type;
	struct sockaddr_un sock;
	char description[];
} __attribute__((packed));

union request {
	struct request_v1 v1;
	struct request_v3 v3;
};


HIDDEN struct mod_support modfun = {
    .modname = "datasock",
    .sender = NULL,
    .delep = NULL,
};

static int send_datasock(int fd_ctl, int fd_data, void *packet, int len, int port)
{
	while (send(fd_data, packet, len, 0) < 0) {
		int rv=errno;

		if(rv != EAGAIN && rv != EWOULDBLOCK) 
			WARN("send_sockaddr port %d: %s",port,strerror(errno));
		else
			rv=EWOULDBLOCK;
		return -rv;
	}
	return 0;
}

static void delep_datasock(int fd_ctl, int fd_data, void *descr)
{
	if (fd_data>=0) remove_fd(fd_data);
	if (fd_ctl>=0) remove_fd(fd_ctl);
	if (descr) free(descr);
}


EXPORT void init()
{ 

    if (!IS_DT_INITIALIZED(dt))
    {
        ERROR("DT not initialized\n");
        exit(1);
    }

    int connect_fd;
    struct sockaddr_un sun;
    int one = 1;
    const size_t max_ctl_sock_len = sizeof(sun.sun_path) - 5;

    /* Set up default modes */
	if (mode < 0 && dirmode < 0)
	{
		/* Default values */
		mode = 00600;    /* -rw------- for the ctl socket */
		dirmode = 02700; /* -rwx--S--- for the directory */
	}
	else if (mode >= 0 && dirmode < 0)
	{
		/* If only mode (-m) has been specified, we guess the dirmode from it,
		 * adding the executable bit where needed */

#		define ADDBIT(mode, conditionmask, add) ((mode & conditionmask) ? ((mode & conditionmask) | add) : (mode & conditionmask))

		dirmode = 02000 | /* Add also setgid */
			ADDBIT(mode, 0600, 0100) |
			ADDBIT(mode, 0060, 0010) |
			ADDBIT(mode, 0006, 0001);
	}
	else if (mode < 0 && dirmode >= 0)
	{
		/* If only dirmode (--dirmode) has been specified, we guess the ctl
		 * socket mode from it, turning off the executable bit everywhere */
		mode = dirmode & 0666;
	}

    if ((connect_fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        ERROR("Failed to create socket\n");
        exit(1);
    }

    if (setsockopt(connect_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
    {
        ERROR("Failed to set socket options\n");
        exit(1);
    }

    if (fcntl(connect_fd, F_SETFL, O_NONBLOCK) < 0)
    {
        ERROR("Failed to set socket to non-blocking\n");
        exit(1);
    }

    if (rel_ctl_socket == NULL)
    {
        rel_ctl_socket = (geteuid() == 0) ? VDESTDSOCK : VDETMPSOCK;
    }

    if (((mkdir(rel_ctl_socket, 0777) < 0) && (errno != EEXIST)))
    {
        fprintf(stderr, "Cannot create ctl directory '%s': %s\n",
                rel_ctl_socket, strerror(errno));
        exit(-1);
    }

    if (!vde_realpath(rel_ctl_socket, ctl_socket))
    {
        fprintf(stderr, "Cannot resolve ctl dir path '%s': %s\n",
                rel_ctl_socket, strerror(errno));
        exit(1);
    }

    if(chown(ctl_socket,-1,grp_owner) < 0) {
		rmdir(ctl_socket);
		ERROR( "Could not chown socket '%s': %s", sun.sun_path, strerror(errno));
		exit(-1);
	}
	if (chmod(ctl_socket, dirmode) < 0) {
		ERROR("Could not set the VDE ctl directory '%s' permissions: %s", ctl_socket, strerror(errno));
		exit(-1);
	}

    sun.sun_family = AF_UNIX;
    if (strlen(ctl_socket) > max_ctl_sock_len)
        ctl_socket[max_ctl_sock_len] = 0;
    snprintf(sun.sun_path, sizeof(sun.sun_path), "%s/ctl", ctl_socket);

    if (bind(connect_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
    {
        if ((errno == EADDRINUSE) && still_used(&sun))
        {
            LOG("Could not bind to socket '%s/ctl': %s", ctl_socket, strerror(errno));
            exit(-1);
        }
        else if (bind(connect_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
        {
            LOG("Could not bind to socket '%s/ctl' (second attempt): %s", ctl_socket, strerror(errno));
            exit(-1);
        }
    }

    chmod(sun.sun_path,mode);
	if(chown(sun.sun_path,-1,grp_owner) < 0) {
		ERROR( "Could not chown socket '%s': %s", sun.sun_path, strerror(errno));
		exit(-1);
	}

    if (listen(connect_fd, 15) < 0)
    {
        LOG("Could not listen on fd %d: %s", connect_fd, strerror(errno));
        exit(-1);
    }

    ctl_type = dt.add_type(data.module_tag,0);
    wd_type = dt.add_type(data.module_tag, 0);
    data_type = dt.add_type(data.module_tag, 1);
    dt.add_fd(connect_fd, ctl_type, NULL);

    modfun.sender = send_datasock;
    modfun.delep = delep_datasock;

    hash_init(INIT_HASH_SIZE);
    port_init(INIT_NUMPORTS);
    qtimer_init();
}

HIDDEN struct extended_option opts[] = {

};

#define GETFILEOWNER(PATH) ({\
		struct stat s; \
		(stat((PATH),&s)?-1:s.st_uid); \
		})

static struct endpoint *new_port_v1_v3(int fd_ctl, int type_port,
		struct sockaddr_un *sun_out)
{
	int n, portno;
	struct endpoint *ep;
	enum request_type type = type_port & 0xff;
	int port_request=type_port >> 8;
	uid_t user=-1;
	int fd_data;
	struct sockaddr_un sun_in;
	const size_t max_ctl_sock_len = sizeof(sun_in.sun_path) - 8;
	// init sun_in memory
	memset(&sun_in,0,sizeof(sun_in));
	switch(type){
		case REQ_NEW_PORT0:
			port_request= -1;
			/* no break: falltrough */
		case REQ_NEW_CONTROL:
			if (sun_out->sun_path[0] != 0) { //not for unnamed sockets
				if (access(sun_out->sun_path,R_OK | W_OK) != 0) { //socket error
					dt.remove_fd(fd_ctl);
					return NULL;
				}
				user=GETFILEOWNER(sun_out->sun_path);
			}

			if((fd_data = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0){
				ERROR("socket: %s",strerror(errno));
				dt.remove_fd(fd_ctl);
				return NULL;
			}
			if(fcntl(fd_data, F_SETFL, O_NONBLOCK) < 0){
				ERROR("Setting O_NONBLOCK on data fd %s",strerror(errno));
				close(fd_data);
				dt.remove_fd(fd_ctl);
				return NULL;
			}

			if (connect(fd_data, (struct sockaddr *) sun_out, sizeof(struct sockaddr_un)) < 0) {
				ERROR("Connecting to client data socket %s",strerror(errno));
				close(fd_data);
				dt.remove_fd(fd_ctl);
				return NULL;
			}

			ep = setup_ep(port_request, fd_ctl, fd_data, user, &modfun); 
			if(ep == NULL)
				return NULL;
			portno=ep_get_port(ep);
            LOG("DATA TYPE=%d", data_type);
			dt.add_fd(fd_data,data_type,ep);
			sun_in.sun_family = AF_UNIX;
			if (strlen(ctl_socket) > max_ctl_sock_len)
				ctl_socket[max_ctl_sock_len] = 0;
			snprintf(sun_in.sun_path,sizeof(sun_in.sun_path),"%s/%03d.%d",ctl_socket,portno,fd_data);

			if ((unlink(sun_in.sun_path) < 0 && errno != ENOENT) ||
					bind(fd_data, (struct sockaddr *) &sun_in, sizeof(struct sockaddr_un)) < 0){
				ERROR("Binding to data socket %s",strerror(errno));
				close_ep(ep);
				return NULL;
			}
			if (geteuid() != 0)
				user = -1;
			if (user != -1)
				chmod(sun_in.sun_path,mode & 0700);
			else
				chmod(sun_in.sun_path,mode);
			if(chown(sun_in.sun_path,user,grp_owner) < 0) {
				ERROR( "chown: %s", strerror(errno));
				unlink(sun_in.sun_path);
				close_ep(ep);
				return NULL;
			}

			n = write(fd_ctl, &sun_in, sizeof(sun_in));
			if(n != sizeof(sun_in)){
				WARN("Sending data socket name %s",strerror(errno));
				close_ep(ep);
				return NULL;
			}
			if (type==REQ_NEW_PORT0)
				setmgmtperm(sun_in.sun_path);
			return ep;
			break;
		default:
			WARN("Bad request type : %d", type);
			remove_fd(fd_ctl); 
			return NULL;
	}
}

EXPORT void handle_io(unsigned char type, int fd, int revents, void *private_data) {
    

    struct endpoint *ep = private_data;
    
    if(type == data_type) {
        printf("Handling packet!\n");
        if (revents & POLLIN) {
			struct bipacket packet;
			int len;

			len=recv(fd, &(packet.p), sizeof(struct packet),0);
			if(len < 0){
				if (errno == EAGAIN || errno == EWOULDBLOCK) return;
				LOG("Reading  data: %s\n",strerror(errno));
			}
			else if(len == 0) 
				LOG("EOF data port: %s\n",strerror(errno));
			else if(len >= ETH_HEADER_SIZE)
				handle_in_packet(ep, &(packet.p), len);
		}
    } else if (type == wd_type) {
        char reqbuf[REQBUFLEN+1];
		union request *req=(union request *)reqbuf;
		int len;

		len = read(fd, reqbuf, REQBUFLEN);
		if (len < 0) {
			if(errno != EAGAIN && errno != EWOULDBLOCK){
				LOG("Reading request %s", strerror(errno));
				dt.remove_fd(fd); 
			}
			return;
        } else if (len > 0) {
            struct sockaddr_un sa_un;
			reqbuf[len]=0;
			if(req->v1.magic == SWITCH_MAGIC){
				if(req->v3.version == 3) {
					memcpy(&sa_un, &req->v3.sock, sizeof(struct sockaddr_un));
					ep=new_port_v1_v3(fd, req->v3.type, &sa_un);
					if (ep != NULL) {
						dt.set_private_data(fd,ep);
						setup_description(ep,strdup(req->v3.description));
					}
				}
				else if(req->v3.version > 2 || req->v3.version == 2) {
					ERROR("Request for a version %d port, which this "
							"vde_switch doesn't support", req->v3.version);
					dt.remove_fd(fd); 
				}
				else {
					memcpy(&sa_un, &req->v1.u.new_control.name, sizeof(struct sockaddr_un));
					ep=new_port_v1_v3(fd, req->v1.type, &sa_un);
					if (ep != NULL) {
						dt.set_private_data(fd,ep);
						setup_description(ep,strdup(req->v1.description));
					}
				}
			}
			else {
				WARN("V0 request not supported");
				dt.remove_fd(fd); 
				return;
			}
        }
    } else if (type == ctl_type) {
        struct sockaddr addr;
        socklen_t len = sizeof(addr);
        
        int new_fd = accept(fd, &addr, &len);
        if(new_fd < 0) {
            ERROR("Failed to accept connection\n");
            return;
        }

        dt.add_fd(new_fd, wd_type, NULL);
    }
}

EXPORT int parse_args(int argc, void *optarg)
{
    int c = 0;
    switch (argc)
    {

    default:
        c = argc;
    }

    return c;
}


EXPORT Module_Data *on_load(int tag)
{
    data.num_options = sizeof(opts) / sizeof(struct option);
    data.options = opts;
    return &data;
}

EXPORT int cleanup(unsigned char type,int fd,void *arg)
{
    struct sockaddr_un clun;
	int test_fd;
    LOG("Cleaning up type %d, fd %d\n", type, fd);

	if (fd < 0) {
		const size_t max_ctl_sock_len = sizeof(clun.sun_path) - 5;
		if (!strlen(ctl_socket)) {
			/* ctl_socket has not been created yet */
			return;
		}
		if((test_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0){
			ERROR("socket %s",strerror(errno));
		}
		clun.sun_family=AF_UNIX;
		if (strlen(ctl_socket) > max_ctl_sock_len)
			ctl_socket[max_ctl_sock_len] = 0;
		snprintf(clun.sun_path,sizeof(clun.sun_path),"%s/ctl",ctl_socket);
		if(connect(test_fd, (struct sockaddr *) &clun, sizeof(clun))){
			close(test_fd);
			if(unlink(clun.sun_path) < 0)
				WARN("Could not remove ctl socket '%s': %s", ctl_socket, strerror(errno));
			else if(rmdir(ctl_socket) < 0)
				WARN("Could not remove ctl dir '%s': %s", ctl_socket, strerror(errno));
		}
		else WARN("Cleanup not removing files\n");
	} else {
		if (type == data_type && arg != NULL) {
			int portno=ep_get_port(arg);
			const size_t max_ctl_sock_len = sizeof(clun.sun_path) - 8;
			if (strlen(ctl_socket) > max_ctl_sock_len)
				ctl_socket[max_ctl_sock_len] = 0;
			snprintf(clun.sun_path,sizeof(clun.sun_path),"%s/%03d.%d",ctl_socket,portno,fd);
			unlink(clun.sun_path);
		}
		close(fd);
	}
}

EXPORT void help()
{
}
