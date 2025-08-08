//컴파일 명령어: gcc client.c -o client -lm
//실행 명령어: ./client 203.252.112.31 40000 224.1.1.1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <termios.h>
#include <stdarg.h>
#include <math.h>

#define BUF_SIZE 1024
#define CLIENT_NUM 12
#define GRID_START_X 5
#define GRID_START_Y 5

//클라이언트 접속 시 전송 할 패킷
struct client_init{
    int player_cnt; //플레이어 수
    int player_id; //플레이어 Id
    int grid_size; //한 변의 사이즈
    int panel_cnt; //판의 개수
    int table[BUF_SIZE]; //판의 상태 및 위치
    int game_time; //게임 시간
    int port; //포트 번호
    int team; //팀 번호
};

//클라이언트로부터 지속적으로 받을 패킷
struct user_move{ 
    int player_id;
    int pos;
    int enter;
};

//클라이언트에게 지속적으로 보낼 패킷
struct game_info{
    int time;
    int pos_table[CLIENT_NUM];
    int table[BUF_SIZE];
};

//기타 함수
void* client_send(void* arg);
void makeXY(int cur_pos);
void clrscr();
void gotoxy(int x, int y);
int getch();
void PrintXY(int x, int y, const char *format, ...);
void error_handling(char* message);

//전역 변수
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct client_init init;
struct game_info info;
struct user_move move;
int s;

int current_pos = 0;
int x_pos = GRID_START_X;
int y_pos = GRID_START_Y;


int main(int argc, char* argv[]){
    int sock_tcp;
    struct sockaddr_in serv_tcp_adr;
    struct ip_mreq join_adr; // 이따가 UDP 할때
    socklen_t adr_sz;

    clrscr();

    if(argc != 4){
        printf("Usage : %s <IP> <port> <BP port>\n", argv[0]);
        exit(1);
    }

    //tcp 소캣 생성
    sock_tcp = socket(PF_INET, SOCK_STREAM, 0);
    if(sock_tcp == -1){
        error_handling("TCP socket() error");
    }

    memset(&serv_tcp_adr, 0, sizeof(serv_tcp_adr));
    serv_tcp_adr.sin_family = AF_INET;
    serv_tcp_adr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_tcp_adr.sin_port = htons(atoi(argv[2]));

    if(connect(sock_tcp, (struct sockaddr*)&serv_tcp_adr, sizeof(serv_tcp_adr)) == -1){
        error_handling("connect() error");
    }else{
        pthread_mutex_lock(&mutex);
        printf("Connected............\n");
        read(sock_tcp, &init, sizeof(struct client_init));
        s = pow(init.grid_size, 2.0);
        pthread_mutex_unlock(&mutex);
    }

    int con;
    pthread_mutex_lock(&mutex);
    gotoxy(1,1);
    printf("다른 사용자의 접속을 기다리는 중입니다 ...");
    fflush(stdout);
    pthread_mutex_unlock(&mutex);

    //모든 유저가 접속했을 때 서버로부터 전송된 continue 값을 받음
    read(sock_tcp, &con, sizeof(int));

    //전송 패킷에 대한 기본 설정
    pthread_t clnt_send;
    pthread_create(&clnt_send, NULL, client_send, (void*)&sock_tcp);

    pthread_mutex_lock(&mutex);
    clrscr();
    fflush(stdout);
    pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&mutex);
    printf("게임을 시작합니다...\n");
    int count;
    for(int i=0; i<3; i++){
        read(sock_tcp, &count, sizeof(int));
        printf("%d\n.\n.\n", count);
        sleep(1);
    }

    clrscr();
    fflush(stdout);
    gotoxy(1,1);
    if(init.player_id % 2 == 0){
        printf("You are blue team");
    } else {
        printf("You are red team");
    }
    fflush(stdout);

    gotoxy(1,4);
    printf("--------------------");
    gotoxy(1, init.grid_size + 5);
    printf("--------------------");
    fflush(stdout);
    pthread_mutex_unlock(&mutex);

    // udp 멀티
    int sock_udp;
    struct sockaddr_in serv_udp_adr;

    sock_udp = socket(PF_INET, SOCK_DGRAM, 0);
    if(sock_udp == -1){
        error_handling("UDP socket() error\n");
    }

    // Add SO_REUSEADDR socket option
    int optvalue=1;
    if (setsockopt(sock_udp, SOL_SOCKET, SO_REUSEADDR, &optvalue, sizeof(optvalue)) < 0) {
        error_handling("reuse error\n");
    }

    memset(&serv_udp_adr, 0, sizeof(serv_udp_adr));
    serv_udp_adr.sin_family = AF_INET;
    serv_udp_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_udp_adr.sin_port = htons(atoi(argv[2]) + 1);

    if(bind(sock_udp, (struct sockaddr*)&serv_udp_adr, sizeof(serv_udp_adr)) == -1){
        error_handling("bind() error");
    }

    join_adr.imr_multiaddr.s_addr = inet_addr(argv[3]); // GroupIP
    join_adr.imr_interface.s_addr = htonl(INADDR_ANY);

    setsockopt(sock_udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&join_adr, sizeof(join_adr));

    while(1){
        pthread_mutex_lock(&mutex);
        recvfrom(sock_udp, &info, sizeof(struct game_info), 0, NULL, 0);
        pthread_mutex_unlock(&mutex);

        usleep(7000);
        pthread_mutex_lock(&mutex);
        gotoxy(1,3);
        printf("Left time: %d.0", info.time);
        int x = GRID_START_X, y = GRID_START_X;

        for(int i = 0; i < s; i++){
            if(info.table[i] == 0){
                PrintXY(x, y, "\033[41m \033[0m"); //빨간 배경
            } else if(info.table[i] == 1){
                PrintXY(x, y, "\033[44m \033[0m"); //파란 배경
            } else {
                PrintXY(x, y, "\033[40m \033[0m"); // 검은 배경
            }

            //지금 플레이어들의 위치랑 같은 i값이 있는지 확인
            for(int j=0; j<init.player_cnt; j++){
                if(i == info.pos_table[j]){
                    //플레이어들 위치 중 하나인데 내가 아닌 다른 플레이어의 위치라면
                    if(j != init.player_id){
                        //레드팀이면 분홍색, 블루팀이면 하늘색
                        if(j%2 == 0){
                            PrintXY(x, y, "\033[45m \033[0m"); // 분홍색 배경
                        }else{
                            PrintXY(x, y, "\033[46m \033[0m"); // 하늘색 배경
                        }
                    }
                }
            }
            
            x++;
            if((i + 1) % init.grid_size == 0){
                y++;
                x = GRID_START_X;
            }
        }
        gotoxy(x_pos, y_pos);
        fflush(stdout);
        pthread_mutex_unlock(&mutex);

        //시간 다 되면 게임 종료
        if(info.time == 0){
            break;
        }
    }

    pthread_mutex_lock(&mutex);
    gotoxy(1, init.grid_size + 6);
    printf("Game Over..\n");
    int blue_result;
    int red_result;
    int total_result;
    read(sock_tcp, &blue_result, sizeof(int));
    read(sock_tcp, &red_result, sizeof(int));
    read(sock_tcp, &total_result, sizeof(int));
    sleep(1);
    printf(".\n");
    sleep(1);
    printf(".\n");
    sleep(1);
    printf("\033[36mBlue team %d\033[0m : \033[31m%d Red team \033[0m\n", blue_result, red_result);
    sleep(1);
    printf(".\n");
    sleep(1);
    printf(".\n");
    sleep(1);
    if(total_result == 0){
        printf("\033[36mBlue team Win!! \033[0m\n");
    } else if(total_result == 1){
        printf("\033[31mRed team Win!! \033[0m\n");
    } else {
        printf("Draw!!\n");
    }
    fflush(stdout);
    pthread_mutex_unlock(&mutex);

    close(sock_tcp);
    close(sock_udp);

    return 0;
}

void* client_send(void* arg){
    int clnt_sd = *((int*)arg);
    while(1){
        char key = getch();

        pthread_mutex_lock(&mutex);
        move.player_id = init.player_id;
        if(key == 'w'){
            current_pos -= init.grid_size;
            makeXY(current_pos);
            move.pos = current_pos;
            move.enter = 0;
            write(clnt_sd, &move, sizeof(struct user_move));
        } else if(key == 's'){
            current_pos += init.grid_size;
            makeXY(current_pos);
            move.pos = current_pos;
            move.enter = 0;
            write(clnt_sd, &move, sizeof(struct user_move));
        } else if(key == 'd'){
            current_pos += 1;
            makeXY(current_pos);
            move.pos = current_pos;
            move.enter = 0;
            write(clnt_sd, &move, sizeof(struct user_move));
        } else if(key == 'a'){
            current_pos -= 1;
            makeXY(current_pos);
            move.pos = current_pos;
            move.enter = 0;
            write(clnt_sd, &move, sizeof(struct user_move));
        } else if(key == '\n'){
            move.pos = current_pos;
            move.enter = 1;
            write(clnt_sd, &move, sizeof(struct user_move));
        }
        pthread_mutex_unlock(&mutex);
    }
}

//current pos를 기반으로 x,y값을 만들어줌
void makeXY(int cur_pos){
    x_pos = cur_pos % init.grid_size + GRID_START_X;
    y_pos = cur_pos / init.grid_size + GRID_START_X;
}

void clrscr(){
	fprintf(stdout, "\033[2J\033[0;0f");
	fflush(stdout);
}

void gotoxy(int x,int y){
    printf("%c[%d;%df",0x1B,y,x);
}

int getch(){
	int c = 0;
	struct termios oldattr, newattr;
	tcgetattr(STDIN_FILENO, &oldattr);
	newattr = oldattr;
	newattr.c_lflag &= ~(ICANON | ECHO);
	newattr.c_cc[VMIN] = 1;
	newattr.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &newattr);
	c = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldattr);
	return c;
}

void PrintXY(int x, int y, const char *format, ...){
    va_list vl;
    gotoxy(x, y);
    va_start(vl, format);
    vprintf(format, vl);
    va_end(vl);
    fflush(stdout);
}

void error_handling(char* message){
    printf("%s\n", message);
    exit(1);
}