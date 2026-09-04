/**
 ****************************************************************************************************
 * @file        aw9523b.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2026-01-28
 * @brief       AW9523B驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 BOX3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __AW9523B_H
#define __AW9523B_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "myiic.h"
#include "string.h"


/* 引脚与相关参数定义 */
#define AW9523B_INT_IO              GPIO_NUM_42                      /* AW9523B_INT引脚 */
#define AW9523B_INT                 gpio_get_level(AW9523B_INT_IO)   /* 读取AW9523B_INT的电平 */

/* AW9523B寄存器地址 */
#define AW9523B_INPUT_PORT0_REG      0x00                            /* 输入寄存器0地址 */
#define AW9523B_INPUT_PORT1_REG      0x01                            /* 输入寄存器1地址 */
#define AW9523B_OUTPUT_PORT0_REG     0x02                            /* 输出寄存器0地址 */
#define AW9523B_OUTPUT_PORT1_REG     0x03                            /* 输出寄存器1地址 */
#define AW9523B_CONFIG_PORT0_REG     0x04                            /* 方向配置寄存器0地址 */
#define AW9523B_CONFIG_PORT1_REG     0x05                            /* 方向配置寄存器1地址 */
#define AW9523B_INT_ENABLE_PORT0_REG 0x06                            /* 中断使能寄存器0地址 */
#define AW9523B_INT_ENABLE_PORT1_REG 0x07                            /* 中断使能寄存器1地址 */
#define AW9523B_ID_REG               0x10                            /* ID寄存器地址 */
#define AW9523B_CTL_REG              0x11                            /* 全局控制寄存器地址 */
#define AW9523B_LED_MODE_P0_REG      0x12                            /* P0口LED模式切换寄存器 */
#define AW9523B_LED_MODE_P1_REG      0x13                            /* P1口LED模式切换寄存器 */
#define AW9523B_SW_RST_REG           0x7F                            /* 软件复位寄存器 */

#define AW9523_ID                    0x23

/* AD1/AD0组合对应的地址：
   AD1=0, AD0=0: 0x58
   AD1=0, AD0=1: 0x59
   AD1=1, AD0=0: 0x5A
   AD1=1, AD0=1: 0x5B
*/
#define AW9523B_ADDR                 0x59

/* AW9523B各个IO的功能定义 - 根据实际硬件连接修改 */
/* P0端口 (P0_0 ~ P0_7) */
#define P0_0                         0x0001
#define P0_1                         0x0002  
#define P0_2                         0x0004
#define P0_3                         0x0008
#define P0_4                         0x0010
#define P0_5                         0x0020
#define P0_6                         0x0040
#define P0_7                         0x0080

/* P1端口 (P1_0 ~ P1_7) */
#define P1_0                         0x0100
#define P1_1                         0x0200
#define P1_2                         0x0400
#define P1_3                         0x0800
#define P1_4                         0x1000
#define P1_5                         0x2000
#define P1_6                         0x4000
#define P1_7                         0x8000

/* 具体功能映射 - 根据实际硬件设计修改 */
#define LCD_BL                       P1_0
#define LED_RED                      P1_1
#define LED_BLUE                     P1_2
#define VDD_3V3_EN                   P1_3
#define KEY_K1                       P0_0
#define KEY_K2                       P0_1
#define BAT_CHRG_EN                  P0_2
#define BAT_CHRG                     P0_3
#define ESP_ADC_SEL                  P0_4
#define PA_CTRL                      P0_5
#define EXT_GPIO0                    P0_6
#define EXT_GPIO1                    P0_7
#define VBAT_EN                      P1_4
#define VDDA_3V3_EN                  P1_5
#define VDD_2V8_EN                   P1_6
#define TP_CAM_RESET                 P1_7

#define KEY1                        aw9523b_pin_read(KEY_K1)        /* 读取K1引脚 */
#define KEY2                        aw9523b_pin_read(KEY_K2)        /* 读取K2引脚 */

#define KEY1_PRES                   2                               /* K1按下 */
#define KEY2_PRES                   3                               /* K2按下 */

#define LEDR_TOGGLE()    do { aw9523b_pin_write(LED_RED, !aw9523b_pin_read(LED_RED)); } while(0)  /* LEDR翻转 */

/* 函数声明 */
esp_err_t aw9523b_init(void);                                            /* 初始化AW9523B */
int aw9523b_pin_read(uint16_t pin);                                      /* 获取某个IO状态 */
uint16_t aw9523b_pin_write(uint16_t pin, int val);                       /* 控制某个IO的电平 */
esp_err_t aw9523b_read_byte(uint8_t* data, size_t len);                  /* 读取AW9523B的IO值 */
esp_err_t aw9523b_write_byte(uint8_t reg, uint8_t *data, size_t len);    /* 向AW9523B寄存器写入数据 */
uint8_t aw9523b_key_scan(uint8_t mode);                                  /* 扫描扩展按键 */
void aw9523b_int_init(void);                                             /* 初始化AW9523B的中断引脚 */
void aw9523b_ioconfig(uint16_t config_value);                            /* AW9523B的IO配置 */
esp_err_t aw9523b_soft_reset(void);                                      /* 软件复位 */

#endif
