#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/utility/binary.hpp>
#include <linux/serial.h>
#include "dynamixel.h"
#include "nbpopen.hpp"
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <iostream> 
#include <stdlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>
#include <cstring>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <math.h>
#include <string> 


typedef unsigned char uchar;
typedef double value_t;

using namespace std;
using namespace boost::interprocess;

#define  MAX_READ_BUF         (1024)
#define  BAUD_RATE            (1)
#define  BASE_DIR             ("MRL")
#define  BUTTON_ID      	  (200)
#define  BT_RD_TIMEOUT        (0.01)
#define  PRESSED        	  (1)

#define  BUTTON_LEFT    	  (1)
#define  BUTTON_RIGHT   	  (2)
#define  BUTTON_LEFT_RIGHT    (3)

#define  ROBOT_RUNNING        (1)
#define  ROBOT_STOPED         (2)
#define  ERROR_RUN			  (3)

int fd = -1;
int robot_status = ROBOT_STOPED;

std::string robot_id;
std::string team_id;
std::string username;

double *shm_bt;

DIR *dirp = 0;
boost::interprocess::managed_shared_memory *managed_shm;

std::string *cmd;
FILE *p_pipe[3];
int log_file[3];

std::string get_tty_path() {
    const char *path = "/dev/";
    DIR *dirp = opendir(path);
    if (dirp == NULL)
        return "";
    struct dirent *dp;
    string ttyUSBName = "";
    while ((dp = readdir(dirp)) != NULL) {
        std::string entry(dp->d_name);
        if ((entry.find("tty.usb") != std::string::npos) ||
        (entry.find("ttyUSB") != std::string::npos)) {
            ttyUSBName = entry;
            break;
        }
    }
    closedir(dirp);
    if (ttyUSBName != ""){
        std::string src(path);
        return src + ttyUSBName; 
    }
    return "";	
}

void stty_speed(int fd) {
    int speed = 1000000;
    // Default termios interface
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
    }
    if (cfsetspeed(&tio, speed) != 0) {
    }
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    }

    // For linux:
    struct serial_struct serinfo;
    if (ioctl(fd, TIOCGSERIAL, &serinfo) < 0) {
    }
    serinfo.flags &= ~ASYNC_SPD_MASK;
    serinfo.flags |= ASYNC_SPD_CUST;
    serinfo.custom_divisor = serinfo.baud_base/((float)speed);
    if (ioctl(fd, TIOCSSERIAL, &serinfo) < 0) {
    }
}

uchar * serialize_packet(DynamixelPacket *pkt){
	int len = pkt->length + 4;
	uchar  *buff = (uchar *) malloc(len);
	memcpy(buff , pkt , len);
	return buff; 
}

double get_time() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + 1E-6*t.tv_usec;
}

DynamixelPacket *parse_status_packet(const char *str) {
    DynamixelPacket *pkt;
    pkt->id = *(str+2);
    pkt->length = *(str+3);
    pkt->instruction = *(str+4);
    memcpy(pkt->parameter, str + 5, pkt->length - 2);
    pkt->checksum = *(str + pkt->length+3);
    return pkt;
}

void get_status_packet(int fd , uchar *buff , DynamixelPacket *pkt){
    double t0 = get_time();
    std::string str;
    while (get_time() - t0 < BT_RD_TIMEOUT) {
        int r = read(fd, buff, MAX_READ_BUF);
        if (r > 0) {
            std::string s((char *)buff,r);
            str += s;        
            int nPacket = 0;
            for (int i = 0 ; i < str.length() ; i++) {
                nPacket = dynamixel_input(pkt, str.at(i), nPacket);
                if (nPacket < 0) {
                    // completed packet 
                    return void();
                }
            }
            usleep((useconds_t) 100);
        } 
    }
}

int open_tty() {
    std::string ttyPath = "";
    do {
        sleep(1);
        ttyPath = get_tty_path();
    } while (ttyPath == "");
    if (fd > -1) {
        close(fd);
    }
    fd = open(ttyPath.c_str() , O_RDWR + O_NOCTTY + O_NONBLOCK);
    if (fd < 0) {
        exit (EXIT_FAILURE);
    }
    stty_speed(fd);
}

int read_button_data() {
    if (robot_status == ROBOT_STOPED) {
        int id = 200;
        unsigned char addr = 30;
        unsigned char len = 1;	
        DynamixelPacket *p = dynamixel_instruction_read_data(id, addr, len);
        uchar buff[MAX_READ_BUF];
        // clear buffer
        int r = read(fd , buff , MAX_READ_BUF);
        uchar *strPkt = serialize_packet(p);
        r = write(fd , strPkt , p->length + 4);
        DynamixelPacket statusPkt;
        get_status_packet(fd , (uchar *)buff,&statusPkt);
        return statusPkt.parameter[0];
    } else {
        // read button data from shared memory4
        return (shm_bt[0] * 2) + shm_bt[1];
    }
}

int run_robot() {
    std::string code_dir("/home/robot/MRL/Player");
    int ret = chdir(code_dir.c_str());
    assert(ret==0);
    close(fd);
    cmd = new std::string[3];
    cmd[0] = "sudo -E lua run_dcm.lua";
    cmd[1] = "sudo -E lua run_cognition.lua";
    cmd[2] = "sudo -E lua run_main.lua";
    for (int i = 0 ; i < 3 ; i++) {
        p_pipe[i] = nbpopen(cmd[i].c_str(), "r");
        if (p_pipe[i] == NULL) 
        return ERROR_RUN;
        if (i < 2) 
            sleep(2);
    }
    return ROBOT_RUNNING;
}

int kill_robot() {
    system("sudo killall lua");
    system("sudo rm -r /dev/shm/*");
    return ROBOT_STOPED;
}

int main(void) {
    open_tty();
    int data;
    int btn[2];
    cout << "Start robot handler" << endl;
    while (true) {
        data = read_button_data();
        btn[0] = (int) floor(data/2);
        btn[1] = data % 2;
        if (btn[0] == PRESSED && robot_status == ROBOT_STOPED ) {
            cout << "robot has been started" << endl;
            int r_stat = run_robot();
            if (r_stat == ROBOT_RUNNING ) {
                robot_status = ROBOT_RUNNING ;
                std::string shm_name("dcmSensor");
                managed_shm = new boost::interprocess::managed_shared_memory(boost::interprocess::open_or_create,shm_name.c_str() , 65536); 
                shm_bt = managed_shm->find_or_construct<double>("button")[2]();
            }
        }
        else if (btn[1] == PRESSED && robot_status == ROBOT_RUNNING ) {
            cout << "killled" << endl;
            kill_robot();
            // destroy shm
            delete managed_shm;
            robot_status = ROBOT_STOPED;
            // open tty to get button data
            open_tty();
        }
        if (robot_status == ROBOT_RUNNING && log_enabled) {
            log_process();
        }
        if( robot_status == ROBOT_STOPED && get_tty_path()=="") {
            open_tty();
        }
        usleep(10000);
    }
    exit(EXIT_SUCCESS);
}
