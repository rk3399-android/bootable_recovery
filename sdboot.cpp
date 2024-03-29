/*************************************************************************
	> File Name: sdboot.cpp
	> Author: jkand.huang
	> Mail: jkand.huang@rock-chips.com
	> Created Time: Fri 20 Jan 2017 03:34:51 PM CST
 ************************************************************************/

#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mount.h> 
#include <cutils/properties.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "cutils/android_reboot.h"
#include "common.h"
#include "sdboot.h"
#include "roots.h"
#include "install.h"
#include "ui.h"
#include "screen_ui.h"
#include "rkupdate/Upgrade.h"

#include <fs_mgr.h>
using namespace std;
extern RecoveryUI* ui;
SDBoot::SDBoot(){
    status = INSTALL_ERROR;
    bootwhere();
    bUpdateModel = false;
}
bool SDBoot::isSDboot(){
    return bSDBoot;
}
bool SDBoot::isUSBboot(){
    return bUsbBoot;
}
bool SDBoot::parse_config(char *pConfig,VEC_SD_CONFIG &vecItem)
{     
    printf("in parse_config\n");
    std::stringstream configStream(pConfig);
    std::string strLine,strItemName,strItemValue;
    std::string::size_type line_size,pos;
    vecItem.clear();
    STRUCT_SD_CONFIG_ITEM item;
    while (!configStream.eof())
    { 
        getline(configStream,strLine);
        line_size = strLine.size();
        if (line_size==0)
        continue;
        if (strLine[line_size-1]=='\r')
        {
            strLine = strLine.substr(0,line_size-1);
        }
        printf("%s\n",strLine.c_str());
        pos = strLine.find("=");
        if (pos==std::string::npos)
        {
            continue;
        }
        if (strLine[0]=='#')
        {
            continue;
        }
        strItemName = strLine.substr(0,pos);
        strItemValue = strLine.substr(pos+1);
        strItemName.erase(0,strItemName.find_first_not_of(" "));
        strItemName.erase(strItemName.find_last_not_of(" ")+1);
        strItemValue.erase(0,strItemValue.find_first_not_of(" "));
        strItemValue.erase(strItemValue.find_last_not_of(" ")+1);
        if ((strItemName.size()>0)&&(strItemValue.size()>0))
        {
            item.strKey = strItemName;
            item.strValue = strItemValue;
            vecItem.push_back(item);
        }
    } 
    printf("out parse_config\n");
    return true;

}  
bool SDBoot::parse_config_file(const char *pConfigFile,VEC_SD_CONFIG &vecItem)
{         
    FILE *file=NULL;
    file = fopen(pConfigFile,"rb");
    if( !file ){
        return false;
    }    
    int iFileSize;
    fseek(file,0,SEEK_END);
    iFileSize = ftell(file);
    fseek(file,0,SEEK_SET);
    char *pConfigBuf=NULL;
    pConfigBuf = new char[iFileSize+1];
    if (!pConfigBuf)
    {    
        fclose(file);
        return false;
    }    
    memset(pConfigBuf,0,iFileSize+1);
    int iRead;
    iRead = fread(pConfigBuf,1,iFileSize,file);
    if (iRead!=iFileSize)
    {    
        fclose(file);
        delete []pConfigBuf;
        return false;
    }    
    fclose(file);
    bool bRet;
    bRet = parse_config(pConfigBuf,vecItem);
    delete []pConfigBuf;
    printf("out parse_config_file\n");
    return bRet;
}      
std::vector<std::string> SDBoot::get_sd_config(char *configFile, int argc, char **argv){
    std::vector<std::string> args(argv, argv + argc);
    char arg[64];
    VEC_SD_CONFIG vecItem;
    unsigned long i;
    if (!parse_config_file(configFile,vecItem))
    {
        printf("out get_args_from_sd:parse_config_file\n");
        return args;
    }

    for (i=0;i<vecItem.size();i++)
    {
        if ((strcmp(vecItem[i].strKey.c_str(),"pcba_test")==0)||
            (strcmp(vecItem[i].strKey.c_str(),"fw_update")==0)||
            (strcmp(vecItem[i].strKey.c_str(),"demo_copy")==0)||
            (strcmp(vecItem[i].strKey.c_str(),"volume_label")==0))
        {
            if (strcmp(vecItem[i].strValue.c_str(),"0")!=0)
            {
                sprintf(arg,"--%s=%s",vecItem[i].strKey.c_str(),vecItem[i].strValue.c_str());
                printf("%s\n",arg);
                args.push_back(arg);
            }
        }
    }
    printf("out get_args_from_sd\n");
    return args;
}
std::vector<std::string> SDBoot::get_args_from_sd(int argc, char **argv){
    printf("in get_args_from_sd\n");
    std::vector<std::string> args;
    ensure_sd_mounted();
    if (!bSDMounted)
    {
        printf("out get_args_from_sd:bSDMounted\n");
        return args;
    }
    char configFile[64];
    strcpy(configFile, EX_SDCARD_ROOT);
    strcat(configFile, "/sd_boot_config.config");
    args = get_sd_config(configFile, argc, argv);
    
    return args;
}
std::vector<std::string> SDBoot::get_args_from_usb(int argc, char **argv){
    printf("in get_args_from_usb\n");
    std::vector<std::string> args;
    ensure_usb_mounted(); 
    if (!bUsbMounted) 
    {
        printf("out get_args_from_usb:bUsbMounted=false\n");
        return args;
    }

    char configFile[64];
    strcpy(configFile, USB_ROOT);
    strcat(configFile, "/sd_boot_config.config");  
    args =  get_sd_config(configFile, argc, argv);

    return args;
}

std::vector<std::string> SDBoot::get_args(int argc, char **argv){
    std::vector<std::string> args;
    if(isSDboot()){
        args = get_args_from_sd(argc, argv);
    }else if(isUSBboot()){
        args = get_args_from_usb(argc, argv);
    }else{
        printf("ERROR: get_args failed.(%s:%d)\n", __func__, __LINE__);
    }
    return args;
}

void SDBoot::bootwhere(){
    bSDBoot = false;
    char param[1024];
    int fd, ret; 
    char *s=NULL;
    printf("read cmdline\n");
    memset(param,0,1024);
    fd= open("/proc/cmdline", O_RDONLY);
    ret = read(fd, (char*)param, 1024);

    s = strstr(param,"sdfwupdate");
    if(s != NULL){
        bSDBoot = true;
    }else{ 
        bSDBoot = false;
    }

    s = strstr(param, "usbfwupdate");
    if(s != NULL){
        bUsbBoot = true;
    }else{
        bUsbBoot = false;
    }

    close(fd);
}
void SDBoot::ensure_sd_mounted(){
    int i;
    for(i = 0; i < 3; i++) {
        if(0 == ensure_path_mounted(EX_SDCARD_ROOT)){
            bSDMounted = true;
            break;
        }else {
            printf("delay 1sec\n");
            sleep(1);
        } 
    }    
}

void SDBoot::ensure_usb_mounted(){
    int i;
    for(i = 0; i < 10; i++) {
        if(0 == mount_usb_device()){
            bUsbMounted = true;
            break;
        }else {
            printf("delay 1sec\n");
            sleep(1);
        }
    } 
}


int SDBoot::mount_usb_device()
{
    char configFile[64];
    char usbDevice[64];
    int result;
    DIR* d=NULL;
    struct dirent* de;
    d = opendir(USB_ROOT);
    if (d)
    {//check whether usb_root has  mounted
     strcpy(configFile, USB_ROOT);
     strcat(configFile, "/sd_boot_config.config");
     if (access(configFile,F_OK)==0)
     {
         closedir(d);
         return 0;
     }
     closedir(d);
    }  
    else
    {  
        if (errno==ENOENT)
        {
            if (mkdir(USB_ROOT,0755)!=0)
            {
                printf("failed to create %s dir,err=%s!\n",USB_ROOT,strerror(errno));
                return -1;
            }
        }
        else
        {   
            printf("failed to open %s dir,err=%s\n!",USB_ROOT,strerror(errno));
            return -1;
        }   
    }       

    d = opendir("/dev/block");
    if(d != NULL) {
        while((de = readdir(d))) {
            printf("/dev/block/%s\n", de->d_name);
            if((strncmp(de->d_name, "sd", 2) == 0) &&(isxdigit(de->d_name[strlen(de->d_name)-1])!=0)){
                memset(usbDevice, 0, sizeof(usbDevice));
                sprintf(usbDevice, "/dev/block/%s", de->d_name);
                printf("try to mount usb device %s by vfat", usbDevice);
                result = mount(usbDevice, USB_ROOT, "vfat",
                               MS_NOATIME | MS_NODEV | MS_NODIRATIME, "shortname=mixed,utf8");
                if(result != 0) {
                    printf("try to mount usb device %s by ntfs\n", usbDevice);
                    result = mount(usbDevice, USB_ROOT, "ntfs",
                                   MS_NOATIME | MS_NODEV | MS_NODIRATIME, "");
                }

                if(result == 0) {
                    //strcpy(USB_DEVICE_PATH,usbDevice);
                    USB_DEVICE_PATH = usbDevice;
                    closedir(d);
                    return 0;
                }
            }
        }
        closedir(d); 
    } 

             return -2;
}

void handle_upgrade_callback(char *szPrompt)
{  
    if (ui){
        //ui->Print(szPrompt);
        ui->Print("\n");
    }  
}  
void handle_upgrade_progress_callback(float portion, float seconds)
{  
    if (ui){
        if (seconds==0)
        ui->SetProgress(portion);
        else 
        ui->ShowProgress(portion,seconds);
    }  
}  

int SDBoot::do_sd_mode_update(const char *pFile){
    status=INSTALL_SUCCESS;
    bool bRet,bUpdateIDBlock=true;
    char *pFwPath = (char *)malloc(100);
    strcpy(pFwPath, EX_SDCARD_ROOT);
    if (strcmp(pFile,"1")==0)
    {
        strcat(pFwPath, "/sdupdate.img");
    }
    else if (strcmp(pFile,"2")==0)
    {
        strcat(pFwPath, "/sdupdate.img");
        bUpdateIDBlock = false;
    }
    else
    {
        strcat(pFwPath, pFile);
    }

    ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
    ui->SetProgressType(RecoveryUI::DETERMINATE);
    printf("start sd upgrade...\n");

    #ifdef USE_BOARD_ID
    ensure_path_mounted("/cust");
    ensure_path_mounted("/system");
    restore();
    #endif
    if (bUpdateIDBlock)
    bRet= do_rk_firmware_upgrade(pFwPath,(void *)handle_upgrade_callback,(void *)handle_upgrade_progress_callback);
    else
    bRet = do_rk_partition_upgrade(pFwPath,(void *)handle_upgrade_callback,(void *)handle_upgrade_progress_callback);
    ui->SetProgressType(RecoveryUI::EMPTY);
    if (!bRet)
    {
        status = INSTALL_ERROR;
        ui->Print("SD upgrade failed!\n");
    }
    else
    {
        #ifdef USE_BOARD_ID
        ensure_path_mounted("/cust");
        ensure_path_mounted("/system");
        custom();
        #endif
        status = INSTALL_SUCCESS;
        //bAutoUpdateComplete=true;
        printf("SD upgrade ok.\n");
    }

    return status;
}

int SDBoot::do_usb_mode_update(const char *pFile){
    return status;
}
int SDBoot::do_rk_mode_update(const char *pFile){
    bUpdateModel = true;
    if (bSDBoot){
        printf("SDBoot optarg=%s\n", optarg);
        status = do_sd_mode_update(pFile);
    }else if(bUsbBoot){
        printf("UsbBoot optarg=%s\n", optarg);
        status = do_usb_mode_update(pFile);
    } 
    return status;
}

int SDBoot::do_rk_direct_sd_update(const char *pFile){
    bUpdateModel = true;
	printf("enter do_rk_direct_sd_update!\n");
    status = do_sd_mode_update(pFile);
    return status;
}
bool SDBoot::do_direct_parse_config_file(const char * pConfigFile,VEC_SD_CONFIG & vecItem){
	printf("enter do_direct_parse_config_file!\n");
	return parse_config_file(pConfigFile, vecItem);
}

int SDBoot::do_rk_factory_mode(){
    //pcba_test
    printf("enter pcba test!\n");

    status = INSTALL_SUCCESS;
    const char *args[2];
    args[0] = "/sbin/pcba_core";
    args[1] = NULL;

    pid_t child = fork();
    if (child == 0) {
        execv(args[0], (char* const*)args);
        fprintf(stderr, "run_program: execv failed: %s\n", strerror(errno));
        status = INSTALL_ERROR;
        //pcbaTestPass = false;
    }
    int child_status;
    waitpid(child, &child_status, 0);
    if (WIFEXITED(child_status)) {
        if (WEXITSTATUS(child_status) != 0) {
            printf("pcba test error coder is %d \n", WEXITSTATUS(child_status));
            status = INSTALL_ERROR;
            //pcbaTestPass = false;
        }
    } else if (WIFSIGNALED(child_status)) {
        printf("run_program: child terminated by signal %d\n", WTERMSIG(child_status));
        status = INSTALL_ERROR;
        //pcbaTestPass = false;
    }
    return status;
}
void SDBoot::check_device_remove(){
    if (bSDBoot){
        ui->ShowText(true);
        if (status==INSTALL_SUCCESS)
            ui->Print("Doing Actions succeeded.please remove the sdcard......\n");
        else 
            ui->Print("Doing Actions failed!please remove the sdcard......\n");
        if (bSDMounted)
            checkSDRemoved();

    } else if (bUsbBoot) {
        ui->ShowText(true);
        if (status==INSTALL_SUCCESS)
            ui->Print("Doing Actions succeeded.please remove the usb disk......\n");
        else 
            ui->Print("Doing Actions failed!please remove the usb disk......\n");
        if (bUsbMounted)
            checkUSBRemoved();
    }

    if(bUpdateModel){
        ui->Print("reboot ...\n");
        sync();
        property_set(ANDROID_RB_PROPERTY, "reboot,");
    }
}
void SDBoot::checkSDRemoved() {
    /*	
    Volume* v = volume_for_path(EX_SDCARD_ROOT);
    char *temp;
    char *sec_dev = v->fs_options;
    if(sec_dev != NULL) {
        temp = strchr(sec_dev, ',');
        if(temp) {
            temp[0] = '\0';
        }
    } */ 

    while(1) {
        //int value2 = -1;
        int value = access(getenv(SD_POINT_NAME), 0);
        //if(sec_dev) {
        //  value2 = access(sec_dev, 0);
        //}
        //if(value == -1 && value2 == -1) {
        if(value == -1) {
            printf("remove sdcard\n");
            break;
        }else {
            sleep(1);
        }
    }
}

void SDBoot::checkUSBRemoved() {
    int ret;

    while(1) {
        ret = access(USB_DEVICE_PATH.c_str(), F_OK);
        if(ret==-1) {
            printf("remove USB\n");
            break;
        }else {
            sleep(1);
        }
    }
}
