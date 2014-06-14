#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>
#include <cmath>
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <signal.h>
#include "imgs.h"

using std::string;

#define FBDEV "/dev/fb0"
int scrsize, width, height;
int basesize;

#define CMD_GET_IP_ETH "/home/pi/scripts/ip.sh eth"
#define CMD_GET_IP_WLN "/home/pi/scripts/ip.sh wlan"

//백분율 구하는 식
#define getPercent(total, current) (current / total) * 100
//320:org = b:x 비례식에서 x를 구하는 식
#define getLargeValue(org, b) (org * b) / 320

short *display;
short org[320*240];

//Convert images to char map
char strmap[128][5*5];

bool debug = false;

void cons_log(string message){
	if(debug) printf("%s\n", message.c_str());
}

void drawPixel(int x, int y, short col){
	if((width * y + x) < width * height){
		display[width*y+x] = col;
	}
}

short mkcolor(int r, int g, int b){
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

//(sx, sy)에 이미지 img를 size의 크기로 출력
void writeImage(int sx, int sy, char img[], int size){
	size *= basesize;
	for(int y = 0; y < 5; y++) for(int x = 0; x < 5; x++){
		int prtx = sx + x * size, prty = sy + y * size;
		for(int rx = 0; rx < size; rx++) for(int ry = 0; ry < size; ry++){
			drawPixel(prtx + rx, prty + ry, img[5*y+x] * 0xffff);
		}
	}
}

//화면 중앙에 이미지 img를 size의 크기로 출력
void writeImage(char img[], int size){
	size *= basesize;
	int centerX = width / 2 - 5 * size / 2;
	int centerY = height / 2 - 5 * size / 2;
	writeImage(centerX, centerY, img, size / basesize);
}

//(x, y)에 문자열 s를 size의 크기로 출력
void writeMessage(int x, int y, string s, int size){
	size *= basesize;
	y += height / 2 - size * 5;
	for(int i = 0; i < s.length(); i++){
		int prtx = x + i * (size * 5);
		writeImage(prtx, y, strmap[s.at(i)], size / basesize);
	}
}

//가운데로부터 y만큼 밑에 문자열 s를 size의 크기로 출력(가로: 중앙)
void writeMessage(int y, string s, int size){
	size *= basesize;
	int x = width / 2 - ((s.length()) * (size * 5)) / 2;
	y += height / 2 - size * 5;
	for(int i = 0; i < s.length(); i++){
		int prtx = x + i * (size * 5);
		writeImage(prtx, y, strmap[s.at(i)], size / basesize);
	}
}

//화면 중앙에 문자열 s를 size의 크기로 출력
void writeMessage(string s, int size){
	writeMessage(0, s, size);
}

void clear(){
	for(int i = 0; i < width * height; i++) display[i] = 0;
}

#define img_height(size) size * 5 * basesize

//대기 화면
//연결 되었는지 안되었는지 출력
void printStandBy(bool connected){
	clear();
	writeMessage("pcast", 2);
	char buffer[100];
	FILE* pipe;
	pipe = popen("sh /home/pi/scripts/ip.sh eth", "r");
	fgets(buffer, 100, pipe);
	pclose(pipe);
	writeMessage(img_height(2) + 10, buffer, 1);
	memset(buffer, 0, 100);
	pipe = popen("sh /home/pi/scripts/ip.sh wlan", "r");
	fgets(buffer, 100, pipe);
	pclose(pipe);
	writeMessage(img_height(2) * 2 + 10, buffer, 1);
	if(connected){
		writeMessage(img_height(2) * 3 + 10, "connected", 1);
	}else{
		writeMessage(img_height(2) * 3 + 10, "not connected", 1);
	}
}

//다운로드 화면
void printDownloading(){
	clear();
	writeImage(IMG_DOWNLOAD, 2);
	writeMessage(img_height(2) + 10, "downloading", 1);
}

int server_sockfd, client_sockfd;

void endHandler(int signo){
	cons_log("[PCast:endHandler] SIGTERM/SIGINT 처리기");
	if(client_sockfd != NULL){
		close(client_sockfd);
		cons_log("[PCast:endHandler] 클라이언트 연결 종료됨.");
	}else
		cons_log("[PCast:endHandler] 클라이언트 연결되지 않음.");
	if(server_sockfd != NULL){
		close(server_sockfd);
		cons_log("[PCast:endHandler] 서버 연결 종료됨.");
	}else
		cons_log("[PCast:endHandler] 서버 없음.");
	cons_log("[PCast:endHandler] PCast 종료");
	exit(0);
}

void sockEndHandler(int signo){
	cons_log("[PCast:sockEndHandler] SIGPIPE 처리기");
	if(client_sockfd != NULL){
		close(client_sockfd);
		cons_log("[PCast:sockEndHandler] 연결 종료됨.");
	}else
		cons_log("[PCast:sockEndHandler] 연결되지 않음");
}

bool sock2file(int socket){
	system("sudo rm /tmp/tmp");
	printDownloading();
	cons_log("[PCast:sock2file] 소켓 내용을 파일로 저장합니다.");
	int count = 0;
	int file = open("/tmp/tmp", O_WRONLY | O_CREAT, 0644);
	cons_log("[PCast:sock2file] 내려받는 중입니다!");
	int totalBytes = 0;
	while(true){
		char buffer[1024*1024];
		int recvBytes = recv(socket, buffer, 1024*1024, MSG_DONTWAIT);
		if(recvBytes == 0){
			cons_log("[PCast:sock2file] 연결 종료가 감지되었습니다. sock2file을 종료합니다.");
			return false;
		}else if(recvBytes == -1){
			if(count == 5000){
				char send[] = {1, NULL};
				write(socket, send, 1);
				break;
			}else{
				usleep(1000);
				count++;
			}
		}else{
			count = 0;
			char send[] = {0, NULL};
			write(socket, send, 1);
			write(file, buffer, recvBytes);
			totalBytes += recvBytes;
			//byte to B/KB/MB/GB
			int displaySize = totalBytes;
			char sizeUnit = 'b';
			if(displaySize > 1024){
				displaySize /= 1024;
				sizeUnit = 'k';
			}
			if(displaySize > 1024){
				displaySize /= 1024;
				sizeUnit = 'm';
			}
			//Print current status
			char buf[100];
			sprintf(buf, "received %d%c    ", displaySize, sizeUnit);
			string status(buf);
			writeMessage(0, img_height(2) * 2 + 10, status, 1);
		}
	}
	cons_log("[PCast:sock2file] 수신이 완료되었습니다.");
	return true;
}

int main(int argc, char **argv){
	if(argc == 2){
		if(!strcmp(argv[0], "d")) debug = true;
	}
	signal(SIGTERM, endHandler);
	signal(SIGINT, endHandler);
	signal(SIGPIPE, sockEndHandler);
	memcpy(strmap['0'], IMG_0, 5*5);
	memcpy(strmap['1'], IMG_1, 5*5);
	memcpy(strmap['2'], IMG_2, 5*5);
	memcpy(strmap['3'], IMG_3, 5*5);
	memcpy(strmap['4'], IMG_4, 5*5);
	memcpy(strmap['5'], IMG_5, 5*5);
	memcpy(strmap['6'], IMG_6, 5*5);
	memcpy(strmap['7'], IMG_7, 5*5);
	memcpy(strmap['8'], IMG_8, 5*5);
	memcpy(strmap['9'], IMG_9, 5*5);
	memcpy(strmap['.'], IMG_DOT, 5*5);
	memcpy(strmap[':'], IMG_DOBULE_DOT, 5*5);
	memcpy(strmap['a'], IMG_A, 5*5);
	memcpy(strmap['b'], IMG_B, 5*5);
	memcpy(strmap['c'], IMG_C, 5*5);
	memcpy(strmap['d'], IMG_D, 5*5);
	memcpy(strmap['e'], IMG_E, 5*5);
	memcpy(strmap['f'], IMG_F, 5*5);
	memcpy(strmap['g'], IMG_G, 5*5);
	memcpy(strmap['h'], IMG_H, 5*5);
	memcpy(strmap['i'], IMG_I, 5*5);
	memcpy(strmap['j'], IMG_J, 5*5);
	memcpy(strmap['k'], IMG_K, 5*5);
	memcpy(strmap['m'], IMG_M, 5*5);
	memcpy(strmap['n'], IMG_N, 5*5);
	memcpy(strmap['l'], IMG_L, 5*5);
	memcpy(strmap['o'], IMG_O, 5*5);
	memcpy(strmap['p'], IMG_P, 5*5);
	memcpy(strmap['q'], IMG_Q, 5*5);
	memcpy(strmap['r'], IMG_R, 5*5);
	memcpy(strmap['s'], IMG_S, 5*5);
	memcpy(strmap['t'], IMG_T, 5*5);
	memcpy(strmap['u'], IMG_U, 5*5);
	memcpy(strmap['v'], IMG_V, 5*5);
	memcpy(strmap['w'], IMG_W, 5*5);
	memcpy(strmap['x'], IMG_X, 5*5);
	memcpy(strmap['y'], IMG_Y, 5*5);
	memcpy(strmap['z'], IMG_Z, 5*5);
	memcpy(strmap[' '], IMG_SPACE, 5*5);
	
	//Framebuffer Setup
	int fb_fd;
	fb_fd = open(FBDEV, O_RDWR);
	if(!fb_fd){
		perror("[PCast] Framebuffer 열기 실패");
		exit(1);
	}
	fb_var_screeninfo fvsInfo;
	if(ioctl(fb_fd, FBIOGET_VSCREENINFO, &fvsInfo)){
		perror("[PCast] Framebuffer 크기를 알 수 없습니다.");
		exit(1);
	}
	width = fvsInfo.xres;
	height = fvsInfo.yres;
	scrsize = width * height * 2;
	basesize = (width + height) / 2 / 100;
	display = (short *)mmap(0, scrsize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if((int *)display == -1){
		perror("[PCast] Framebuffer 메모리 할당 실패");
		exit(1);
	}
	cons_log("[PCast] Framebuffer 준비됨");
	system("sudo killall dbus-daemon");
	printStandBy(false);
	//Socket setup
	int state, client_len;
	int pid;
	struct sockaddr_in siClient, siServer;
	state = 0;
	client_len = sizeof(siClient);
	if((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("[PCast] Socket 생성 실패");
		exit(0);
	}
	linger lLinger;
	lLinger.l_onoff = 1;
	lLinger.l_linger = 10;
	bzero(&siServer, sizeof(siServer));
	siServer.sin_family = AF_INET;
	siServer.sin_addr.s_addr = htonl(INADDR_ANY);
	siServer.sin_port = htons(5520);
	state = bind(server_sockfd , (struct sockaddr *)&siServer, sizeof(siServer));
	if (state == -1){
		perror("[PCast] Socket bind 실패");
		exit(0);
	}
	state = listen(server_sockfd, 5);
	if (state == -1){
		perror("[PCast] Socket listen 실패");
		exit(0);
	}
	while(1){
		cons_log("[PCast] 소켓 접속 대기중");
		client_sockfd = accept(server_sockfd, (struct sockaddr *)&siClient,
							   &client_len);
		if (client_sockfd == -1){
			perror("[PCast] Socket accept 실패");
			exit(1);
		}
		if(setsockopt(client_sockfd, SOL_SOCKET, SO_REUSEADDR, &lLinger, sizeof(lLinger)) == -1){
			perror("[PCast] Socket setsockopt 실패");
			exit(1);
		}
		cons_log("[PCast] 접속됨");
		printStandBy(true);
		int avg;
		int orgtime = time(NULL);
		while(1){
			cons_log("[PCast] 수신 대기중");
			char datainfo[2];
			int recvBytes = recv(client_sockfd, datainfo, 2, MSG_WAITALL);
			if(recvBytes == 0 || recvBytes == -1) break;
			else{
				system("sudo killall omxplayer.bin");
				cons_log("[PCast] 데이터 도착");
				cons_log("[PCast] 기기에 전송");
				write(client_sockfd, datainfo, 2);
				cons_log("[PCast] *** 다운로드 모드 ***");
				//*** Downloading mode ***
				cons_log("[PCast] 파일을 다운로드 중입니다. 잠시만 기다려 주세요.");
				bool status = sock2file(client_sockfd);
				if(!status) break;
				system("nohup omxplayer /tmp/tmp &");
				cons_log("[PCast] 동영상 재생이 시작되었습니다.\n");
				printStandBy(true);
			}
		}
		cons_log("[PCast] 연결 끊어짐\n");
		printStandBy(false);
		close(client_sockfd);
	}
}
