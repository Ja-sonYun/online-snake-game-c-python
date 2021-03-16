working on

,----.      ,---------------.,---------------.             ,--------------.,-------------------.       ,-----------------------.
| EP |      | clnts_handler || world_handler |             | clnt_handler || clnt_calc_handler |       | clnt_map_comm_handler |
`----'      `---------------'`---------------'             `--------------'`-------------------'       `-----------------------'
   |                |                |                             |                 |                             |
   |                |                |                             |                 |                             |
   |         pthread_create          |                             |                 |                             |
   +-------------------------------->|                             |                 |                             |
   |                |                |                             |                 |                             |
   |                |    pthread_create(many)                      |                 |                             |
   +-------------------------------------------------------------->|                 |                             |
   |                |                |                             |                 |                             |
   |                |                | pthd_wait(main_loop_cond)   |                 |                             |
   |                |                +--------------------------.  |                 |                             |
   |                |                |                          |  |                 |                             |
   |                |                |<-------------------------'  |                 |                             |
   |                |                |                             |                 |                             |
   |                |                |                             |                pthread_create                 |
   |                |                |                             +---------------------------------------------->|
   |                |                |                             |                 |                             |
   |                |                |                             | pthread_create  |                             |
   |                |                |                             +---------------->|                             |
   |                |                |                             |                 |                             |
   |                |                |          pthd_signal(main_loop_cond)          |                             |
   |                |                |<----------------------------------------------+                             |
   |                |                |                             |                 |                             |
   |                | pthread_create |                             |                 |                             |
   |                |<---------------+                             |                 |                             |
   |                |                |                             |                 |                             |
   |                |                |                             | read loop       |                             |
   |                |                |                             +----------.      |                             |
   |                |                |                             |          |      |                             |
   |                |                |                             |<---------'      |                             |
   |                |                |                             |                 |                             |
   |                |                |                             |                 |                             | pthd_wait(send_map_cond)
   |                |                |                             |                 |                             +-------------------------.
   |                |                |                             |                 |                             |                         |
   |                |                |                             |                 |                             |<------------------------'
   |                |                |                             |                 |                             |
   |                |                |                             |                 | pthd_wait(user->cal_cond)   |
   |                |                |                             |                 +--------------------------.  |
   |                |                |                             |                 |                          |  |
   |                |                |                             |                 |<-------------------------'  |
   |                |                |                             |                 |                             |
   |                |                | loop, e0.5s                 |                 |                             |
   |                |                +------------.                |                 |                             |
   |                |                |            |                |                 |                             |
   |                |                |<-----------'                |                 |                             |
   |                |                |                             |                 |                             |
   |                |                |                        pthd_broadcast(send_map_cond)                        |
   |                |                +---------------------------------------------------------------------------->|
   |                |                |                             |                 |                             |
   |                |                | wait until                  |                 |                             |
   |                |                | all client                  |                 |                             |
   |                |                | calculated                  |                 |                             |
   |                |                +----------------.            |                 |                             |
   |                |                |                |            |                 |                             |
   |                |                |<---------------'            |                 |                             |
   |                |                |                             |                 |                             |
   |                |                |                             |                 | pthd_signal(user->cal_cond) |
   |                |                |                             |                 |<----------------------------+
   |                |                |                             |                 |                             |
   |                |                |                             |                 |                             | pthd_wait(user->cal_cond)
   |                |                |                             |                 |                             +--------------------------.
   |                |                |                             |                 |                             |                          |
   |                |                |                             |                 |                             |<-------------------------'
   |                |                |                             |                 |                             |
   |                |                |                             |                 | pthd_signal(user->cal_cond) |
   |                |                |                             |                 +---------------------------->|
   |                |                |                             |                 |                             |
   |                |                |                             |                 |                             | pthd_wait(map_ready_cond)
   |                |                |                             |                 |                             +--------------------------.
   |                |                |                             |                 |                             |                          |
   |                |                |                             |                 |                             |<-------------------------'
   |                |                |                             |                 |                             |
   | calced_user++  |                |                             |                 |                             |
   +--------------. |                |                             |                 |                             |
   |              | |                |                             |                 |                             |
   |<-------------' |                |                             |                 |                             |
   |                |                |                             |                 |                             |
   |                |                | if calced==active           |                 |                             |
   |                |                +------------------.          |                 |                             |
   |                |                |                  |          |                 |                             |
   |                |                |<-----------------'          |                 |                             |
   |                |                |                             |                 |                             |
   |                |                |                       pthd_broadcast(map_ready_cond)                        |
   |                |                +---------------------------------------------------------------------------->|
   |                |                |                             |                 |                             |
   |                |                |                             |                 |                             |

[*] running...
[*] waiting user...
[+] user_id 0, ip 127.0.0.1, socket_id 4 is connected.
[+] user connected, id:0.
[>] user started to play, id:0.
[DEBUG|user_id:0] Enter clnt_map_comm_handler()
[-] map parsed, seq:0, active user: 1
 \ calculate here, user_id 0, about seq: 0
  \ write at this point, user 0, status:1, about seq: 0
[<] user_id 0 send commend, key code 0x31
[<] user_id 0 send commend, key code 0x31
[<] user_id 0 send commend, key code 0x31
[<] user_id 0 send commend, key code 0x31
[<] user_id 0 send commend, key code 0x31
[<] user_id 0 send commend, key code 0x31
[-] map parsed, seq:1, active user: 1
 \ calculate here, user_id 0, about seq: 1
  \ write at this point, user 0, status:1, about seq: 1
[-] map parsed, seq:2, active user: 1
 \ calculate here, user_id 0, about seq: 2
  \ write at this point, user 0, status:1, about seq: 2
[-] map parsed, seq:3, active user: 1
 \ calculate here, user_id 0, about seq: 3
  \ write at this point, user 0, status:1, about seq: 3
[-] client disconnected.(user id 0, ip 127.0.0.1)
[*] no user, stop server...
------------------------------
 ? quitting clnt_map_comm_handler from user id :0
 ? quitting clnt_calc_handler from user id :0
 ? user id 0 successfully cleaned up
 ? user id 0 successfully cleaned up
[+] user_id 1, ip 127.0.0.1, socket_id 5 is connected.
[+] user connected, id:1.
[>] user started to play, id:1.
[DEBUG|user_id:1] Enter clnt_map_comm_handler()
[*] resume server...
[<] user_id 1 send commend, key code 0x31
[<] user_id 1 send commend, key code 0x31
[<] user_id 1 send commend, key code 0x31
[<] user_id 1 send commend, key code 0x31
[<] user_id 1 send commend, key code 0x31
[<] user_id 1 send commend, key code 0x31
[-] map parsed, seq:5, active user: 1
 \ calculate here, user_id 1, about seq: 5
  \ write at this point, user 1, status:1, about seq: 5
[-] map parsed, seq:6, active user: 1
 \ calculate here, user_id 1, about seq: 6
  \ write at this point, user 1, status:1, about seq: 6
[-] client disconnected.(user id 1, ip 127.0.0.1)
[*] no user, stop server...
------------------------------
 ? quitting clnt_map_comm_handler from user id :1
 ? waiting id 1 exit...
 ? quitting clnt_calc_handler from user id :1
 ? user id 1 successfully cleaned up
 ? user id 1 successfully cleaned up
