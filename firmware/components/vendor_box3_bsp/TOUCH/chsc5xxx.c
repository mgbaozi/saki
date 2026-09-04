/**
 ****************************************************************************************************
 * @file        CHSC5XXX.c
 * @author      正点原子团队(正点原子)
 * @version     V1.1
 * @date        2023-12-1
 * @brief       2.4寸电容触摸屏-CHSC5xxx 驱动代码
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

#include "chsc5xxx.h"


const char* touch_name = "touch.c";
i2c_master_dev_handle_t chsc5xxx_handle = NULL;

/* 默认支持的触摸屏点数(5点触摸) */
uint8_t g_chsc_tnum = 5;
uint8_t temp[4];

/**
 * @brief       向chsc5xxx写入数据
 * @param       reg : 起始寄存器地址
 * @param       buf : 数据缓缓存区
 * @param       len : 写数据长度
 * @retval      esp_err_t ：0, 成功; 1, 失败;
 */
esp_err_t chsc5xxx_wr_reg(uint32_t reg, uint8_t *buf, unsigned int len)
{
    esp_err_t ret;
    uint8_t *wr_buf = malloc(4 + len);

    if (wr_buf == NULL)
    {
        ESP_LOGE(touch_name, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    wr_buf[0] = (reg >> 24) & 0xFF; /* ADDR[31:24] */
    wr_buf[1] = (reg >> 16) & 0xFF; /* ADDR[23:16] */
    wr_buf[2] = (reg >> 8) & 0xFF;  /* ADDR[15:8] */
    wr_buf[3] = reg & 0xFF;         /* ADDR[7:0] */
    
    memcpy(wr_buf + 4, buf, len);     /* 拷贝数据至存储区当中 */

    ret = i2c_master_transmit(chsc5xxx_handle, wr_buf, 4 + len,1000);

    free(wr_buf);                      /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       从chsc5xxx读出数据
 * @param       reg : 起始寄存器地址
 * @param       buf : 数据缓缓存区
 * @param       len : 读数据长度
 * @retval      esp_err_t ：0, 成功; 1, 失败;
 */
esp_err_t chsc5xxx_rd_reg(uint32_t reg, uint8_t *buf, unsigned int len)
{
    uint8_t memaddr_buf[4];

    memaddr_buf[0] = (reg >> 24) & 0xFF;   /* ADDR[31:24] */
    memaddr_buf[1] = (reg >> 16) & 0xFF;   /* ADDR[23:16] */
    memaddr_buf[2] = (reg >> 8) & 0xFF;    /* ADDR[15:8] */
    memaddr_buf[3] = reg & 0xFF;           /* ADDR[7:0] */

    return i2c_master_transmit_receive(chsc5xxx_handle, memaddr_buf, sizeof(memaddr_buf), buf, len, 1000);
}

extern int semi_touch_init();

/**
 * @brief       初始化chsc5xxx触摸屏
 * @param       无
 * @retval      0, 初始化成功; 1, 初始化失败;
 */
uint8_t chsc5xxx_init(void)
{   
    if (bus_handle == NULL)
    {
            myiic_init();                               /* 初始化I2C */
    }

    i2c_device_config_t chsc5xxx_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,      /* 从机地址长度 */
        .scl_speed_hz    = 100000,                  /* 传输速率 */
        .device_address  = CHSC5432_ADDR,           /* 从机7位的地址 */
    };
    /* I2C总线上添加gt9xxx设备 */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &chsc5xxx_i2c_dev_conf, &chsc5xxx_handle));
    
    /* 复位触摸屏 */
    CT_RST(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    CT_RST(1);
    vTaskDelay(pdMS_TO_TICKS(100));

    chsc5xxx_rd_reg(CHSC5XXX_PID_REG, temp, 4);             /* 从0x20000080地址读取ID */
    temp[3] = 0;
    ESP_LOGI(touch_name, "CTP:0x%x", temp[0]);
    return 0;
}

/**
 * @brief       扫描触摸屏(采用查询方式)
 * @param       mode : 电容屏未用到次参数, 为了兼容电阻屏
 * @retval      当前触屏状态
 *   @arg       0, 触屏无触摸; 
 *   @arg       1, 触屏有触摸;
 */
uint8_t chsc5xxx_scan(uint8_t mode)
{
    uint8_t buf[28];
    uint8_t i = 0;
    uint8_t res = 0;
    uint16_t temp;
    uint16_t tempsta;
    static uint8_t t = 0;                            /* 控制查询间隔,从而降低CPU占用率 */
    t++;

    if ((t % 5) == 0 || t < 5)                       /* 空闲时,每进入10次CTP_Scan函数才检测1次,从而节省CPU使用率 */
    {
        chsc5xxx_rd_reg(CHSC5XXX_CTRL_REG, buf, 28); /* 官方建议一次读取28字节，然后在分析触摸坐标 */
        mode = 0x80 + buf[1];
        //ESP_LOGI(touch_name, "mode:%d", mode);

        /* 判断是否有触摸按下 */
        if ((mode & 0XF) && ((mode & 0XF) <= g_chsc_tnum))
        {
            temp = 0XFFFF << (mode & 0XF);
            tempsta = tp_dev.sta;
            tp_dev.sta = (~temp) | TP_PRES_DOWN | TP_CATH_PRES;
            tp_dev.x[g_chsc_tnum - 1] = tp_dev.x[0];
            tp_dev.y[g_chsc_tnum - 1] = tp_dev.y[0];
            /* 获取触摸坐标 */
            for (i = 0; i < g_chsc_tnum; i++)
            {
                if (tp_dev.sta & (1 << i))
                {
                    if (tp_dev.touchtype & 0X01)                        /* 横屏 */
                    {
                        tp_dev.x[i] = ((uint16_t)(buf[5 + i * 5] >> 4 ) << 8) + buf[3 + i * 5];
                        tp_dev.y[i] = lcddev.height - (((uint16_t)(buf[5 + i * 5] & 0x0F) << 8) + buf[2 + i * 5]);
                    }
                    else                                                /* 竖屏 */
                    {
                        tp_dev.x[i] = ((uint16_t)(buf[5 + i * 5] & 0x0F) << 8) + buf[2 + i * 5];
                        tp_dev.y[i] = ((uint16_t)((buf[5 + i * 5] & 0xF0) >> 4 ) << 8) + buf[3 + i * 5];
                    }

                    ESP_LOGD(touch_name, "x[%d], y[%d]", tp_dev.x[i], tp_dev.y[i]);
                }
            }
            res = 1;
            /* 触摸坐标超出屏幕范围 */
            if (tp_dev.x[0] > lcddev.width || tp_dev.y[0] > lcddev.height)
            {
                /* 触摸数量是否大于1~5 */
                if ((mode & 0XF) > 1)
                {
                    tp_dev.x[0] = tp_dev.x[1];
                    tp_dev.y[0] = tp_dev.y[1];
                    t = 0;
                }
                else    /* 超出范围且未检测到触摸点 */
                {
                    tp_dev.x[0] = tp_dev.x[g_chsc_tnum - 1];
                    tp_dev.y[0] = tp_dev.y[g_chsc_tnum - 1];
                    mode = 0X80;
                    tp_dev.sta = tempsta;
                }
            }
            else 
            {
                t = 0;
            }
        }
    }

    //printf("x[0]:%d,y[0]:%d\r\n", tp_dev.x[0], tp_dev.y[0]);
    
    /* 超出触摸范围 */
    if ((mode & 0X8F) == 0X80)
    {
        if (tp_dev.sta & TP_PRES_DOWN)
        {
            tp_dev.sta &= ~TP_PRES_DOWN;
        }
        else
        {
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
            tp_dev.sta &= 0XE000;
        }
    }

    if (t > 240)
    {
        t = 10;
    }

    return res;
}
