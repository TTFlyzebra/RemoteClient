//
// Created by FlyZebra on 2021/9/30 0030.
//
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "ServerManager.h"

int32_t main(int32_t  argc,  char*  argv[])
{
    printf("main client is start.\n");
    ServerManager *manager = new ServerManager();
    delete manager;
}
