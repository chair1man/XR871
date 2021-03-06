/*
  * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
  *
  *  Redistribution and use in source and binary forms, with or without
  *  modification, are permitted provided that the following conditions
  *  are met:
  *    1. Redistributions of source code must retain the above copyright
  * 	  notice, this list of conditions and the following disclaimer.
  *    2. Redistributions in binary form must reproduce the above copyright
  * 	  notice, this list of conditions and the following disclaimer in the
  * 	  documentation and/or other materials provided with the
  * 	  distribution.
  *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
  * 	  its contributors may be used to endorse or promote products derived
  * 	  from this software without specific prior written permission.
  *
  *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <lwip/netdb.h>
#include <time.h>
#include "bbc_sdk.h"
#include "cloud/bbc/utils.h"
#include "cjson/cJSON.h"
#include "bbc_porting.h"
#include "cloud/bbc/devguid_get.h"

//采用本地SDK的SHA1，BBC的SHA-1计算数据有问题
#include "net/mbedtls/sha1.h"

#define BBC_RESTFUL_DBG_SET 	0
#define LOG(flags, fmt, arg...)	\
	do {								\
		if (flags) 						\
			printf(fmt, ##arg);		\
	} while (0)
#define BBC_REST_DBG(fmt, arg...)	\
			LOG(BBC_RESTFUL_DBG_SET, "[BBC_REST_DBG] "fmt, ##arg)


cJSON* toJson(Device* device)
{
	cJSON *jDevice = cJSON_CreateObject();
	cJSON *jDeviceClass = cJSON_CreateObject();

	//deviceClass
	cJSON_AddItemToObject(jDeviceClass,"name", cJSON_CreateString(device->deviceClass.name));
	//device
	cJSON_AddItemToObject(jDevice,"deviceId", cJSON_CreateString(device->deviceId));// <=== id
	cJSON_AddItemToObject(jDevice,"name",cJSON_CreateString(device->name));
	cJSON_AddItemToObject(jDevice,"mac",cJSON_CreateString(device->mac));
	cJSON_AddItemToObject(jDevice,"vendor",cJSON_CreateString(device->vendor));
	cJSON_AddItemToObject(jDevice,"firmwareVersion",cJSON_CreateString(device->firmwareVersion));
	cJSON_AddItemToObject(jDevice,"sdkVersion",cJSON_CreateString(device->sdkVersion));
	cJSON_AddItemToObject(jDevice,"romType",cJSON_CreateString(device->romType));
	cJSON_AddItemToObject(jDevice,"deviceClass",jDeviceClass);
	
	return jDevice;
}

//鎴愬姛 锛氳繑鍥瀌eviceGuid锛屽け璐ヨ繑鍥� NULL
char* register_device(Device *device){
	if(device == NULL){
		return NULL;
	}
	char* result = NULL;
	char temp_url[64] = {0};
	char temp_signature[256] = {0};
	char digest[64] = {0};
	char signature[64] = {0};
	//char request[1024] = {0};
	char *request = malloc(1024);
	if(request == NULL) {
		BBC_REST_DBG("request malloc error!!\n");
	}
	memset(request, 0, 1024);
	
	char* response = NULL;
	int len = 0;
	time_t  time_of_seconds = 0;
	Url* url = NULL;
	cJSON* json_response = NULL, *status = NULL,*data = NULL, *guid = NULL;
	cJSON* jdevice = toJson(device);
	char* device_str = cJSON_PrintUnformatted(jdevice);
	sprintf(temp_url, "%s%s",SERVER_URI, "/device");
	url = url_parse(temp_url);
	if(url == NULL){
		BBC_REST_DBG(" parse url error ");
		goto error;
	}

	//生成签名 组成请求内容
	time(&time_of_seconds); //签名的时间戳，单位：秒
	BBC_REST_DBG(" time of seconds %ld ,", time_of_seconds);
	len = sprintf(temp_signature, "%s%s%s%s%ld%s", device->deviceId, device->mac, device->vendor,device->deviceClass.name,time_of_seconds,bbc_lic);
	temp_signature[len] = '\0';
	BBC_REST_DBG(" len %d , temp_signature %s \n", len, temp_signature);
	mbedtls_sha1((unsigned char*)temp_signature, len, (unsigned char*)digest);
	to_hex_str((unsigned char*)digest, signature, 20);
	BBC_REST_DBG(" device str %s,\nsignature %s \n", device_str, signature);
	sprintf(request, "PUT %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: allwinnertech\r\n"
		"Range: bytes=0-\r\n"
		"Content-Type: application/json;charset=utf-8\r\n"
		"Content-Length: %d\r\n"
		"Auth-Timestamp: %ld\r\n"
		"Auth-Signature: %s\r\n"
		"Connection: Close\r\n\r\n"
		"%s\r\n",
		url->path, url->hostname, strlen(device_str),time_of_seconds, signature, device_str);
	BBC_REST_DBG(" send request %s \n", request);

	response  = execute_request(url->hostname, url->port, request);
	BBC_REST_DBG("response: %s \n", response);

	if(response == NULL){
		BBC_REST_DBG("response error ");
		goto error;
	}
    json_response = cJSON_Parse(response);
    if(json_response == NULL){
    	goto error;
    }
	status = cJSON_GetObjectItem(json_response,"status");
	if(status == NULL || strcmp("error", status->valuestring) == 0){
		goto error;
	}
	data = cJSON_GetObjectItem(json_response,"data");
	if(data != NULL){
		guid = cJSON_GetObjectItem(data, "deviceGuid");
		if(guid == NULL){
			goto error;
		}
		int len = sizeof(char)*strlen(guid->valuestring) + 1;
		result = calloc(len, 1);
		if(result != NULL){
			strcpy(result, guid->valuestring);
		}
	}
error:
	if(json_response) cJSON_Delete(json_response);
	if(jdevice)  cJSON_Delete(jdevice);
	if(url) url_free(url);
	if(response) free(response);
	if(device_str) free(device_str);
	if(request) free(request);

	return result;
}

//返回 0：失败，  1： 成功
int sync_device(char* deviceGuid, Device *device, OtaFailedInfo *failedInfo){
	if(deviceGuid == NULL || device == NULL){
		return 0;
	}
	int result = 0;
	char temp_url[128] = {0};
	char temp_signature[128] = {0};
	char digest[64] = {0};
	char signature[64] = {0};
	//char request[1024] = {0};
	char *request = malloc(1024);
	if(request == NULL) {
		BBC_REST_DBG("request malloc error!!\n");
	}
	memset(request, 0, 1024);
	
	char* response = NULL;
	char* device_str = NULL;
	int len = 0;
	time_t  time_of_seconds = 0;
	Url* url = NULL;
	cJSON* json_response = NULL,*status = NULL, /**ws_url = NULL,*/ *jdevice = NULL;

	jdevice = toJson(device);
	cJSON_AddItemToObject(jdevice, "deviceGuid", cJSON_CreateString(deviceGuid));
	if(failedInfo != NULL){
		cJSON* data = cJSON_CreateObject();
		cJSON* upgradefail = cJSON_CreateObject();
		cJSON_AddItemToObject(upgradefail, "curVersion", cJSON_CreateString(failedInfo->curVersion));
		cJSON_AddItemToObject(upgradefail, "errcode", cJSON_CreateNumber(failedInfo->errorCode));
		cJSON_AddItemToObject(data, "upgradefail", upgradefail);
		cJSON_AddItemToObject(jdevice, "upgrade", data);
	}
	device_str = cJSON_PrintUnformatted(jdevice);//闇�瑕佸悓姝ョ殑璁惧淇℃伅

	sprintf(temp_url, "%s%s%s",SERVER_URI, "/device/", deviceGuid);
	url = url_parse(temp_url);
	if(url == NULL){
		BBC_REST_DBG(" parse url error ");
		goto error;
	}
	//鐢熸垚绛惧悕 缁勬垚璇锋眰鍐呭
	time(&time_of_seconds);
	BBC_REST_DBG(" time of seconds %ld ", time_of_seconds);
	len = sprintf(temp_signature, "%s%ld%s", deviceGuid, time_of_seconds, bbc_lic);
	mbedtls_sha1((unsigned char*)temp_signature, len,(unsigned char*)digest);
	to_hex_str((unsigned char*)digest,signature, 20);
	sprintf(request, "POST %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: allwinnertech\r\n"
		"Range: bytes=0-\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: Close\r\n"
		"Auth-Timestamp: %ld\r\n"
		"Auth-Signature: %s\r\n\r\n"
		"%s\r\n",
		url->path, url->hostname, strlen(device_str),time_of_seconds, signature, device_str);
	BBC_REST_DBG(" send request %s ", request);

	response  = execute_request(url->hostname, url->port, request);
	BBC_REST_DBG("response %s ", response);
	if(response == NULL){
		BBC_REST_DBG("response error ");
		goto error;
	}
	json_response = cJSON_Parse(response);
	if(json_response == NULL){
		BBC_REST_DBG("json_response error ");
		goto error;
	}
	status = cJSON_GetObjectItem(json_response,"status");
	if(status != NULL && strcmp("success", status->valuestring) == 0){
		result = 1;
	}
error:
	if(json_response) cJSON_Delete(json_response);
	if(jdevice)  cJSON_Delete(jdevice);
	if(url) url_free(url);
	if(response) free(response);
	if(device_str) free(device_str);
	if(request) free(request);

	return result;
}


