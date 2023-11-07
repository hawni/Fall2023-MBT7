#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/utility/binary.hpp>
#include <linux/serial.h>
#include "dynamixel.h"
#include "nbpopen.hpp"
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h> //-- What is This Used for ?
#include <termios.h>
#include <iostream> //-- What is This Used for ?
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h> //-- What is This Used for ?
#include <assert.h> //-- What is This Used for ?
#include <cstring>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <math.h>
#include <string> //-- What is This Used for ?


typedef unsigned char uchar;
typedef double value_t;

using namespace std;
using namespace boost::interprocess;

#define  MAX_READ_BUF         	(1024)
#define  BAUD_RATE            	(1)
#define  BASE_DIR             	("MRL")
#define  BUTTON_ID		(200)
#define  BT_RD_TIMEOUT        	(0.01)
#define  PRESSED        	(1)

#define  BUTTON_LEFT    	(1)
#define  BUTTON_RIGHT   	(2)
#define  BUTTON_LEFT_RIGHT    	(3)

#define  ROBOT_RUNNING        	(1)
#define  ROBOT_STOPED         	(2)
#define  ERROR_RUN		(3)

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
    const char *path = "/dev/"; //-- Where is /dev/ ? and What is Used for and What Can We See Inside It ?
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

//-- What Does this Function Do ?
double get_time() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + 1E-6*t.tv_usec;
}

//-- What is Dynamixel ?
DynamixelPacket *parse_status_packet(const char *str) {
    DynamixelPacket *pkt;
    pkt->id = *(str+2);
    pkt->length = *(str+3);
    pkt->instruction = *(str+4);
    memcpy(pkt->parameter, str + 5, pkt->length - 2);
    pkt->checksum = *(str + pkt->length+3);
    return pkt;
}

//-- What Does this Function Do ?
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

//-- What Does this Function Do ?
int run_robot() {
    std::string code_dir("/home/robot/MRL/Player");
    int ret = chdir(code_dir.c_str());
    assert(ret==0);
    close(fd);
    cmd = new std::string[3];
    cmd[0] = "sudo -E lua run_dcm.lua"; //-- What is DCM ?
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
    system("sudo killall lua"); //-- What is Lua and Its Relation with C++ ?
    system("sudo rm -r /dev/shm/*"); //-- What is /dev/shm and What is Used for ?
    return ROBOT_STOPED;
}
