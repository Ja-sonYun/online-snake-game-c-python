/*
,------.              ,------.
| Serv |              | Clnt |
`------'              `------'
    |                     |
    |                     |
    | room init           |
    | 1000x1000           |
    +----------.          |
    |          |          |
    |<---------'          |
    |                     |
    |      ,-*req*-.      |
    |      |uniq nm|      |
    |      |ip addr|      |
    |      `-------'      |
    |<--------------------+
    |                     |
    | - user register     |
    |  +id                |
    |  +thread            |
    |  +snake coor(xy)    |
    +-----------------.   |
    |                 |   |
    |<----------------'   |
    |                     |
    |     ,-*res*--.      |
    |     |created.|      |
    |     `--------'      |
    +-------------------->|
    |                     |
    |     |---*7*---|     |
    |     send world      |
    |     (100x100)       |
    +-------------------->|
    |                     |
    |                     | wait user
    |                     | input(udrl)
    |                     +------------.
    |                     |            |                ,--y_coordinate, uint16_t
    |                     |<-----------' ,--x_coodinate,| uint16_t
    |                     |	   ,--input(U|P: 31, DOWN: 3|2, RIGHT: 33, LEFT: 34), uint8_t , or code
    |      send user      | 0xFF 0x30 0xFF 0xFF 0x30 0xFF 0xFF 0x30
    |      input          |	0xFF 0xFF 0xFF 0xFF <-- server sequence number
    |<--------------------+
    |                     |
    | recv all user       |
    | input for 0.5 sec   |
    +-------------------. |
    |                   | |
    |<------------------' |
    |                     |
    |   after wait,       |
    |   send world        |
    |   to all clnt.      |
    |   until sent all,   |
    |   block all input   |
    |   return to *7*     |
    +-------------------->|
    |                     |
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define UP    0x31
#define DOWN  0x32
#define RIGHT 0x33
#define LEFT  0x34
#define INIT_SNAKE_L 3
#define MAX_CLIENT 10
#define RAISE_ERR(str) { printf("[ERR] "str); exit(1); }
#define MAP_X_SIZE 1000
#define MAP_Y_SIZE 1000
#define WALL	1
#define EMPTY   0
#define SNAKE   2
#define H_SNAKE 5
#define BUF_SIZE 2000
#define HOLD  0x01
#define START 0x02

struct user_req
{
	uint8_t input;
	uint16_t x;
	uint16_t y;
	uint32_t seq;
};

struct user_req unpack_req(char *buf)
{
	return (struct user_req)
	{
		.input = buf[0],
		.x = (uint16_t)buf[2],
		.y = (uint16_t)buf[5],
		.seq = (uint32_t)buf[8]
	};
}


struct coor_node
{
	struct coor_node *prev;
	uint16_t x;
	uint16_t y;
	struct coor_node *next;
};

struct user
{
	int id;
	char *name;
	struct sockaddr_in addr;
	int sock;
	int node_length;
	struct coor_node *head;
	struct coor_node *last;
};

// return last
struct coor_node* create_snake(struct user* user_p, uint8_t dir)
{
	user_p->node_length = INIT_SNAKE_L;
	struct coor_node *temp, *n = user_p->head;
	for (int i = 1; i < user_p->node_length; i++)
	{
		temp = (struct coor_node*)calloc(1, sizeof(struct coor_node));
		temp->x = n->x;
		temp->y = n->y;
		switch (dir)
		{
			case UP:
				temp->y = n->y - 1;
				break;
			case DOWN:
				temp->y = n->y + 1;
				break;
			case RIGHT:
				temp->x = n->x - 1;
				break;
			case LEFT:
				temp->x = n->x + 1;
				break;
		}
		n->prev = temp;
		temp->next = n;
		n = temp;
	}
	user_p->last = n;
	return temp;
}
// n-p n-pXn-p

void move_snake(struct user *user_p, uint8_t dir, bool do_inc)
{
	if (do_inc)
	{
		struct coor_node *n_last = (struct coor_node*)calloc(1, sizeof(struct coor_node));
		n_last->next = user_p->last;
		user_p->last->prev = n_last;
		user_p->last = n_last;

		user_p->last->x = user_p->last->next->x;
		user_p->last->y = user_p->last->next->y;
	}

	user_p->last->next->prev = NULL;
	user_p->last->prev = user_p->head;
	user_p->head->next = user_p->last;
	user_p->head = user_p->last;
	user_p->last = user_p->last->next;
	user_p->head->next = NULL;

	switch (dir)
	{
		case UP:
			user_p->head->y = user_p->head->prev->y + 1;
			user_p->head->x = user_p->head->prev->x;
			break;
		case DOWN:
			user_p->head->y = user_p->head->prev->y - 1;
			user_p->head->x = user_p->head->prev->x;
			break;
		case RIGHT:
			user_p->head->y = user_p->head->prev->y;
			user_p->head->x = user_p->head->prev->x + 1;
			break;
		case LEFT:
			user_p->head->y = user_p->head->prev->y;
			user_p->head->x = user_p->head->prev->x - 1;
			break;
	}
}

pthread_mutex_t mutex;
uint32_t seq = 0;

struct user users[MAX_CLIENT] = { 0, };
int users_c = 0;
uint8_t world[MAP_Y_SIZE][MAP_X_SIZE] = { 0, };

void *handler(void *arg);

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_addr, clnt_addr;
	int clnt_addr_ln;
	int proc_user_id;

	pthread_t thread_id;

	if (argc != 2)
	{
		printf("%s: <port>\n", argv[0]);
		exit(1);
	}

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));

	if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
		RAISE_ERR("bind()");

	if (listen(serv_sock, 5) == -1)
		RAISE_ERR("listen()");

	printf("[*] running...\n");

	for (;;)
	{
		clnt_addr_ln = sizeof(clnt_addr);
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_ln);

		pthread_mutex_lock(&mutex);
		users[users_c].id = users_c;
		users[users_c].addr = clnt_addr;
		users[users_c].sock = clnt_sock;
		printf("connected\n");
		struct user* user_p = &users[users_c];
		pthread_mutex_unlock(&mutex);

		pthread_create(&thread_id, NULL, handler, (void*)&user_p);
		pthread_detach(thread_id);
	}
	close(serv_sock);
	return 0;
}

void *handler(void *arg)
{
	struct user* user_p = *((struct user**)arg);
	struct user_req req;
	char buf[BUF_SIZE] = { 0, };

	printf("handler\n");

	/* for (;;) */
	/* { */
	/*     read(user_p->sock, buf, BUF_SIZE); */
	/*     req = unpack_req(buf); */
	/*     if (req.input == HOLD) */
	/*     { */
	/*         printf("connected. \n"); */
	/*     } */
	/*     else if (req.input == START) */
	/*     { */
	/*         printf("started. \n"); */
	/*         break; */
	/*     } */
	/* } */

	struct coor_node* snake_head = (struct coor_node*)calloc(1, sizeof(struct coor_node));
	user_p->head = snake_head;
	snake_head->x = 100;
	snake_head->y = 100;
	create_snake(user_p, RIGHT);

	move_snake(user_p, RIGHT, false);

	printf("x:%d , y:%d\n", user_p->head->x, user_p->head->y);
	printf("x:%d , y:%d\n", user_p->head->prev->x, user_p->head->prev->y);
	printf("x:%d , y:%d\n", user_p->head->prev->prev->x, user_p->head->prev->prev->y);
	/* printf("x:%d , y:%d\n", user_p->head->prev->prev->prev->x, user_p->head->prev->prev->prev->y); */
	/* for (int i = 0; i < user_p->node_length; i++) */
	/* { */
	/*     printf("x:%d , y:%d\n", snake_head->x, snake_head->y); */
	/*     if (snake_head->prev != NULL) */
	/*         snake_head = snake_head->prev; */
	/* } */

	return NULL;
}
