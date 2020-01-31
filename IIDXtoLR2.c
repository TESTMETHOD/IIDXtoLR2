#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <linux/input.h>
#include <linux/uinput.h>

int ddread(char *IIDXdevice, char *vkbd){
    int fd;
    char buffer[4096];
    char reading[32768];
    char *devicePtrS,*devicePtrE;
    ssize_t readsize = 0;
    long int fullsize = 0;

    fd = open("/proc/bus/input/devices",O_RDONLY);
    if(fd == -1){
        close(fd);
        return -1;
    }
    //printf("Open OK : %d\n",fd);

    do{
        readsize = read(fd, buffer, sizeof(buffer));    
        if (readsize > 0){
            for(int i = 0;i<readsize;i++){
                reading[fullsize+i] = buffer[i];
            }
            fullsize += readsize;
        }else{
            if(readsize < 0){
                close(fd);
                return -1;
            }
        }
    }while (readsize != 0);

    //printf("read size is %ld\n", fullsize);
    //printf("devices are \n%s", reading);

    devicePtrS = strstr(reading,"Handlers=kbd");
    devicePtrS = strstr(devicePtrS," ")+1;
    if (devicePtrS == NULL){
        printf("Cannot open vKBD device.");
        return -1;
    }
    devicePtrE = strstr(devicePtrS,"\n");
    if(devicePtrE != NULL){
        for (size_t i = 0; i < (devicePtrE - devicePtrS); i++){
            vkbd[i] = *(devicePtrS + i);
        }
    }

    devicePtrS = strstr(reading,"I: Bus=0003 Vendor=1ccf Product=1018 Version=0111");
    if(devicePtrS == NULL){
        printf("IIDXController Not Found.");
        return -1;
    }
    //printf("device is \n%s",devicePtrS);

    devicePtrS = strstr(devicePtrS,"Handlers");
    devicePtrS = strstr(devicePtrS,"=") + 1;
    devicePtrE = strstr(devicePtrS," ");
    if(devicePtrE != NULL){
        for (size_t i = 0; i < (devicePtrE - devicePtrS); i++){
            IIDXdevice[i] = *(devicePtrS + i);
        }
    }
    close(fd);
}


int fdread(char* dataset[2]){
    char* IIDXdevice = dataset[0];
    unsigned char* TTvalue = dataset[1];
    unsigned char data[256];
    ssize_t readsize = 0;
    //printf("IIDXdevice is : %s\n",IIDXdevice);
    //printf("%ud\n",TTvalue);

    char devicefile[11 + sizeof(IIDXdevice)];
    snprintf(devicefile, 11 + sizeof(IIDXdevice) ,"%s%s", "/dev/input/", IIDXdevice);
    //printf("%s\n",devicefile);

    int fd = open(devicefile,O_RDONLY);
    if(fd == -1){
        close(fd);
        return -1;
    }
    //printf("%d\n",fd);



    do{
        readsize = read(fd, data, sizeof(data));
        if(readsize == 48){
            *TTvalue = data[20];
            //printf("%x\n",*TTvalue);
        }
    }while (readsize != -1);

    close(fd);

    return 0;
}

void emit(int fd, int type, int code, int val){
    struct input_event ie;

   ie.type = type;
   ie.code = code;
   ie.value = val;
   /* timestamp values below are ignored */
   ie.time.tv_sec = 0;
   ie.time.tv_usec = 0;

   write(fd, &ie, sizeof(ie));
}

int kcsend(char* TTvalue){
    int vifd;
    unsigned char prevTTvalue=*TTvalue,presentTTvalue=*TTvalue;
    struct uinput_user_dev vkbd;

    enum{
        stop,
        forward,
        back
    }TTstate = stop;

    vifd=open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(vifd < 0){
        close(vifd);
        return -1;
    }

    ioctl(vifd, UI_SET_EVBIT, EV_KEY);
    ioctl(vifd, UI_SET_KEYBIT, KEY_LEFTSHIFT);
    ioctl(vifd, UI_SET_KEYBIT, KEY_LEFTCTRL);

    memset(&vkbd, 0, sizeof(vkbd));
    snprintf(vkbd.name, UINPUT_MAX_NAME_SIZE,"TTconvV-kbd");
    vkbd.id.bustype = BUS_USB;
    vkbd.id.vendor  = 0x1e21;
    vkbd.id.product = 0x0001;
    vkbd.id.version = 0;
    
    ioctl(vifd, UI_DEV_SETUP, &vkbd);
    ioctl(vifd, UI_DEV_CREATE);

    while (1){
        presentTTvalue = *TTvalue;
        //printf("%d\n",*TTvalue);
        if(prevTTvalue != presentTTvalue){
            if( (prevTTvalue < presentTTvalue && !(((prevTTvalue >> 4) == 0x0) && ((presentTTvalue >> 4) == 0xf)) )||((prevTTvalue >> 4 == 0xf) && (presentTTvalue >> 4 == 0x0))){
                switch(TTstate){
                    case back:
                        emit(vifd, EV_KEY, KEY_LEFTCTRL, 0);
                        emit(vifd, EV_SYN, SYN_REPORT, 0);
                    case stop:
                        emit(vifd, EV_KEY, KEY_LEFTSHIFT, 1);
                        emit(vifd, EV_SYN, SYN_REPORT, 0);
                        printf("forward\n");
                        TTstate = forward;
                        //nanosleep(&(struct timespec){0,10*1000*1000},NULL);
                        break;
                    default:
                        break;
                }
            }else if( (prevTTvalue > presentTTvalue && !(((prevTTvalue >> 4) == 0xf) && ((presentTTvalue >> 4) == 0x0)) )||((prevTTvalue >> 4 == 0x0) && (presentTTvalue >> 4 == 0xf))){
                switch(TTstate){
                    case forward:
                        emit(vifd, EV_KEY, KEY_LEFTSHIFT, 0);
                        emit(vifd, EV_SYN, SYN_REPORT, 0);
                    case stop:
                        emit(vifd, EV_KEY, KEY_LEFTCTRL, 1);
                        emit(vifd, EV_SYN, SYN_REPORT, 0);
                        printf("back\n");
                        TTstate = back;
                        //nanosleep(&(struct timespec){0,10*1000*1000},NULL);
                        break;
                    default:
                        break;
                }
            }
            prevTTvalue = presentTTvalue;
        }else{
            switch(TTstate){
                case back:
                    emit(vifd, EV_KEY, KEY_LEFTCTRL, 0);
                    emit(vifd, EV_SYN, SYN_REPORT, 0);
                    printf("stop\n");
                    TTstate = stop;
                    break;
                case forward:
                    emit(vifd, EV_KEY, KEY_LEFTSHIFT, 0);
                    emit(vifd, EV_SYN, SYN_REPORT, 0);
                    printf("stop\n");
                    TTstate = stop;
                    break;
                default:
                    break;
            }
        }
        //printf("%d\n",presentTTvalue);
        nanosleep(&(struct timespec){0,20*1000*1000},NULL);
    }
    ioctl(vifd, UI_DEV_DESTROY);
    close(vifd);
    return 0;
}


int main(void){
    char IIDXdevice[16], vkbd[16], TTvalue;
    int devicefd;

    for(int i = 0;i<sizeof(IIDXdevice);i++){
        IIDXdevice[i]=0;
    }

    for(int i = 0;i<sizeof(vkbd);i++){
        vkbd[i]=0;
    }
    
    if(ddread(IIDXdevice,vkbd) == -1){
        return 0;
    }

    pthread_t fdReadThread,kcSendThread;
    int fdReadret,kcReadret;

    //printf("vkbd is %s\n",vkbd);
    //printf("device is : %s\n",IIDXdevice);
    if(pthread_create(&fdReadThread, NULL, (void *)fdread,(void *)(char *[]){IIDXdevice,&TTvalue}) != 0){
        return -1;
    }

    if(pthread_create(&kcSendThread, NULL, (void *)kcsend,(void *)&TTvalue) != 0){
        return -1;
    }
    
    if(pthread_join(fdReadThread, (void *)&fdReadret) != 0){
        printf("%d",fdReadret);
        return -1;
    }

    if(pthread_join(kcSendThread, (void *)&kcReadret) != 0){
        printf("%d",kcReadret);
        return -1;
    }

    return 0;
}