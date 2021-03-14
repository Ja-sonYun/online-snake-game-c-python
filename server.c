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
#define ONE_SEC 1000000

#define printf_clr(str) printf(str KWHT)
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define UNPACKING(buf) \
	(struct user_req) \
	{ \
		.input = buf[0], \
		.x = ctoui16(buf[2], buf[3]), \
		.y = ctoui16(buf[5], buf[6]), \
		.seq = ctoui32(buf[8], buf[9], buf[10], buf[11]) \
	};

union {
	char hex[2];
	uint16_t uint;
} __char_int16_t;

static inline ctoui16(char hex0, char hex1)
{
	union char_int16_t;
	__char_int16_t.hex[0] = hex0;
	__char_int16_t.hex[1] = hex1;
	return __char_int16_t.uint;
}

union {
	char hex[4];
	uint32_t uint;
} __char_int32_t;

static inline ctoui32(char hex0, char hex1, char hex2, char hex3)
{
	union char_int32_t;
	__char_int32_t.hex[0] = hex0;
	__char_int32_t.hex[1] = hex1;
	__char_int32_t.hex[2] = hex2;
	__char_int32_t.hex[3] = hex3;
	return __char_int32_t.uint;
}
struct user_req
{
	uint8_t input;
	uint16_t x;
	uint16_t y;
	uint32_t seq;
};

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
	struct in_addr addr;
	int sock;
	int node_length;
	uint8_t last_cmd;
	struct coor_node *head;
	struct coor_node *last;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t thread_id;
};

struct coor_node* create_snake(struct user* user_p, uint8_t dir);
void move_snake(struct user *user_p, uint8_t dir, bool do_inc);
void *handler(void *arg);
void *time_handler(void *arg);

pthread_cond_t main_loop_cond =	     PTHREAD_COND_INITIALIZER;
pthread_mutex_t m_mutex =            PTHREAD_MUTEX_INITIALIZER;
pthread_t thread_id;
uint32_t seq = 0;

struct user users[MAX_CLIENT] = { 0, };
int users_c = 0;
uint8_t world[MAP_Y_SIZE][MAP_X_SIZE] = { 0, };
int active_users = 0;
int synced_clnt = 0;
bool sending_map = false;


int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_addr, clnt_addr;
	int clnt_addr_ln;
	int proc_user_id;

	int status_when_exist;

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

	pthread_create(&thread_id, NULL, time_handler, NULL);

	for (;;)
	{
		clnt_addr_ln = sizeof(clnt_addr);
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_ln);

		pthread_mutex_lock(&m_mutex);
		pthread_cond_signal(&main_loop_cond);
		struct user* user_p = &users[users_c];
		active_users++;
		user_p->id = users_c;
		user_p->addr = clnt_addr.sin_addr;
		users_c++;
		pthread_mutex_unlock(&m_mutex);

		printf(KGRN"[+] user_id %d, ip %s is connected.\n"KWHT, users_c, inet_ntoa(user_p->addr));
		user_p->sock = clnt_sock;
		pthread_mutex_init(&user_p->mutex, NULL);
		pthread_cond_init(&user_p->cond, NULL);

		pthread_create(&user_p->thread_id, NULL, handler, (void*)&user_p);
		pthread_detach(user_p->thread_id);
	}
	close(serv_sock);

	pthread_join(thread_id, (void*)&status_when_exist);

	if (status_when_exist)
		printf("[*] successfully existed.\n");
	else
		printf("[*] something wrong.\n");

	return 0;
}

void *handler(void *arg)
{
	struct user* user_p = *((struct user**)arg);
	struct user_req req;
	char buf[BUF_SIZE] = { 0, };
	int str_len = 0;

	printf("[*] Enter handler, thread id:%d\n", user_p->id);

	/* while ((str_len = read(user_p->sock, buf, BUF_SIZE)) != 0) */
	/* { */
	/*     printf("got req\n"); */
	/*     req = UNPACKING(buf); */
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

	/* struct coor_node* snake_head = (struct coor_node*)calloc(1, sizeof(struct coor_node)); */
	/* user_p->head = snake_head; */
	/* snake_head->x = 100; */
	/* snake_head->y = 100; */
	/* create_snake(user_p, RIGHT); */

	while ((str_len = read(user_p->sock, buf, BUF_SIZE)) != 0)
	{
		req = UNPACKING(buf);

		pthread_mutex_lock(&m_mutex);
		if (sending_map)
		{
			// send map to clnt
			synced_clnt++;
		}
		pthread_mutex_unlock(&m_mutex);
	}

	pthread_mutex_lock(&m_mutex);
	active_users--;
	pthread_mutex_unlock(&m_mutex);
	printf(KYEL"[-] client disconnected.(user id %d, ip %s)\n"KWHT, user_p->id, inet_ntoa(user_p->addr));

	return NULL;
}

void *time_handler(void *arg)
{
	printf("[*] waiting user...\n");

	pthread_mutex_lock(&m_mutex);
	pthread_cond_wait(&main_loop_cond, &m_mutex);
	pthread_mutex_unlock(&m_mutex);

	for (;;)
	{
		pthread_mutex_lock(&m_mutex);
		if (!active_users)
		{
			printf_clr(KBLU"[*] no user, stop server...\n");
			pthread_cond_wait(&main_loop_cond, &m_mutex);
			printf_clr(KBLU"[*] resume server...\n");
		}

		printf("[-] map parsed, seq:%d, active user: %d\n", seq, active_users);
		if (sending_map && synced_clnt == active_users)
		{
			synced_clnt = 0;
			sending_map = false;
		}
		pthread_mutex_unlock(&m_mutex);

		usleep(ONE_SEC * 0.5);
		/* sending_map = true; */
		/* pthread_mutex_unlock(&m_mutex); */
		seq++;
	}
}

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
	user_p->last_cmd = dir;
	return temp;
}

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

	if ((user_p->last_cmd == DOWN && dir == UP) ||
		(user_p->last_cmd == UP && dir == DOWN) ||
		(user_p->last_cmd == RIGHT && dir == LEFT) ||
		(user_p->last_cmd == LEFT && dir == RIGHT))
	{
		dir = user_p->last_cmd;
	}

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

	user_p->last_cmd = dir;
}
