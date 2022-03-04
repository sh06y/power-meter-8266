#include <EEPROM.h>

#ifndef _SMART_CONFIG_
// 声明
#define _SMART_CONFIG_

#define EEPROM_START 1024

struct WifiPwd {
  char ssid[32];
  char pwd[64];
};

/*
  保存参数到eeprom
*/
void saveConfig(WifiPwd *wifipwd) {
  EEPROM.begin(EEPROM_START);
  uint8_t *p = (uint8_t*)(wifipwd);
  for (int i = 0; i < sizeof(*wifipwd); i++) {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
  // 释放内存
  delete wifipwd;
}

/*
   获取wifi账号密码信息
*/
WifiPwd* loadConfigs() {
  // 为变量请求内存
  WifiPwd *pvalue  = new WifiPwd;   
  EEPROM.begin(EEPROM_START);
  uint8_t *p = (uint8_t*)(pvalue);
  for (int i = 0; i < sizeof(*pvalue); i++) {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.commit();
  return pvalue;
}

/**
   清空wifi账号和密码
*/
void clearConfig() {
  EEPROM.begin(EEPROM_START);
  // 这里为啥是96 ，因为在结构体声明的长度之和就是96
  for (int i = 0; i < 96; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

#endif