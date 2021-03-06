
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>

#include "sqm_port.h"
#include "sqm_types.h"
#include "sys_basic_macro.h"
#include "AppSetting.h"
#include "SysSetting.h"
#include "app_sys.h"

#include "mid_sys.h"
#include "log_sqm_port.h"
#include "mid/mid_mutex.h"
#include "LogModule.h"
#include "NetworkFunctions.h"
#include "config/pathConfig.h"
#include "mid/mid_tools.h"
#include "customer.h"
#include "cutils/properties.h"

#define STB_PACK_CACHE_NUM 1024
#define IND_STRNCPY(dest, src, n)	strncpy(dest, src, n)
#define IND_MEMCPY(dest,src, n)	memcpy(dest,src, n)
#define IND_STRCPY(dest, src)		strcpy(dest, src)

static int sqmpro_flag = 0;
static int g_mdiMLR = 0;
static int g_mdiDF = 0;
static int  g_jitter = 0;
static struct sqmPostData g_sqm = {0};
typedef struct sqm_data_cache {
	unsigned int pkg_count;
	unsigned int pkg_count_max;
	unsigned int base;

	unsigned int head_num;
	unsigned int tail_num;
	unsigned int data_len;
	STB_TS_PACK stb_pack[STB_PACK_CACHE_NUM];
}SQM_DATA_CACHE;


//status record
static SQM_STATUS sqm_port_stat;

static CHN_STAT g_chn_stat = STB_IDLE;// channel statu
static int g_chn_ip;          // channel ip address
static int g_chn_port;        // channel port
static int g_local_chn_port;  // local stream port
static char g_chn_url[MAX_SQM_URL_STR_LEN];  // channel url

/* Eagle add. 2011年01月19日*/
static unsigned int g_listen_port = 37001;      /* default */
static unsigned int g_server_port = 37000;      /* default */

/*纠错状态从纠错后切换到纠错前，最大的判断值*/
static int pushdata_sockfd = -1;
static struct sockaddr_un servaddr = {0};
static struct sockaddr_un cliaddr = {0};
static SQM_DATA_CACHE g_pack_cache;
//C26 SQM  需要的参数.
static char root_path[20] = {"/data/sqm.ini"};
static char var_path[20] = { "/var/sqm.ini" };
static int pushmsg_sockfd = -1;
static SQM_DATA_CACHE g_msg_cache;
static struct sockaddr_un msg_servaddr = {0};
static struct sockaddr_un msg_cliaddr = {0};
//C28 SQM
static int get_sqm_data_type = 0;


static void sqm_port_init();
static void sqm_port_start();
static void sqm_port_set_info();
static void sqm_port_stop();
static void sqm_cache_save(STB_TS_PACK *stb_pack);
static int sqm_cache_load_begin(STB_TS_PACK *stb_pack);
static void sqm_cache_load_end(void);
static void sqm_cache_clear(void);
static int sqm_port_pushmsg(int pIndex, SQM_MSG_C26 *sqm_msg_c26, int len, SQM_MSG sqm_msg);

extern "C" {
char* Hybroad_getHWtype();
int getModuleLevel(const char* name);
void mid_sys_serial(char *buf);
int mid_tool_checkURL(const char* url, char *p_addr, int *p_port);
#ifndef NEW_ANDROID_SETTING
char* IPTVMiddleware_SettingGetStr(const char* name, char* value, int len);
#endif
};

SQM_STATUS sqm_port_getStatus(void)
{
	return sqm_port_stat;
}

static char MqmcIP[128] = {0};
static void sqm_port_start()
{
    if (sqm_port_stat == SQM_START_OK) {
        LogUserOperDebug("sqm already started\n");
        return;
    }
    LogUserOperDebug("sqm_port_start\n");
    if (!strlen(MqmcIP)) {
        LogUserOperError("No mqmcIp found, try to find in config file\n");
        sysSettingGetString("mqmcIp", MqmcIP, sizeof(MqmcIP), 0);
        if(!strlen(MqmcIP)) {
            LogUserOperError("Error: No mqmcIp\n");
            return;
        }
    }
    property_set("ctl.start", "sqmloader");
    LogUserOperDebug("after sqm_port_start\n");
    sqm_port_stat = SQM_START_OK;

}

void sqm_port_mqmip_set( const char *ipaddr)
{
	int tmplen;
    LogUserOperError("sqm_port_mqmip_set[%s]\n", ipaddr);  //debug only
	tmplen = strlen(ipaddr);
	if(tmplen > 15){             // ipaddr format: xxx.xxx.xxx.xxx
		LogUserOperError("The ip address is too long, len = %d\n", tmplen);
		return;
	}else{
		memset(MqmcIP, 0, sizeof(MqmcIP));
		strcpy(MqmcIP, ipaddr);
	}
}

// parse and save channel info
// when the pointer is NULL, this function will do nothing to the acordding para;
void parseChnInfo(CHN_STAT pStat, struct sockaddr_in* serv_sin, struct sockaddr_in* data_sin,char* url)
{
	//char tmpbuf[MAX_SQM_URL_STR_LEN];
	char *pstr;
	int tmplen;

//	mid_mutex_lock(g_sqm_mutex);
	g_chn_stat = pStat;

	if(serv_sin != NULL){  // single
		g_chn_ip = ntohl(serv_sin->sin_addr.s_addr);
		g_chn_port = ntohs(serv_sin->sin_port);
		if(data_sin != NULL){
			g_local_chn_port = ntohs(data_sin->sin_port);
		}else{
			LogUserOperDebug("error to get local port in single mode\n");
		}
	}else{    // multi
		if(data_sin != NULL){
			g_chn_ip = ntohl(data_sin->sin_addr.s_addr);
			g_chn_port = ntohs(data_sin->sin_port);
			g_local_chn_port = ntohs(data_sin->sin_port);
		}
	}

	if(url != NULL){
		memset(g_chn_url, 0, MAX_SQM_URL_STR_LEN);      // clear url array
		if(serv_sin != NULL){    // unicast
			pstr = strchr(url, '?');         // drop the parameters in url
			if(pstr == NULL){
				LogUserOperDebug("Illegal ip string, can NOT find the seperater ? in channel url\n");
//				mid_mutex_unlock(g_sqm_mutex);
				return;
			}
			tmplen = pstr - url;
		}else{                // multicast
			tmplen = strlen(url);
		}
		if(tmplen > MAX_SQM_URL_STR_LEN){
			LogUserOperDebug("The channel url is too long! len=%d, bigger than %d\n", tmplen, MAX_SQM_URL_STR_LEN);
		}else{
			memcpy(g_chn_url, url, tmplen);
		}
	}
//	mid_mutex_unlock(g_sqm_mutex);
}

// write msg to fifo
int sqm_port_msg_write(SQM_MSG msg)
{
    LogUserOperDebug("sqm_port_msg_write [%d]\n", msg);
    if (msg == MSG_START) {
        sqm_port_start();
    }
    else if (msg == MSG_STOP) {
        sqm_port_stop();
    }
    else {
        LogUserOperError("unknow msg\n");
    }
	return 0;
}

static void sqm_port_stop()
{
    LogUserOperDebug("sqm_port_stop\n");
    if(sqm_port_stat == SQM_STOP_OK){
    	LogUserOperDebug("ERROR: sqm already stop, now stat is %d\n", sqm_port_stat);
             return;
    }
    //system("killall -9 sqmpro");
    char sqmproPidFile[] = {"/var/sqmpro_pid.lock"};
    if (!access(sqmproPidFile, R_OK)) {
        FILE* fp = fopen(sqmproPidFile, "r");
        if (!fp) {
            LogUserOperError("open sqmproPidFile error\n");
            return;
        }
        char buf[256] = {0};
        fgets(buf, sizeof(buf), fp);
        fclose(fp);
        int pid = atoi(buf);
        LogUserOperDebug("kill pid [%d]\n", pid);
        kill(pid ,SIGABRT);
    }

    sqm_port_stat = SQM_STOP_OK;
}

// prepare for sqm porting module
void sqm_port_prepare(void)
{
    sqm_port_stat = SQM_READY;
}

/* Eagle. add. 2011骞?1鏈?9鏃?*/
unsigned int sqm_get_listen_port ( void )
{
    return g_listen_port;
}
/* -----  end of function sqm_get_listen_port  ----- */

unsigned int sqm_get_server_port ( void )
{
    return g_server_port;
}
/* -----  end of function sqm_get_server_port  ----- */

void sqm_set_listen_port ( unsigned int port )
{
    if ( 0 != port )
    {
        g_listen_port = port;
    }
    return ;
}
/* -----  end of function sqm_set_listen_port  ----- */

void sqm_set_server_port ( unsigned int port )
{
    if ( 0 != port )
    {
        g_server_port = port;
    }
    return ;
}
/* -----  end of function sqm_set_server_port  ----- */

int sqm_port_pushdata_open(void)
{
	int ret = 0;
//	struct timeval timeout = {0,500};
	int buf_size = MAX_BUFF_SIZE;

	LogUserOperDebug("OPEN_PUSHDATA_SOCKET\n");
	if(pushdata_sockfd > 0){
		close(pushdata_sockfd);
	}

	pushdata_sockfd = -1;
	memset(&servaddr, 0, sizeof(servaddr));
	memset(&cliaddr, 0, sizeof(cliaddr));
	pushdata_sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if(pushdata_sockfd < 0){
		LogUserOperError("create pushdata_sockfd error!\n");
		return -1;
	}
	if (-1 == fcntl(pushdata_sockfd, F_SETFL, O_NONBLOCK)){
        LogUserOperError("set pushdata_sockfd nonblock error!\n");

		close(pushdata_sockfd);
		pushdata_sockfd = -1;

		return -1;
	}
	//设置socket发送缓存,buf_size 取probe_external_api.h中的MAX_BUFF_SIZE.
    if (-1 == setsockopt(pushdata_sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size))){
    	LogUserOperError("set pushdata_sockfd buffer size error!\n");

		close(pushdata_sockfd);
		pushdata_sockfd = -1;

		return -1;
    }
//	ret = setsockopt(pushdata_sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(struct timeval));
//	LogUserOperDebug("setsockopt=%d\n", ret);
	ret = unlink(SQM_SOCKET_TX);
	LogUserOperDebug("unlink=%d, errno = %d[%s]\n", ret, errno, strerror(errno));
    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sun_family = AF_LOCAL;
    strcpy(cliaddr.sun_path, SQM_SOCKET_TX);	/* bind an address for us */

	if(-1 == bind(pushdata_sockfd, (struct sockaddr *) &cliaddr, sizeof(cliaddr)))
	{
		LogUserOperError("bind pushdata_sockfd error!, errno = %d[%s]\n", errno, strerror(errno));

		close(pushdata_sockfd);
		pushdata_sockfd = -1;
		unlink(SQM_SOCKET_TX);

		return -1;
	}

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sun_family = AF_LOCAL;
    strcpy(servaddr.sun_path, SQM_SOCKET_RX); 	/* fill in server's address */

	sqm_port_pushdata_clear();

	return pushdata_sockfd;
}

int sqm_port_pushdata(int pIndex, char *buf, int len, int scale)
{
	//指数采样发送数据包
	SQM_DATA_CACHE *pack_cache = &g_pack_cache;

	STB_TS_PACK stb_pack;
	int send_len = 0;
	int ret = 0, err = 0;

	if(pushdata_sockfd <= 0){
		ERR_OUT("[socket error!]\n");
	}

	if(NULL == buf || len > MAX_PKT_SIZE || len < 188){
		ERR_OUT("[data error!], len = %d\n", len);
	}

	memset(&stb_pack, 0, sizeof(stb_pack));
	memcpy(stb_pack.pack, buf, len);

	stb_pack.total_len = len;
	stb_pack.seq_num = scale;
	gettimeofday(&stb_pack.tv, NULL);
	send_len = len + 16;

	//指数退避检查纠错状态切换
	if(pack_cache->base) {
		if(pack_cache->base > STB_PACK_CACHE_NUM) {
			pack_cache->base = STB_PACK_CACHE_NUM;
			pack_cache->pkg_count = pack_cache->pkg_count_max;
		}
		pack_cache->base--;

		sqm_cache_save(&stb_pack);
		//LogUserOperDebug("save packet\n");
	} else if(pack_cache->data_len > 0) {
		sqm_cache_save(&stb_pack);
		while(pack_cache->data_len > 0) {
			send_len = sqm_cache_load_begin(&stb_pack) + 16;
			ret = sendto(pushdata_sockfd, &stb_pack, send_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
			if(ret != send_len) {
				//LogUserOperDebug("re-send data failed, ret = %d, seq = %d, errno = [%d]%s\n", ret, stb_pack.seq_num, errno, strerror(errno));
				break;
			} else {
				sqm_cache_load_end();
			}
		}
		if(pack_cache->data_len > 0) {
			;
		} else {
			pack_cache->pkg_count = 0;
			pack_cache->base = 0;
		}
	} else {
		ret = sendto(pushdata_sockfd, &stb_pack, send_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
		if(ret != send_len) {
			if(ret == -1) {
				err = errno;

				pack_cache->pkg_count_max = pack_cache->pkg_count;
				pack_cache->pkg_count++;
				pack_cache->base = 1<<pack_cache->pkg_count;

				if(err == 2 || err == 11) {
					sqm_cache_save(&stb_pack);
					//LogUserOperDebug("send data failed, ret = %d, seq = %d, errno = [%d]%s\n", ret, stb_pack.seq_num, errno, strerror(errno));
				} else {
					//LogUserOperDebug("send data failed, ret = %d, seq = %d, errno = [%d]%s\n", ret, stb_pack.seq_num, errno, strerror(errno));
				}
			} else {
				//LogUserOperDebug("send data failed, ret = %d, seq = %d, errno = [%d]%s\n", ret, stb_pack.seq_num, errno, strerror(errno));
			}
		} else {
			//LogUserOperDebug("send data failed, ret = %d, seq = %d, errno = [%d]%s\n", ret, stb_pack.seq_num, errno, strerror(errno));
		}
	}
	return ret;

Err:
	return -1;
}

int sqm_port_pushdata_close(void)
{
	int ret = 0;

	if(pushdata_sockfd > 0){
		close(pushdata_sockfd);
	}

	pushdata_sockfd = -1;

	ret = unlink(SQM_SOCKET_TX);

	LogUserOperDebug("unlink=%d\n", ret);

	return ret;
}

void sqm_port_pushdata_clear(void)
{
	SQM_DATA_CACHE *pack_cache = &g_pack_cache;

	memset(pack_cache, 0, sizeof(SQM_DATA_CACHE));
}

static void sqm_cache_clear(void)
{
	SQM_DATA_CACHE *pack_cache = &g_pack_cache;

	pack_cache->head_num = 0;
	pack_cache->tail_num = 0;
	pack_cache->data_len = 0;
	memset(&(pack_cache->stb_pack), 0, sizeof(STB_TS_PACK) * STB_PACK_CACHE_NUM);
}

static void sqm_cache_save(STB_TS_PACK *stb_pack)
{
	SQM_DATA_CACHE *pack_cache = &g_pack_cache;
	unsigned int num = 0, len = 0;

	len = pack_cache->data_len;

	if(len >= STB_PACK_CACHE_NUM || len == 0) {
		sqm_cache_clear();
	}

	num = pack_cache->tail_num;

	memset(&(pack_cache->stb_pack[num]), 0, sizeof(STB_TS_PACK));
	memcpy(pack_cache->stb_pack[num].pack, stb_pack->pack, stb_pack->total_len);
	memcpy(&(pack_cache->stb_pack[num].tv), &(stb_pack->tv), sizeof(struct timeval));
	pack_cache->stb_pack[num].total_len = stb_pack->total_len;
	pack_cache->stb_pack[num].seq_num = stb_pack->seq_num;

	if(pack_cache->tail_num >= STB_PACK_CACHE_NUM - 1)
		pack_cache->tail_num = 0;
	else
		pack_cache->tail_num++;

	pack_cache->data_len++;
}

static int sqm_cache_load_begin(STB_TS_PACK *stb_pack)
{
	SQM_DATA_CACHE *pack_cache = &g_pack_cache;
	int num = 0;

	num = pack_cache->head_num;

	memset(stb_pack, 0, sizeof(STB_TS_PACK));
	memcpy(stb_pack->pack, pack_cache->stb_pack[num].pack, pack_cache->stb_pack[num].total_len);
	memcpy(&(stb_pack->tv), &(pack_cache->stb_pack[num].tv), sizeof(struct timeval));
	stb_pack->total_len = pack_cache->stb_pack[num].total_len;
	stb_pack->seq_num = pack_cache->stb_pack[num].seq_num;

	return stb_pack->total_len;
}

static void sqm_cache_load_end(void)
{
	SQM_DATA_CACHE *pack_cache = &g_pack_cache;

	if(pack_cache->head_num >= STB_PACK_CACHE_NUM - 1)
		pack_cache->head_num = 0;
	else
		pack_cache->head_num++;

	if(pack_cache->data_len > 0)
		pack_cache->data_len--;
	else
		pack_cache->data_len = 0;
}

//读取root下的sqm.ini信息,保存到var下, 并启动sqoloader.
int  sqm_port_sqmloader_start(void)
{
    LogUserOperError("sqm_port_sqmloader_start track\n");
    if (SQM_START_OK == sqm_port_stat) {
        LogUserOperDebug("ERROR: sqm already start, now stat is %d, first stop sqm\n", sqm_port_stat);
        sqm_port_msg_write(MSG_STOP);
    }
    property_set("ctl.start", "sqm_control");
    system("rm  /var/sqm.ini");
    sqm_info_copy(var_path);
    sqm_port_msg_write(MSG_START);

    return 0;
}

int  sqm_create_msg(SQM_MSG sqm_msg, SQM_MSG_C26 *sqm_msg_c26 )
{
	//char var[MAX_MSG_SIZE] = {0}, server_ip[32], stb_ip[32];
	int tmplen;

	switch(sqm_msg) {
		case MSG_PLAY:
			tmplen = strlen(g_chn_url);
			if(tmplen > MAX_SQM_URL_STR_LEN){
				LogUserOperDebug("ERROR: wrong channel url! len = %d\n", tmplen);
				return -1;
			}

			if (g_chn_stat == UNICAST_CHANNEL_TYPE || g_chn_stat == MULTICAST_CHANNEL_TYPE){

				sqm_msg_c26->MediaType = g_chn_stat;
				sqm_msg_c26->ChannelIp = g_chn_ip;
				sqm_msg_c26->ChannelPort = g_chn_port;
				sqm_msg_c26->StbPort = g_local_chn_port;
				strcpy(sqm_msg_c26->ChannelURL, g_chn_url);

				LogUserOperDebug( "IP:%d.%d.%d.%d@@PORT:%d",  (g_chn_ip >> 24) & 0xff, (g_chn_ip >> 16) & 0xff,(g_chn_ip >> 8) & 0xff,g_chn_ip & 0xff,g_chn_ip);
				LogUserOperDebug( "%d-%d-%d-%d-%s \n",sqm_msg_c26->MediaType,sqm_msg_c26->ChannelIp,sqm_msg_c26->ChannelPort,sqm_msg_c26->StbPort,sqm_msg_c26->ChannelURL);
			}
			break;
		case MSG_PAUSE:
			sqm_msg_c26->MediaType = g_chn_stat;
			break;
		case MSG_FAST:
			sqm_msg_c26->MediaType = g_chn_stat;
			break;
		case MSG_STOP:
			sqm_msg_c26->MediaType = g_chn_stat;
			break;
		case MSG_LOG_LEVEL:
			sqm_msg_c26->MediaType =  getModuleLevel("cuser");
		case MSG_GET_DATA:									//  1:SecData ????
			sqm_msg_c26->MediaType =  get_sqm_data_type;	// 0:StatData ????
			break;
		default:
			break;
		}

		return 0;
}

//  绑定消息类型
int sqm_port_buildmsg(SQM_MSG sqm_msg)
{
	SQM_MSG_C26 sqm_msg_c26={0};

	switch(sqm_msg) {
		case MSG_PLAY:
			sqm_create_msg(sqm_msg,&sqm_msg_c26);
			break;
		case MSG_PAUSE:
			sqm_create_msg(sqm_msg,&sqm_msg_c26);
			break;
		case MSG_FAST:
			sqm_create_msg(sqm_msg,&sqm_msg_c26);
			break;
		case MSG_STOP:
			sqm_create_msg(sqm_msg,&sqm_msg_c26);
			break;
		case MSG_LOG_LEVEL:
			sqm_create_msg(sqm_msg,&sqm_msg_c26);
			break;
		case MSG_GET_DATA:
			sqm_create_msg(sqm_msg,&sqm_msg_c26);
			break;
		default:
			break;
	}

	int ret = sqm_port_pushmsg(0, &sqm_msg_c26, sizeof(sqm_msg_c26), sqm_msg);

	LogUserOperDebug("send to sqmpro result:%d\n", ret);
	return ret;
}

int sqm_port_pushmsg_open(void)
{
	int ret = 0;
	int buf_size = MAX_BUFF_SIZE;

	LogUserOperDebug("sqm_port_pushmsg_open\n");
	if(pushmsg_sockfd > 0){
		close(pushmsg_sockfd);
	}

	pushmsg_sockfd = -1;
	memset(&msg_servaddr, 0, sizeof(msg_servaddr));
	memset(&msg_cliaddr, 0, sizeof(msg_cliaddr));
	pushmsg_sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if(pushmsg_sockfd < 0){
		LogUserOperError("create pushdata_sockfd error!\n");
		return -1;
	}
	if (-1 == fcntl(pushmsg_sockfd, F_SETFL, O_NONBLOCK)){
        LogUserOperError("set pushdata_sockfd nonblock error!\n");

		close(pushmsg_sockfd);
		pushmsg_sockfd = -1;

		return -1;
	}
	//设置socket发送缓存,buf_size 取probe_external_api.h中的MAX_BUFF_SIZE.
    if (-1 == setsockopt(pushmsg_sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size))){
    	LogUserOperError("set pushdata_sockfd buffer size error!\n");

		close(pushmsg_sockfd);
		pushmsg_sockfd = -1;

		return -1;
    }
//	ret = setsockopt(pushmsg_sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(struct timeval));
//	LogUserOperDebug("setsockopt=%d\n", ret);
	ret = unlink(SQM_SIG_TX);
	LogUserOperError("unlink=%d, errno = %d[%s]\n", ret, errno, strerror(errno));
       bzero(&msg_cliaddr, sizeof(msg_cliaddr));
       msg_cliaddr.sun_family = AF_LOCAL;
       strcpy(msg_cliaddr.sun_path, SQM_SIG_TX);	/* bind an address for us */

	if(-1 == bind(pushmsg_sockfd, (struct sockaddr *) &msg_cliaddr, sizeof(msg_cliaddr)))
	{
		LogUserOperError("bind pushmsg_sockfd error!, errno = %d[%s]\n", errno, strerror(errno));

		close(pushmsg_sockfd);
		pushmsg_sockfd = -1;
		unlink(SQM_SIG_TX);

		return -1;
	}

    bzero(&msg_servaddr, sizeof(msg_servaddr));
    msg_servaddr.sun_family = AF_LOCAL;
    strcpy(msg_servaddr.sun_path, SQM_SIG_RX); 	/* fill in server's address */

	//sqm_port_pushdmsg_clear();

	return pushmsg_sockfd;
}

//int sqm_port_pushmsg(int pIndex, char *buf, int len, SQM_MSG sqm_msg)
static int sqm_port_pushmsg(int pIndex, SQM_MSG_C26 *sqm_msg_c26, int len, SQM_MSG sqm_msg)
{
	//printf("============sqm_port_pushmsg  buf =%s, len =%d, ========\n", buf, len);

    STB_SQM_MSG stb_sqm_msg = {0};
	int send_len = 0;
	int ret = 0;

	if(pushmsg_sockfd <= 0){
		ERR_OUT("[socket error!]\n");
	}

	if(NULL == sqm_msg_c26 || len > MAX_PKT_SIZE ){
		ERR_OUT("[data error!], len = %d\n", len);
	}

	memset(&stb_sqm_msg, 0, sizeof(stb_sqm_msg));
	stb_sqm_msg.Var.MediaType = sqm_msg_c26->MediaType;
	stb_sqm_msg.Var.ChannelIp = sqm_msg_c26->ChannelIp;
	stb_sqm_msg.Var.ChannelPort = sqm_msg_c26->ChannelPort;
	stb_sqm_msg.Var.StbPort = sqm_msg_c26->StbPort;
	strcpy(stb_sqm_msg.Var.ChannelURL, sqm_msg_c26->ChannelURL);

	if (sqm_msg == MSG_STOP)
		stb_sqm_msg.command_id = sqm_msg;
	else
		stb_sqm_msg.command_id = sqm_msg - 4;
	stb_sqm_msg.command_length = len + 4;
	send_len = len + 4;

	LogUserOperDebug("\n");
	LogUserOperDebug("WILL send to SQMPRO: stb_sqm_msg.command_id = %d, stb_sqm_msg.command_length = %d\n",\
		stb_sqm_msg.command_id,stb_sqm_msg.command_length);
	LogUserOperDebug("MediaType  or loglevel=%d-ChannelIp=%d-ChannelPort=%d-StbPort=%d-ChannelURL=%s \n",\
		sqm_msg_c26->MediaType,sqm_msg_c26->ChannelIp,sqm_msg_c26->ChannelPort,\
		sqm_msg_c26->StbPort,sqm_msg_c26->ChannelURL);
	LogUserOperDebug("\n");

	ret = sendto(pushmsg_sockfd, &stb_sqm_msg, send_len, 0, (struct sockaddr *)&msg_servaddr, sizeof(msg_servaddr));
	LogUserOperDebug("ret = %d, errno=%d[%s]\n",  ret, errno, strerror(errno));
	return ret;

Err:
	return -1;
}
int getSqmDataMdiMLR(void)
{

    LogUserOperDebug("-----mdiMLR = %d\n", g_sqm.mdiMLR);
    return g_sqm.mdiMLR;    
}

int getSqmDataMdiDF(void)
{
    LogUserOperDebug("-----mdiDF= %d\n",  g_sqm.mdiDF);
    return g_sqm.mdiDF;
}

int getSqmDataJitter(void)
{
    LogUserOperDebug("-----jitter= %d\n", g_sqm.jitter);	
    return g_sqm.jitter;
}

 int getSqmDataBadDuration(void)
{
    LogUserOperDebug("get BadDuration = %u\n", g_sqm.badDuration);
    return g_sqm.badDuration;
}
 int getSqmDataPlayduration(void)
{
    LogUserOperDebug("get Playduration= %u\n", g_sqm.playDuration);
    return g_sqm.playDuration;
}

int getSqmDataAvailability(void)
{
//not a nomal value return a default value;
    if(g_sqm.playDuration == 0)
        return 100;
    else
        return (int)((1 - 1.0*g_sqm.badDuration /g_sqm.playDuration ) * 100);
}

int getSqmDataVideoQuality(void)
{
    return g_sqm.vmos;
}
int sqm_port_getdata( int data_type )
{

	int recv_len = 0;
	//SQM_MSG_GET  *data_get = NULL;
	char recv_buf[1024] = {0};

	//LogUserOperDebug("size = %d, data_type =%d\n", size ,data_type);
	if(data_type < 0 || data_type > 1)
		LogUserOperError("[data_type error!], data_type = %d\n", data_type);
       get_sqm_data_type = data_type;

	sqm_port_buildmsg(MSG_GET_DATA);

	recv_len = recvfrom(pushmsg_sockfd, recv_buf, sizeof(recv_buf), 0,NULL, NULL);
	LogUserOperDebug("recv_len / data_get  = %d / %d \n", recv_len, sizeof(SQM_MSG_GET));

	if (recv_len > 0) {
//做一个简单判断，当获取秒级数据的时候接收到的数据是64字节，统计数据结构体大小为88字节		
		if(recv_len == 64){
		    STB_SECOND_DATA *data_get = (STB_SECOND_DATA *)(recv_buf);
		       g_sqm.mdiMLR = (data_get->StatData.mlr)*4/5;
			g_sqm.jitter = (data_get->StatData.jitter)*4/5;
			g_sqm.mdiDF = (data_get->StatData.df /1000)*4/5;
			g_sqm.vmos = data_get->StatData.vmos;
		    LogUserOperDebug("data_type = %d, data_get->result =%d\n", data_type, data_get->result );
		    LogUserOperDebug("00::df-mlr-jitter-vmos-index2:::%d-%d-%d-%d-%d	\n",  data_get->StatData.df,  data_get->StatData.mlr,  data_get->StatData.jitter,  data_get->StatData.vmos,  data_get->StatData.OtherIndex2);
             }else{
		    SQM_MSG_GET * data_get =  (SQM_MSG_GET *)(recv_buf) ;
			    
                    LogUserOperDebug("data_get->result =%d, data_get->sqmStbData.timestamp = %ld\n", data_get->result, data_get->sqmStbData.timestamp);
			LogUserOperDebug("00::df-mlr-jitter-vmos-index2:::%d-%d-%d-%d-%d	\n",  data_get->sqmStbData.StbData[0].df,  data_get->sqmStbData.StbData[0].mlr,  data_get->sqmStbData.StbData[0].jitter,  data_get->sqmStbData.StbData[0].vmos,  data_get->sqmStbData.StbData[0].OtherIndex2);
			LogUserOperDebug("11::df-mlr-jitter-vmos-index2::::%d-%d-%d-%d-%d	\n", data_get->sqmStbData.StbData[1].df,  data_get->sqmStbData.StbData[1].mlr,  data_get->sqmStbData.StbData[1].jitter,  data_get->sqmStbData.StbData[1].vmos,  data_get->sqmStbData.StbData[1].OtherIndex2);
			LogUserOperDebug("22::df-mlr-jitter-vmos-index2:::%d-%d-%d-%d-%d	\n",  data_get->sqmStbData.StbData[2].df,  data_get->sqmStbData.StbData[2].mlr, data_get->sqmStbData.StbData[2].jitter,  data_get->sqmStbData.StbData[2].vmos,  data_get->sqmStbData.StbData[2].OtherIndex2);
			LogUserOperDebug("data_get->Playduration=%d,   data_get->Alarmduration=%d\n",  data_get->sqmStbData.Playduration, data_get->sqmStbData.Alarmduration);
                    if(data_get->sqmStbData.StbData[0].mlr != 0)
			    g_sqm.mdiMLR = (data_get->sqmStbData.StbData[0].mlr)*4/5;
			if(data_get->sqmStbData.StbData[0].jitter != 0)
			    g_sqm.jitter = (data_get->sqmStbData.StbData[0].jitter)*4/5;
			if(data_get->sqmStbData.StbData[0].df != 0)
			    g_sqm.mdiDF = (data_get->sqmStbData.StbData[0].df /1000)*4/5;
			g_sqm.vmos = data_get->sqmStbData.StbData[1].vmos;  //取最大值较好
			g_sqm.badDuration = data_get->sqmStbData.Alarmduration;
			g_sqm.playDuration = data_get->sqmStbData.Playduration;
			LogUserOperDebug("--------------------------------------------------------------\n");
             	}
	}else
		LogUserOperError("[recv from sqmpro error!], recv_len = %d\n", recv_len);

	

	return 0;

}

/************App_sys.c***********/
static void urlCheckSum1(char *buf)
{
    char *p = NULL;
    unsigned int sum = 0;

    p = buf;
    sum = 0;
    while(*p != '\0')
        sum += (unsigned int) * p++;
    sum += 1;
    sprintf(p, "%u", sum);
    return;
}

#define SQM_SAVE_PATH "/data"
int sqm_find_file(void)
{
    DIR *dir = NULL;
    struct dirent *dp = NULL;

    if((dir = opendir(SQM_SAVE_PATH)) != NULL) {
        while((dp = readdir(dir)) != NULL) {
            if((!strcmp(dp->d_name , "sqmpro"))) {
                sqmpro_flag = 1;
            }

            if(sqmpro_flag)
                break;
        }
    } else {
        LogUserOperError("opendir error!\n");
        closedir(dir);
        return -1;
    }
    closedir(dir);
    if(!sqmpro_flag)
        system("rm "SQM_SAVE_PATH"/sqmpro");

    return (sqmpro_flag);
}

void * sqmpro_copy(void *)
{
    system("cp /system/bin/sqmpro  /data");
    return NULL;
}

int sqm_file_check(void)
{
    LogUserOperDebug("sqm_file_check\n");
    pthread_t pHandle_sqm;
    int find = sqm_find_file();

    if((-1 != find) && (!find)) {
        if(!sqmpro_flag)
            pthread_create(&pHandle_sqm, NULL, sqmpro_copy, 0);
        return 1;
    } else
        return 0;
}

int sqm_info_write(char *sqm_info , int sqminfo_len, char *writePath)
{
    FILE *sqm_write = NULL;

    if(!sqm_info)
        return 0;
    LogUserOperDebug("Sqm_info = %s, sqminfo_len = %d\n", sqm_info, sqminfo_len);
    mode_t oldMask = umask(0077);
    sqm_write = fopen(writePath, "ab");
    umask(oldMask);
    if(!sqm_write) {
        LogUserOperError("fopen error!\n");
        return -1;
    }

    fwrite(sqm_info, 1, sqminfo_len, sqm_write);

    fclose(sqm_write);
    return 0;
}

// mqm_ip_get
void sqm_port_mqmip_get(char *ipaddr)
{
	strcpy(ipaddr, MqmcIP);
	return ;
}


int sqm_get_epg_ip(char  *return_ip, int ip_len)
{
    int epg_len = 0;
    char epg_ip[URL_MAX_LEN] = {0}, epg_temp[URL_MAX_LEN] = {0}, *pstr = NULL, *pstr1 = NULL;

    if(ip_len > URL_MAX_LEN) {
        LogSafeOperError("epg_ip  is too lenght\n");
        goto Err;
    }


    snprintf(epg_temp, sizeof(epg_temp), "%s", Hippo::Customer().AuthInfo().AvailableEpgUrl().c_str());
    if(strlen(epg_temp)) {
        pstr = strchr(epg_temp, '/');
        if(pstr == NULL) {
            LogUserOperError("epg_ip format error: epg_ip =%s\n", epg_temp);
            goto Err;
        }

        while(*pstr == '/') {
            pstr++;
        }
        if(pstr == NULL) {
            LogUserOperError("get epg_ip error: epg_ip =%s\n", epg_temp);
            goto Err;
        }

        pstr1 = strchr(pstr, ':');
        if(pstr1 == NULL) {
            LogUserOperError("get epg_ip error: epg_ip =%s\n", epg_temp);
            goto Err;
        }

        epg_len = pstr1 - pstr;
        if(epg_len < 0 || epg_len > ip_len)  {
            LogUserOperError("epg_len error: epg_len =%d\n", epg_temp);
            goto Err;
        } else {
            IND_MEMCPY(epg_ip, pstr, epg_len);
            epg_ip[epg_len] = '\0';
            IND_STRNCPY(return_ip, epg_ip, ip_len);
        }
    } else {
        LogUserOperError("get epg_ip error: epg_ip =%s\n", epg_temp);
        goto Err;
    }

    LogUserOperDebug(" get EPG IP:: %s\n", return_ip);
    return 0;
Err:
    return -1;
}

int sqm_info_copy(char* filePath)
{
    if (!access(filePath, R_OK)) {
        LogUserOperDebug("/var/sqm.ini is exist, return\n");
        return 0;
    }
    FILE* fp = fopen(filePath, "w");
    if (!fp) {
        LogUserOperError("fopen file error\n");
        return -1;
    }
    char tempBuf[512] = {0};
    //StbId
    char StbId[33] = {0};
    memset(tempBuf, 0, sizeof(tempBuf));
#ifdef NEW_ANDROID_SETTING
    sysSettingGetString("STBID", StbId, 33, 0);
#else
    IPTVMiddleware_SettingGetStr("STBID", StbId, 33);
#endif
    snprintf(tempBuf, sizeof(tempBuf), "StbId=%s\n", StbId);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //UserId
    char UserId[33] = {0};
#ifdef NEW_ANDROID_SETTING
    appSettingGetString("ntvuser", UserId, 33, 0);
#else
    IPTVMiddleware_SettingGetStr("ntvuser", UserId, 33);
#endif
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "UserId=%s\n", UserId);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //MacAddress
    char MacAddress[18] = {0};
    // IPTVMiddleware_SettingGetStr("MACAddress", MacAddress, 18);
    // Mantis 24223，华为测试如下说：
    //      规范要求：STB加入组播、加入单播、请求网络链接携带的MAC都为无线网卡的MAC；
    //      STB向EPG、SQM、TMS上报携带的MAC为机顶盒都为有线网卡的MAC
    network_tokenmac_get(MacAddress, sizeof(MacAddress), ':');

    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "MacAddress=%s\n", MacAddress);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);


    //StbIP
    char StbIP[20] = {0};
    #if 0
    char ifname[URL_LEN] = { 0 };
    network_default_ifname(ifname, URL_LEN);
    network_address_get(ifname, StbIP, sizeof(StbIP));
    #else
    #ifdef NEW_ANDROID_SETTING
    appSettingGetString("StbIP", StbIP, sizeof(StbIP), 0);
    #else
    IPTVMiddleware_SettingGetStr("StbIP", StbIP, sizeof(StbIP));
    #endif
    #endif

    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "StbIP=%s\n", StbIP);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //StbNetName
    char StbNetName[33] = {0};
#ifdef NEW_ANDROID_SETTING
        sysSettingGetString("StbNetName", StbNetName, 33, 0);
#else
    IPTVMiddleware_SettingGetStr("StbNetName", StbNetName, 33);
#endif
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "StbNetName=%s\n", StbNetName);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //StbMulticastNetName
    char StbMulticastNetName[33] = {0};
#ifdef NEW_ANDROID_SETTING
    sysSettingGetString("StbNetName", StbMulticastNetName, 33, 0);
#else
    IPTVMiddleware_SettingGetStr("StbNetName", StbMulticastNetName, 33);
#endif
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "StbMulticastNetName=%s\n", StbMulticastNetName);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //PppoeAccount
    char PppoeAccount[33] = {0};
#ifdef NEW_ANDROID_SETTING
    sysSettingGetString("PppoeAccount", PppoeAccount, 33, 0);
#else
    IPTVMiddleware_SettingGetStr("PppoeAccount", PppoeAccount, 33);
#endif
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "PppoeAccount=%s\n", PppoeAccount);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //EpgIP
    char EpgIP[16] = {0};
    if (sqm_get_epg_ip(EpgIP, sizeof(EpgIP)))
        snprintf(EpgIP, sizeof(EpgIP), "0.0.0.0");
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "EpgIP=%s\n", EpgIP);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //StbSoftwareVersion
    char StbSoftwareVersion[33] = {0};
#ifdef NEW_ANDROID_SETTING
    sysSettingGetString("SoftwareVersion", StbSoftwareVersion, 33, 0);
#else
    IPTVMiddleware_SettingGetStr("SoftwareVersion", StbSoftwareVersion, 33);
#endif
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "StbSoftwareVersion=%s\n", StbSoftwareVersion);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //StbHardwareType
    char StbHardwareType[33] = {0};
    snprintf(StbHardwareType, 33, "%s", "3719M");
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "StbHardwareType=%s\n", StbHardwareType);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //MqmcIP
    //char MqmcIP[16] = {0};
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "MqmcIP=%s\n", MqmcIP);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //MqmcSendPort default 37000
    int MqmcSendPort = sqm_get_server_port();
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "MqmcSendPort=%d\n", MqmcSendPort);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //MqmcRecvPort default 37001
    int MqmcRecvPort = sqm_get_listen_port();
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "MqmcRecvPort=%d\n", MqmcRecvPort);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //SqmLogLevel
    int SqmLogLevel = 0;
    SqmLogLevel = getModuleLevel("cuser");
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "SqmLogLevel=%d\n", SqmLogLevel);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //StbNatIP
    char StbNatIP[16] = {0};
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "StbNatIP=%s\n", "StbNatIP");
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //StbNatPort
    int StbNatPort = 0;
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "StbNatPort=%d\n", StbNatPort);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //UpdateUrl
    char UpdateUrl[257] = {0};
    char tempUrl[257] = {0};
    sysSettingGetString("upgradeUrl", tempUrl, sizeof(tempUrl), 0);
    char ip[64 + 4] = {"0.0.0.0"};
    int port = 0;
    if (strlen(tempUrl) && !strncmp(tempUrl, "http://", 7))
        mid_tool_checkURL(tempUrl, ip, &port);
    snprintf(UpdateUrl, sizeof(UpdateUrl), "http://%s:%d/UPGRADE/jsp/upgrade.jsp?TYPE=%s&MAC=%s&USER=%s&VER=%s&CHECKSUM=",
    ip, port, StbHardwareType, MacAddress, UserId, StbSoftwareVersion);
    urlCheckSum1(UpdateUrl);
    strcat(UpdateUrl, "\n");
    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "UpdateUrl=%s\n", UpdateUrl);
    fwrite(tempBuf, strlen(tempBuf), 1, fp);
    LogUserOperDebug("%s", tempBuf);

    //the following contents is writtern by sqmloader
    char SqmVersion[] = {"SqmVersion=\n"};
    fwrite(SqmVersion, strlen(SqmVersion), 1, fp);

    char MqmcIPReg[] = {"MqmcIPReg=\n"};
    fwrite(MqmcIPReg, strlen(MqmcIPReg), 1, fp);

    char MqmcPortReg[] = {"MqmcPortReg=\n"};
    fwrite(MqmcPortReg, strlen(MqmcPortReg), 1, fp);

    char SqmUpdateServerIP[] = {"SqmUpdateServerIP=\n"};
    fwrite(SqmUpdateServerIP, strlen(SqmUpdateServerIP), 1, fp);

    char SqmUpdateServerPort[] = {"SqmUpdateServerPort=\n"};
    fwrite(SqmUpdateServerPort, strlen(SqmUpdateServerPort), 1, fp);

    char SqmloaderLogLevel[] = {"SqmloaderLogLevel=\n"};
    fwrite(SqmloaderLogLevel, strlen(SqmloaderLogLevel), 1, fp);

    char SqmproLogLevel[] = {"SqmproLogLevel=\n"};
    fwrite(SqmproLogLevel, strlen(SqmproLogLevel), 1, fp);

    fclose(fp);

    return 0;
}

/**************************end of file****************************/
