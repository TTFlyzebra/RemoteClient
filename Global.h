//
// Created by FlyZebra on 2021/9/24 0002.
//

#ifndef ANDROID_GLOBAL_H
#define ANDROID_GLOBAL_H

extern bool is_screenRotate;

struct Terminal{
	char tid[8];
	char name[256];
	char local_ip[4];
	char internet_ip[4];
};
extern Terminal mTerminal;


#endif //ANDROID_GLOBAL_H