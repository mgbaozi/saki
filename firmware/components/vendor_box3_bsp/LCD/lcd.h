/**
 ****************************************************************************************************
 * @file        lcd.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2026-01-28
 * @brief       LCD(MCU屏) 驱动代码
 *
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

#ifndef __LCD_H__
#define __LCD_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aw9523b.h"
#include <math.h>
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

/* 引脚定义 */
#define LCD_NUM_CS      GPIO_NUM_47
#define LCD_NUM_DC      GPIO_NUM_48
#define LCD_NUM_RST     GPIO_NUM_NC
#define LCD_NUM_RD      GPIO_NUM_NC
#define LCD_NUM_WR      GPIO_NUM_NC

#define GPIO_LCD_D0     GPIO_NUM_NC
#define GPIO_LCD_D1     GPIO_NUM_NC
#define GPIO_LCD_D2     GPIO_NUM_NC
#define GPIO_LCD_D3     GPIO_NUM_NC
#define GPIO_LCD_D4     GPIO_NUM_NC
#define GPIO_LCD_D5     GPIO_NUM_NC
#define GPIO_LCD_D6     GPIO_NUM_NC
#define GPIO_LCD_D7     GPIO_NUM_NC

#define LCD_RST(x)      do { x ?                                \
                             gpio_set_level(LCD_NUM_RST, 1):   \
                             gpio_set_level(LCD_NUM_RST, 0);   \
                        } while(0)

/* RGB_BL */
#define ESP_LCD_BL(x)   do { x ?                               \
                             aw9523b_pin_write(LCD_BL, 1):     \
                             aw9523b_pin_write(LCD_BL, 0);     \
                        } while(0)

#define LCD_HOST            SPI2_HOST

/* 常用颜色值 */
#define WHITE           0xFFFF      /* 白色 */
#define BLACK           0x0000      /* 黑色 */
#define RED             0xF800      /* 红色 */
#define GREEN           0x07E0      /* 绿色 */
#define BLUE            0x001F      /* 蓝色 */ 
#define MAGENTA         0XF81F      /* 品红色/紫红色 = BLUE + RED */
#define YELLOW          0XFFE0      /* 黄色 = GREEN + RED */
#define CYAN            0X07FF      /* 青色 = GREEN + BLUE */  

/* 非常用颜色 */
#define BROWN           0XBC40      /* 棕色 */
#define BRRED           0XFC07      /* 棕红色 */
#define GRAY            0X8430      /* 灰色 */ 
#define DARKBLUE        0X01CF      /* 深蓝色 */
#define LIGHTBLUE       0X7D7C      /* 浅蓝色 */ 
#define GRAYBLUE        0X5458      /* 灰蓝色 */ 
#define LIGHTGREEN      0X841F      /* 浅绿色 */  
#define LGRAY           0XC618      /* 浅灰色(PANNEL),窗体背景色 */ 
#define LGRAYBLUE       0XA651      /* 浅灰蓝色(中间层颜色) */ 
#define LBBLUE          0X2B12      /* 浅棕蓝色(选择条目的反色) */ 

/* LCD信息结构体 */
typedef struct _lcd_obj_t
{
    uint32_t pwidth;    /* 临时设定值（宽度） */
    uint32_t pheight;   /* 临时设定值（高度） */
    uint8_t  dir;       /* 屏幕方向 */
    uint16_t width;     /* 宽度 */
    uint16_t height;    /* 高度 */
} lcd_obj_t;

/* 导出相关变量 */
extern lcd_obj_t lcddev;;
extern esp_lcd_panel_handle_t panel_handle;

/* lcd相关函数 */
esp_err_t lcd_init(void);                                                                                    /* 初始化lcd */
void lcd_clear(uint16_t color);                                                                                         /* 清除屏幕 */
void lcd_display_dir(uint8_t dir);                                                                                      /* lcd显示方向设置 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);                                                            /* lcd画点函数 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);                                /* 在指定区域内填充指定颜色块 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);                                 /* 画线 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);                                              /* 画圆 */
void lcd_show_char(uint16_t x, uint16_t y, uint8_t chr, uint8_t size, uint8_t mode, uint16_t color);                       /* 在指定位置显示一个字符 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);                     /* 显示len个数字 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);      /* 扩展显示len个数字(高位是0也显示) */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);   /* 显示字符串 */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color);                             /* 绘画矩形 */
void lcd_app_show_mono_icos(uint16_t x,uint16_t y,uint8_t width,uint8_t height,uint8_t *icosbase,uint16_t color,uint16_t bkcolor); /* 显示单色图标 */

#endif
