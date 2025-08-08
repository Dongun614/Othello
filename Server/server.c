//컴파일: gcc server.c -o server -lm
//코드 실행: ./server -n:2 -s:10 -b:24 -t:10 -p:40000 -bp:224.1.1.1

//1. 멀티캐스트 서버 구현
//2. 사용자 이동 구현

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#define TTL 64
#define BUF_SIZE 1024
#define CLIENT_NUM 12

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

int client_sockets[CLIENT_NUM]; // 클라이언트 소켓 배열
pthread_t client_threads[CLIENT_NUM]; //클라이언트 스레드
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; //뮤텍스 선언

//전역 변수
int n, s; //각각 사용자의 수, 2차원 공간의 크기, 시간을 의미
struct client_init init;
struct game_info info;

//함수
void* tcp_thread(void* arg);
void table_relandering(int pos);
void* timer();
void error_handling(char* message);

int main(int argc, char* argv[]){
    printf("프로그램을 시작합니다...\n");
    srand((unsigned)time(NULL));

    if(argc != 7){
        printf("Usage : %s -n -s -b -t -p -bp\n", argv[0]);
        exit(1);
    }
    
    char* val[6]; 
    //val[0]:n(참여하는 플레이어의 수)
    //val[1]:s(판이 놓이는 2차원 공간의 크기) 
    //val[2]:b(판의 개수) 
    //val[3]:t(게임 진행 시간) 
    //val[4]:p(포트 번호)
    //val[5]:bp(브로드캐스트 번호)

    //필요한 값 추출
    for(int i=1; i<argc; i++){
        strtok(argv[i], ":");
        val[i-1] = strtok(NULL, " ");
    }

    //포지션 테이블 메모리 할당
    n = atoi(val[0]);
    for(int i=0; i<n; i++){
        info.pos_table[i] = 0; //초기위치 다 0으로 설정
    }

    //s값을 이용한 테이블 구성
    s = pow(atoi(val[1]), 2);
    for(int i=0; i<s; i++){
        init.table[i] = -100;
    }

    //b값을 통한 판 생성
    pthread_mutex_lock(&mutex);
    int temp;
    for(int i=0; i<atoi(val[2]); i++){
        temp = rand()%s;
        if(init.table[temp] != -100){ //중복되지 않게
            i--;
            continue;
        }

        if(i%2 == 0){ 
            init.table[temp] = 0;
        } else {
            init.table[temp] = 1;
        }
    }
    pthread_mutex_unlock(&mutex);

    //p값을 이용한 타이머 세팅
    pthread_mutex_lock(&mutex);
    info.time = atoi(val[3]) + 3;
    pthread_mutex_unlock(&mutex);

    //세팅 된 초기값은 지속적으로 보낼 패킷에도 저장
    memcpy(info.table, init.table, sizeof(int)*BUF_SIZE);

    //init 패킷 구성
    pthread_mutex_lock(&mutex);
    init.player_cnt = atoi(val[0]);
    init.grid_size = atoi(val[1]);
    init.panel_cnt = atoi(val[2]);
    init.game_time = atoi(val[3]);
    init.port = atoi(val[4]);
    pthread_mutex_unlock(&mutex);

    //TCP socket 만들기
    int serv_TCP;
    struct sockaddr_in serv_tcp_adr;
    struct sockaddr_in clnt_tcp_adr;
    socklen_t clnt_tcp_adr_sz;
    serv_TCP = socket(PF_INET, SOCK_STREAM, 0);
    if(serv_TCP == -1){
        error_handling("TCP socket creation error");
    }

    memset(&serv_tcp_adr, 0, sizeof(serv_tcp_adr));
    serv_tcp_adr.sin_family = AF_INET;
    serv_tcp_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_tcp_adr.sin_port = htons(init.port);

    if(bind(serv_TCP, (struct sockaddr*)&serv_tcp_adr, sizeof(serv_tcp_adr)) == -1){
        error_handling("tcp bind() error\n");
    }
    if(listen(serv_TCP, CLIENT_NUM) == -1){
        error_handling("listen() error");
    }

    clnt_tcp_adr_sz = sizeof(clnt_tcp_adr); 

    //클라이언트 입장시 초기정보 우선적으로 전송
    for(int i=0; i<init.player_cnt; i++){
        client_sockets[i] = accept(serv_TCP, (struct sockaddr*)&clnt_tcp_adr, &clnt_tcp_adr_sz);
        if(client_sockets[i] == -1){
            error_handling("accept() error");
        }
        else{
            printf("Connected client %d\n", client_sockets[i]);
            pthread_mutex_lock(&mutex);
            init.player_id = i;
            init.team = i%2;
            write(client_sockets[i], &init, sizeof(struct client_init));
            //입력값에 대한 정보를 받는 스레드 생성
            pthread_mutex_unlock(&mutex);            
        }
    }

    int con = 1;
    for(int i=0; i<init.player_cnt; i++){
        write(client_sockets[i], &con, sizeof(int));
        pthread_create(&client_threads[i], NULL, tcp_thread, &client_sockets[i]);
    }

    // UDP 멀티
    int serv_UDP;
    //struct sockaddr_in serv_udp_adr;
    struct sockaddr_in mul_adr;
    int time_live = TTL;

    serv_UDP = socket(PF_INET, SOCK_DGRAM, 0);
    if (serv_UDP == -1) {
        error_handling("UDP socket creation error");
    }

    memset(&mul_adr, 0, sizeof(mul_adr));
    mul_adr.sin_family = AF_INET;
    mul_adr.sin_addr.s_addr = inet_addr(val[5]); //multicast IP
    mul_adr.sin_port = htons(atoi(val[4])+1); //Multicast Port

    setsockopt(serv_UDP, IPPROTO_IP, IP_MULTICAST_TTL,
        (void*)&time_live, sizeof(time_live));

    pthread_t time;
    pthread_create(&time, NULL, (void*)timer, NULL);
    while (1) {
        usleep(7000);
        pthread_mutex_lock(&mutex);
        sendto(serv_UDP, &info, sizeof(struct game_info), 0, //n은 플레이어 수
            (struct sockaddr*)&mul_adr, sizeof(mul_adr));
        if(info.time == 0){
            sendto(serv_UDP, &info, sizeof(struct game_info), 0, //n은 플레이어 수
                (struct sockaddr*)&mul_adr, sizeof(mul_adr));
            break;
        }
        pthread_mutex_unlock(&mutex);
    }

    //여기에 게임 결과들 
    int blue_count = 0;
    int red_count = 0;
    int total_count;
    int rest = 0;

    for(int i=0; i<s; i++){
        if(info.table[i] == 1){
            blue_count++;
        } else if(info.table[i] == 0) {
            red_count ++;
        } else rest++;
    }
    if(blue_count > red_count) total_count = 0; //블루팀 승리
    else if(blue_count < red_count) total_count = 1; //레드팀 승리
    else total_count = 2; // 무승부

    for(int i=0; i<init.player_cnt; i++){
        write(client_sockets[i], &blue_count, sizeof(int));
        write(client_sockets[i], &red_count, sizeof(int));
        write(client_sockets[i], &total_count, sizeof(int));
    }

    close(serv_TCP);
    close(serv_UDP);

    printf("프로그램을 종료합니다...\n");
    return 0;
}

void* tcp_thread(void* arg){
    int clnt_sd = *((int*)arg);
    int count = 3;
    for(int i=0; i<3; i++){
        write(clnt_sd, &count, sizeof(int));
        count--;
    }
    struct user_move move;

    while(1){
        read(clnt_sd, &move, sizeof(struct user_move));
        info.pos_table[move.player_id] = move.pos;
        if(move.enter == 1){
            table_relandering(move.pos);
        }
    }
}

void table_relandering(int pos){
    pthread_mutex_lock(&mutex);
    info.table[pos]++;
    for(int i=0; i<s; i++){
        if(info.table[i] > 0){
            info.table[i] = info.table[i]%2;
        }
    }
    pthread_mutex_unlock(&mutex);
}

void* timer(){
    while(info.time > 0){
        sleep(1);
        pthread_mutex_lock(&mutex);
        info.time--;
        pthread_mutex_unlock(&mutex);
    }
}

void error_handling(char* message){
    printf("%s\n", message);
    exit(1);
}