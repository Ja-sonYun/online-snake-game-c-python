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

// threads
// main_thread -> world_handler_thread: calculate all snakes pos
//         | |
//         | '--> clnts_handler_thread: block all clnt thread when calculating map
//         |
//         '----> (many)clnt_handler_thread:
//	              '--> clnt_map_comm_handler_thread: send map every 0.5 sec after calculate

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MOVECMD_F 0b10000000
#define WRAP_MVCMD_F(CMD) (MOVECMD_F | CMD)
#define IS_MVCMD(CMD)     (CMD & MOVECMD_F)
#define DOWN  WRAP_MVCMD_F(0x31)
#define UP    WRAP_MVCMD_F(0x32)
#define RIGHT WRAP_MVCMD_F(0x33)
#define LEFT  WRAP_MVCMD_F(0x34)
#define INIT_SNAKE_L 4
#define MAX_CLNT_BUF 100
#define MAX_ACTIVE_CLNT 2
#define RAISE_ERR(str) { printf("[ERR] "str); exit(1); }
#define MAP_X_SIZE 1000
#define MAP_Y_SIZE 1000
#define WALL	1
#define EMPTY   0
#define SNAKE   2
#define H_SNAKE 5
#define BUF_SIZE 2000
#define INIT  0x01
#define START 0x02
#define ONE_SEC 1000000
#define UPDATE_INTERVAL ONE_SEC*0.7

#define printf_clr(str) printf(str KWHT"\n")
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

__attribute__ ((noinline)) uint16_t ctoui16(char hex0, char hex1)
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

__attribute__ ((noinline)) uint32_t ctoui32(char hex0, char hex1, char hex2, char hex3)
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
	uint8_t running_thread;
	bool online;
	bool playing;
	bool alive;
	int id;
	char *name;
	struct in_addr addr;
	int sock;
	int node_length;
	uint32_t seq;
	uint8_t pending_cmd;
	uint8_t last_cmd;
	struct coor_node *head;
	struct coor_node *last;
	pthread_cond_t cond;
	pthread_cond_t cal_cond;
	pthread_mutex_t mutex;
	pthread_t clnt_calc_handler_id;
	pthread_t clnt_map_comm_handler_id;
	pthread_t thread_id;
};

typedef struct _p
{
	int u;
	uint8_t t;
} p_t;

struct coor_node* create_snake(struct user* user_p, uint8_t dir);
void move_snake(struct user *user_p, uint8_t dir, bool do_inc);
void parse_all_snakes();
void snake_one_tick(struct user *user_p);
void *clnt_handler(void *arg);
void *clnts_handler(void *arg);
void *world_handler(void *arg);
bool find_empty_area(struct user *user_);
bool game_key_input(struct user_req req, struct user *user_);
void dump_map(int y, int x, int range); // from(y, x) to(y+range, x+range)

pthread_cond_t main_loop_cond =	   PTHREAD_COND_INITIALIZER;
pthread_cond_t clnt_quit_cond =    PTHREAD_COND_INITIALIZER;
pthread_cond_t clnt_ready_cond =   PTHREAD_COND_INITIALIZER;
pthread_cond_t map_ready_cond =    PTHREAD_COND_INITIALIZER;
pthread_cond_t until_next_tick =   PTHREAD_COND_INITIALIZER;
pthread_mutex_t m_mutex =          PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pseudo_mutex =     PTHREAD_MUTEX_INITIALIZER;
pthread_t thread_id;
uint32_t seq = 0;

struct user users[MAX_CLNT_BUF] = { 0, };
int users_c = 0;
p_t world[MAP_Y_SIZE][MAP_X_SIZE] = { 0, };
p_t prev_world[MAP_Y_SIZE][MAP_X_SIZE] = { 0, };
int active_users = 0;
int playing_users = 0;
int calculated_users = 0;
bool block_all_clnt = false;

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_addr, clnt_addr;
	int clnt_addr_ln;

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

	pthread_mutex_init(&m_mutex, NULL);
	pthread_create(&thread_id, NULL, world_handler, NULL);

	for (;;)
	{
		clnt_addr_ln = sizeof(clnt_addr);
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_ln);

		pthread_mutex_lock(&m_mutex);
		if (active_users == MAX_ACTIVE_CLNT)
		{
			printf(KMAG"[WARNING] reached max client %d\n"KWHT, MAX_ACTIVE_CLNT);
			pthread_mutex_unlock(&m_mutex);
			// TODO: function that send error message to clinet;
			continue;
		}
		struct user* user_p = &users[users_c];
		active_users++;
		user_p->id = users_c;
		user_p->addr = clnt_addr.sin_addr;
		user_p->playing = false;
		if (!user_p->online)
			user_p->online = true;
		else
		{
			printf(KRED"[x] user_id %d, suspicious connection.\n"KWHT, users_c);
			memset(&user_p[users_c], 0, sizeof(struct user));
			active_users--;
			pthread_mutex_unlock(&m_mutex);
			continue;
		}
		printf(KGRN"[+] user_id %d, ip %s, socket_id %d is connected.\n"KWHT, users_c, inet_ntoa(user_p->addr), clnt_sock);
		users_c++;
		pthread_mutex_unlock(&m_mutex);

		user_p->sock = clnt_sock;
		user_p->seq = 0;
		user_p->running_thread = 0;
		pthread_cond_init(&user_p->cond, NULL);
		pthread_cond_init(&user_p->cal_cond, NULL);
		pthread_mutex_init(&user_p->mutex, NULL);

		pthread_create(&user_p->thread_id, NULL, clnt_handler, (void*)&user_p);
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

void clnt_threads_handler_cleanup(void *arg)
{
	if (pthread_mutex_trylock(&m_mutex)) // unlock if parent thread is cancelled when mutex is locked
		pthread_mutex_unlock(&m_mutex);
	struct user* user_p = *((struct user**)arg);
	pthread_mutex_lock(&m_mutex);
	user_p->running_thread--;
	pthread_mutex_unlock(&m_mutex);
}

void *clnt_map_comm_handler(void *arg)
{
	struct user* user_p = *((struct user**)arg);
	char buf[BUF_SIZE] = { 0, };
	pthread_cleanup_push(clnt_threads_handler_cleanup, (void*)&user_p);
	pthread_mutex_lock(&m_mutex);
#ifdef DEBUG
	printf(KMAG"[DEBUG|user_id:%d] Enter clnt_map_comm_handler()"KWHT"\n", user_p->id);
#endif
	user_p->running_thread++;
	pthread_mutex_unlock(&m_mutex);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	for (;;)
	{
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_mutex_lock(&m_mutex);
#ifdef DEBUG
		printf(KMAG"[DEBUG|user_id:%d] send map, clnt_map_comm_handler(), seq:%d\n"KWHT, user_p->id, seq);
#endif
		pthread_cond_wait(&until_next_tick, &m_mutex);
		pthread_cond_signal(&user_p->cal_cond); // send signal to clnt_calc_handler, make it start calculate
		if (user_p->online)
		{
			pthread_cond_wait(&user_p->cal_cond, &m_mutex); // wait until calcuated
			calculated_users++;
#ifdef DEBUG
			printf_clr(KYEL"  \\ calculated");
#endif
			pthread_cond_wait(&map_ready_cond, &m_mutex); // wait until calcuated
#ifdef DEBUG
			printf(KMAG"  \\ write at this point, user %d, status:%d, about seq: %d\n"KWHT, user_p->id, user_p->online, seq);
#endif
			// do highlight this user snake
			/* write(user_p->sock, buf, BUF_SIZE); */
		}
		pthread_mutex_unlock(&m_mutex);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		pthread_testcancel();
	}

	pthread_cleanup_pop(0);
}

void *clnt_calc_handler(void *arg)
{
	struct user* user_p = *((struct user**)arg);
	pthread_cleanup_push(clnt_threads_handler_cleanup, (void*)&user_p);
	pthread_mutex_lock(&m_mutex);
	user_p->running_thread++;
	if (active_users > 0)
		pthread_cond_signal(&main_loop_cond);
	pthread_mutex_unlock(&m_mutex);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	for (;;)
	{
		pthread_mutex_lock(&m_mutex);
		pthread_cond_wait(&user_p->cal_cond, &m_mutex); // wait signal from clnt_map_comm_handler()
		if (user_p->online)
		{
#ifdef DEBUG
			printf(KMAG" \\ calculate here, user_id %d, about seq: %d"KWHT"\n", user_p->id, seq);
#endif
			snake_one_tick(user_p);
			pthread_cond_signal(&user_p->cal_cond); // send signal that this is calculated
		}
		pthread_mutex_unlock(&m_mutex);
		pthread_testcancel();
	}

	pthread_cleanup_pop(0);
}

void *clnt_handler(void *arg)
{
	struct user* user_p = *((struct user**)arg);
	struct user_req req;
	char buf[BUF_SIZE] = { 0, };
	int str_len = 0;
	bool init = false;
	pthread_t map_comm_id;
	pthread_t cal_map_id;

	pthread_mutex_lock(&m_mutex);
	user_p->running_thread++;
	pthread_mutex_unlock(&m_mutex);

	while ((str_len = read(user_p->sock, buf, BUF_SIZE)) != 0)
	{
		if (!init)
		{
			req = UNPACKING(buf);
			if (req.input == INIT)
			{
				printf(KGRN"[+] user connected, id:%d."KWHT"\n", user_p->id);
			}
			else if (req.input == START)
			{
				printf(KGRN"[>] user started to play, id:%d."KWHT"\n", user_p->id);
				pthread_create(&map_comm_id, NULL, clnt_map_comm_handler, (void*)&user_p);
				pthread_create(&cal_map_id, NULL, clnt_calc_handler, (void*)&user_p);

				struct coor_node* snake_head = (struct coor_node*)calloc(1, sizeof(struct coor_node));
				pthread_mutex_lock(&m_mutex);

				user_p->head = snake_head;
				user_p->pending_cmd = 0;
				find_empty_area(user_p);
				create_snake(user_p, RIGHT);
				user_p->alive = true;

				playing_users++;
				user_p->playing = true;
				pthread_mutex_unlock(&m_mutex);
				init = true;
			}
		}
		else
		{
			pthread_mutex_lock(&m_mutex);
			pthread_mutex_unlock(&m_mutex);
			req = UNPACKING(buf);

#ifdef DEBUG
			printf(KCYN"[<] user_id %d send commend, key code 0x%X"KWHT"\n", user_p->id, req.input);
#endif
			pthread_mutex_lock(&m_mutex);
			user_p->pending_cmd = req.input;
			pthread_mutex_unlock(&m_mutex);
		}

		memset(buf, 0, BUF_SIZE);
	}


	pthread_mutex_lock(&m_mutex);
	playing_users--;
	printf(KYEL"[-] client disconnected.(user id %d, ip %s)\n"KWHT, user_p->id, inet_ntoa(user_p->addr));
	user_p->online = false;
	user_p->playing = false;
	active_users--;
	user_p->running_thread--;
	pthread_mutex_unlock(&m_mutex);

	pthread_cancel(map_comm_id);
	pthread_cancel(cal_map_id);

	pthread_join(map_comm_id, NULL);
	pthread_join(cal_map_id, NULL);

	pthread_mutex_lock(&m_mutex);
	if (user_p->running_thread == 0)
	{
		printf(KGRN"[-] user id %d -> all threads are successfully quitted."KWHT"\n", user_p->id);
		memset(user_p, 0, sizeof(struct user));
	}
	pthread_mutex_unlock(&m_mutex);

	return NULL;
}

void *clnts_handler(void *arg) // garbage collector?
{
	int i;
	for (;;)
	{
		/* pthread_mutex_lock(&m_mutex); */
		/* pthread_cond_wait(&clnt_quit_cond, &m_mutex); */
		/* usleep(ONE_SEC * 2); // wait for exit */
		/* for (i = 0; i < users_c; i++) */
		/* { */
			/* if (!users[i].online && users[i].running_thread) */
			/* { */
			/*     printf(KRED"[*] %d users thread does not quitted. running pthread_kill()..."KWHT"\n", users[i].id); */
			/*     pthread_kill(users[i].clnt_calc_handler_id, 0); */
			/*     pthread_kill(users[i].clnt_map_comm_handler_id, 0); */
			/* } */
		/* } */
		/* pthread_mutex_unlock(&m_mutex); */
	}
}

void *world_handler(void *arg)
{
	printf("[*] waiting user...\n");
	int calced_user = 0;

	pthread_mutex_lock(&m_mutex);
	pthread_cond_wait(&main_loop_cond, &m_mutex);
	pthread_mutex_unlock(&m_mutex);

	pthread_t clnts_handler_id;

	pthread_create(&clnts_handler_id, NULL, clnts_handler, NULL);

	for (;;)
	{
		pthread_mutex_lock(&m_mutex);
		if (!playing_users)
		{
			/* pthread_cond_broadcast(&until_next_tick); // broadcast to all clnts, do write */
			printf_clr(KBLU"[*] no user, stop server...");
			printf_clr(KBLU"------------------------------");
			calculated_users = 0;
			pthread_cond_wait(&main_loop_cond, &m_mutex);
			printf_clr(KBLU"[*] resume server...");
		}
		else
		{
			seq++;
			pthread_mutex_unlock(&m_mutex);
			pthread_cond_broadcast(&until_next_tick); // resume all sub processes from clnts
#ifdef DEBUG
			printf_clr(KYEL"[*] wait for all user calculated");
#endif
			for (;;)
			{
				pthread_mutex_lock(&m_mutex);
				if (calculated_users == playing_users || playing_users == 0)
				{
					calculated_users = 0;
					pthread_mutex_unlock(&m_mutex);
					break;
				}
				pthread_mutex_unlock(&m_mutex);
			}
			pthread_mutex_lock(&m_mutex);
#ifdef DEBUG
			printf("[-] all calculated. start parsing the map, about seq:%d, playing user: %d\n", seq, playing_users);
#endif
			// map parse function here
			parse_all_snakes();
#ifdef DEBUG
			printf("[-] all parsed. start to write, about seq:%d, playing user: %d\n", seq, playing_users);
#endif
			pthread_cond_broadcast(&map_ready_cond); // broadcast to all clnts, do write

			dump_map(100, 100, 50);
		}
		pthread_mutex_unlock(&m_mutex);

		usleep(UPDATE_INTERVAL);//*0.5);
		printf_clr(KGRN"----- seq++ -----");
	}

	pthread_join(clnts_handler_id, NULL);

	return NULL;
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

// need to lock mutex
bool find_empty_area(struct user *user_)
{
	bool not_empty = false;
	for (int y = 10; y < MAP_Y_SIZE / 10 - 10; y+=2)
	{
		for (int x = 10; x < MAP_X_SIZE / 10 - 10; x+=2)
		{
			///
			for (int yz = 0; yz < 10; yz++)
			{
				for (int yx = 0; yx < 10; yx++)
				{
					if (world[y*10+yz][x*10+yx].t == SNAKE)
					{
						not_empty = true;
						break;
					}
				}
				if (not_empty)
					break;
			}
			if (!not_empty)
			{
				user_->head->x = x * 10 + 5;
				user_->head->y = y * 10 + 5;
				return true;
			}
			not_empty = false;
		}
	}

	return false;
}

bool game_key_input(struct user_req req, struct user *user_)
{
	pthread_mutex_lock(&m_mutex);
	// check something is there in front of the head
	move_snake(user_, req.input, 0);

	pthread_mutex_unlock(&m_mutex);
}


// already locked before call this
void snake_one_tick(struct user *user_p)
{
	if (user_p->pending_cmd == 0 || !IS_MVCMD(user_p->pending_cmd))
		move_snake(user_p, user_p->last_cmd, 0);
	else
	{
		move_snake(user_p, user_p->pending_cmd, 0);
		user_p->pending_cmd = 0;
	}
}

// mutex locked
void parse_all_snakes()
{
	struct coor_node *t;
	memcpy(&prev_world, world, sizeof(world));
	memset(&world, 0, sizeof(world));
	for (int i = 0; i < users_c; i++)
	{
		if (users[i].playing)
		{
			/* if (world[users[i].head->y][users[i].head->x].t == SNAKE)// or reached border of world */

			// redraw map
			t = users[i].head;
			for (int j = 0; j < users[i].node_length; j++)
			{
				world[t->y][t->x].t = SNAKE;
				world[t->y][t->x].u = users[i].id;
				/* printf(KCYN"user id %d -> head x: %d, y: %d"KWHT"\n", users[i].id, t->x, t->y); */
				if (t->prev == NULL)
					break;
				t = t->prev;
			}

		}
	}
}

void dump_map(int y, int x, int range)
{
	for (int i = y; i < y+range; i++)
	{
		for (int j = x; j < x+range; j++)
		{
			if (world[i][j].t == SNAKE)
				putchar('*');
			else
				putchar(' ');
		}
		putchar('\n');
	}
}
