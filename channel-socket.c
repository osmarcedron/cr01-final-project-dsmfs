/* Copyright (C) - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mohamed Lamine Karaoui <moharaka@gmail.com>, November 2020
 */

/* Main idea
 *
Create a descentralized hash table using sockets
 - new node enters
 - connects to the node to get a node value
 - the server node will compare values and either give it a new port value or make itself a successor
 - the server node will be responsible to give port number to any node connected to it

 Hashing
 - is done by servers of each nodes
 - along with storing pred and succ each server will store a lookup table
 - the succ table will be filled with the hashe table

The code wasn't actually tested as my VirtualBox machine cann't
actually handle running QEMU servers so it freezes as soon as it
starts
*/


#include "channel.h"
#include "internal.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>

#include <net/sock.h>

#define PORT 2325

int test;

#define __BLOCKED_HASH_BITS	7
static DEFINE_HASHTABLE(dsm_comm_htable, __BLOCKED_HASH_BITS);
static DEFINE_SPINLOCK(dsm_comm_hlock);

struct dsm_comm_hentry
{
	int tgt_id;
	dsm_request_t request;
	struct hlist_node hlink;
	socket socket;
};

void print_request(dsm_request_t *request);

#define MAX_SEMA 12+1
struct semaphore dsm_request_semaphores[MAX_SEMA][DSM_REQ_NUM];
struct semaphore dsm_response_semaphores[MAX_SEMA];
		
static void sema_up(int sema_id, int response, int req_type)
{
	dsm_debug("sema %d is it a response? %d, req_type %d\n", sema_id, response, req_type);

	if(response)
		up(&dsm_response_semaphores[sema_id]);
	else
		up(&dsm_request_semaphores[sema_id][req_type-1]);
}

static int sema_down(int sema_id, int response, int req_type)
{
	dsm_debug("sema %d is it a response? %d, req_type %d\n", sema_id, response, req_type);

	if(response)
		return down_interruptible(&dsm_response_semaphores[sema_id]);
	else
		return down_interruptible(&dsm_request_semaphores[sema_id][req_type-1]);
}

static int channel_put_request(int target_id, int local_id, dsm_request_t *request)
{
	int src_id;
 	struct dsm_comm_hentry *entry;

	src_id = request->src_id;

	entry = kmalloc(sizeof(struct dsm_comm_hentry), GFP_KERNEL);
	entry->tgt_id = target_id;
	entry->request = *request;
	if(request->length)
	{
		entry->request.payload = kmalloc(request->length, GFP_KERNEL);
		memcpy(entry->request.payload, request->payload, request->length);
	}

	print_request(request);
	print_request(&entry->request);
	
	if(request->length)
	{
		dsm_debug("hash %d %d\n", jhash(entry->request.payload,entry->request.length,0),
				  jhash(entry->request.payload,entry->request.length,0));
		dsm_debug("content int0 %d\n", *((int*)(entry->request.payload)));
		dsm_debug("content int0 %d\n", *((int*)(request->payload)));
	}
	//dsm_debug("content int0 %d\n", *((int*)(request->payload)));


	// TODO
	// Determines whether this node has the entry or not (HASHING involved)
	// If so, then it confirms the operation was succesfull
	// Else, it sends the request to the its neighbours
	// Using a lookup table

	if(local_id == src_id)	//this is a request
		sema_up(target_id, 0, request->req_type);
	else if (target_id != src_id)	//this is a request (forwarded)
		sema_up(target_id, 0, request->req_type);
	else			//this is a response
		sema_up(target_id, 1, request->req_type);

	return 0;
}

static struct dsm_request_s* channel_get_request(int local_id, enum dsm_request_type req_type)
{
	int ret;
	int bkt;
	int found;
	dsm_request_t* request;
 	struct dsm_comm_hentry *entry;
	struct sockaddr_in sin;

	found = 0;
	entry = NULL;
	request = NULL;

	dsm_debug("DSMFS: %s: local_id %d sema %d\n", 
				__func__, local_id, -1);
	ret=sema_down(local_id, 0, req_type);
	if(ret<0)
		goto out_err;


	spin_lock(&dsm_comm_hlock);
	// Create socket
	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
								 IPPROTO_TCP, &csvc->socket);
	if(error<0) {
		printk(KERN_ERR "cannot create socket\n");
		return -1;
	}

	error = kernel_connect(csvc->socket, (struct sockaddr*)&sin,
							   sizeof(struct sockaddr_in), 0);
	if(error<0) {
		printk(KERN_ERR "cannot connect socket\n");
		return -1;
	}
	printk(KERN_INFO "sock %d connected\n", i++);

	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);

	// IP ADDRESS of central server
	memset(&size_to_send, server_channel->id, 10);
	sprintf(size_to_send, "%d", local_id);
	send_msg(csvc->socket, size_to_send, 10);

	// IP of the server where to store the entry
	server_ip = recv_msg(csvc->socket, buf, len+1);

	sin.sin_addr.s_addr = htonl(server_ip);
	send_msg(csvc->socket, size_to_send + "+" + req_type, 10);

	found = recv_msg(csvc->socket, buf, len+1);

	if(found)
		hash_del(&entry->hlink);
	else
		request=NULL;
	spin_unlock(&dsm_comm_hlock);

	if(found && request->length)
		dsm_debug("hash %d", jhash(request->payload, request->length, 0));

	if(!found)
	{
		sema_up(local_id, 0, req_type);//the request is for another thread
		msleep(1);//TODO: remove me?
	}

out_err:
	return request;
}

static struct dsm_request_s* channel_get_response(int local_id)
{
	int ret;
	int found;
	dsm_request_t* request;
 	struct dsm_comm_hentry *entry;

	found = 0;
	entry = NULL;
	request = NULL;

	dsm_debug("DSMFS: %s: local_id %d sema %d\n", 
				__func__, local_id, 0);
	ret=sema_down(local_id, 1, -1);
	if(ret<0)
		goto out_err;

	spin_lock(&dsm_comm_hlock);

	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);

	// IP ADDRESS of central server
	memset(&size_to_send, server_channel->id, 10);
	sprintf(size_to_send, "%d", local_id);
	send_msg(csvc->socket, size_to_send, 10);

	// IP of the server where to store the entry
	server_ip = recv_msg(csvc->socket, buf, len+1);

	sin.sin_addr.s_addr = htonl(server_ip);
	send_msg(csvc->socket, size_to_send + "+" + req_type, 10);

	found = recv_msg(csvc->socket, buf, len+1);

	if(found)
		hash_del(&entry->hlink);
	else
		request=NULL;
	spin_unlock(&dsm_comm_hlock);

	if(found && request->length)
		dsm_debug("hash %d", jhash(request->payload, request->length, 0));

	if(!found)
	{
		sema_up(local_id, 1, -1);//the request maybe for another thread
		msleep(1);//TODO: remove me?
	}

out_err:
	return request;
}


static void dsm_channel_init(int local_id)
{
	int i,j;

	if(local_id != 0)/* only 0 initialize the channels */
		return;
		
	for (i=0; i< MAX_SEMA; i++)
	{
		for (j=0; j< DSM_REQ_NUM; j++)
			sema_init(&dsm_request_semaphores[i][j], 0);
		sema_init(&dsm_response_semaphores[i], 0);
	}
}

int start_listen(void)
{
	int error, i, size;
	struct sockaddr_in sin;

    // Create socket
	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
							 IPPROTO_TCP, &svc->listen_socket);
	if(error<0) {
		printk(KERN_ERR "cannot create socket\n");
		return -1;
	}

    // It converts to all available interfaces to byte order.
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);

    // Option needed to allow sockets to be reusable
	int opt = 1;
    error = kernel_setsockopt(svc->listen_socket, SOL_SOCKET,
							  SO_REUSEADDR, (char *)&opt, sizeof (opt));
    if (error < 0) {
		printk(KERN_ERR "Error setting socket options %d\n", error);
        return error;
    }

    // Bind the socket
	error = kernel_bind(svc->listen_socket, (struct sockaddr*)&sin,
			sizeof(sin));
	if(error < 0) {
		printk(KERN_ERR "cannot bind socket, error code: %d\n", error);
		return -1;
	}

    // Listen socket
	error = kernel_listen(svc->listen_socket,5);
	if(error<0) {
		printk(KERN_ERR "cannot listen, error code: %d\n", error);
		return -1;
	}
	return 0;
}

dsm_channel_t* dsm_channel_create(int local_id,
					struct super_block *sb,
					int central_port,
					char* central_ip)
{
	dsm_channel_t *server_channel = kmalloc(sizeof(dsm_channel_t), GFP_KERNEL);
	server_channel->id=local_id;
	server_channel->sb=sb;

	start_listen();

	dsm_channel_init(local_id);

	return server_channel;
}

void dsm_drop_request(dsm_request_t* request)
{
	struct dsm_comm_hentry *entry=NULL;

	BUG_ON(!request);

	if(request->length)
		kfree(request->payload);
	entry = container_of(request, struct dsm_comm_hentry, request);
	kfree(entry);
}

int send_msg(struct socket *sock, char *buf, int len)
{
	struct msghdr msg;
	struct kvec iov;
	int size;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	msg.msg_name = 0;
	msg.msg_namelen = 0;

    // Send the message and print when it contains
    // something (size > 0)
	size = kernel_sendmsg(sock, &msg, &iov, 1, len);

	if (size > 0)
		printk(KERN_INFO "message sent!\n");

	return size;
}

int recv_msg(struct socket *sock, unsigned char *buf, int len)
{
	struct msghdr msg;
	struct kvec iov;
	int size = 0;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	msg.msg_name = 0;
	msg.msg_namelen = 0;

    // Receive the message and print when it contains something (size > 0)
	size = kernel_recvmsg(sock, &msg, &iov, 1, len, msg.msg_flags);

	if (size > 0)
		printk(KERN_ALERT "the message is : %s\n",buf);

	return size;
}


int dsm_channel_get_request(dsm_channel_t* server_channel,
							dsm_request_t** request, int tx_id,
							enum dsm_request_type req_type)
{
	dsm_request_t * ret=NULL;
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);

		if(tx_id==-1)
			ret=channel_get_request(server_channel->id, req_type);
		else
			ret=channel_get_response(server_channel->id, tx_id);
	}while(ret==NULL && !kthread_should_stop());
	//}while(ret==NULL);

	*request=ret;

	return 0;
}

int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request)
{
	channel_put_request(target_node, server_channel->id, request);
	return 0;
}
