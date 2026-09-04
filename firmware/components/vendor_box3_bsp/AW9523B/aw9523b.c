/**
 ****************************************************************************************************
 * @file        aw9523b.c
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

#include "aw9523b.h"
const char *aw9523b_tag  = "aw9523b";
i2c_master_dev_handle_t aw9523b_handle  = NULL;

/**
 * @brief       读取AW9523B的输入状态
 * @param       data:读取数据的存储区
 * @param       len:读取数据的大小
 * @retval      ESP_OK:读取成功; 其他:读取失败
 */
esp_err_t aw9523b_read_byte(uint8_t *data, size_t len)
{
    uint8_t reg_addr = AW9523B_INPUT_PORT0_REG;
    
    return i2c_master_transmit_receive(aw9523b_handle, &reg_addr, 1, data, len, -1);
}

/**
 * @brief       向AW9523B寄存器写入数据
 * @param       reg:寄存器地址
 * @param       data:要写入数据的存储区
 * @param       len:要写入数据的大小
 * @retval      ESP_OK:写入成功; 其他:写入失败
 */
esp_err_t aw9523b_write_byte(uint8_t reg, uint8_t *data, size_t len)
{
    esp_err_t ret;

    uint8_t *buf = malloc(1 + len);
    if (buf == NULL)
    {
        ESP_LOGE(aw9523b_tag, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    buf[0] = reg;                   /* 0号元素为寄存器数值 */
    memcpy(buf + 1, data, len);     /* 拷贝数据至存储区中 */

    ret = i2c_master_transmit(aw9523b_handle, buf, len + 1, -1);

    free(buf);                      /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       控制某个IO的电平
 * @param       pin     : 控制的IO
 * @param       val     : 电平
 * @retval      返回所有IO状态
 */
uint16_t aw9523b_pin_write(uint16_t pin, int val)
{
    uint8_t w_data[2];
    uint16_t temp = 0x0000;

    /* 读取当前输出状态 */
    uint8_t reg_addr = AW9523B_OUTPUT_PORT0_REG;
    i2c_master_transmit_receive(aw9523b_handle, &reg_addr, 1, w_data, 2, -1);

    if (pin <= 0x00FF)
    {
        if (val)
        {
            w_data[0] |= (uint8_t)(0xFF & pin);
        }
        else
        {
            w_data[0] &= ~(uint8_t)(0xFF & pin);
        }
    }
    else
    {
        if (val)
        {
            w_data[1] |= (uint8_t)(0xFF & (pin >> 8));
        }
        else
        {
            w_data[1] &= ~(uint8_t)(0xFF & (pin >> 8));
        }
    }
 
     temp = ((uint16_t)w_data[1] << 8) | w_data[0]; 

     aw9523b_write_byte(AW9523B_OUTPUT_PORT0_REG, w_data, 2);
    
    return temp;
}

/**
 * @brief       获取某个IO状态
 * @param       pin : 要获取状态的IO
 * @retval      此IO口的值(状态, 0/1)
 */
int aw9523b_pin_read(uint16_t pin)
{
    uint16_t ret;
    uint8_t r_data[2];

    aw9523b_read_byte(r_data, 2);

    ret = r_data[1] << 8 | r_data[0];

    return (ret & pin) ? 1 : 0;
}

/**
 * @brief       AW9523B的IO方向配置
 * @param       config_value：IO配置输入或者输出 (0:输出, 1:输入)
 * @retval      无
 */
void aw9523b_ioconfig(uint16_t config_value)
{
    /* 从机地址 + CMD + data1(P0) + data2(P1) */
    /* P10、P11、P12、P13和P14为输入，其他引脚为输出 -->0001 1111 0000 0000 注意：0为输出，1为输入*/
    uint8_t data[2];
    esp_err_t err;

    data[0] = (uint8_t)(0xFF & config_value);
    data[1] = (uint8_t)(0xFF & (config_value >> 8));

    do
    {
        err = aw9523b_write_byte(AW9523B_CONFIG_PORT0_REG, data, 2);
        if (err != ESP_OK)
        {
            ESP_LOGE(aw9523b_tag, "%s configure %X failed, ret: %d", __func__, config_value, err);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
    } while (err != ESP_OK);
}

/**
 * @brief       配置中断使能
 * @param       enable_value：中断使能配置 (0:使能中断, 1:关闭中断)
 * @retval      无
 */
void aw9523b_int_enable_config(uint16_t enable_value)
{
    uint8_t data[2];

    data[0] = (uint8_t)(0xFF & enable_value);    /* P0中断使能 */
    data[1] = (uint8_t)(0xFF & (enable_value >> 8)); /* P1中断使能 */

    aw9523b_write_byte(AW9523B_INT_ENABLE_PORT0_REG, data, 2);
}

/**
 * @brief       配置LED模式
 * @param       p0_mode：P0端口LED模式 (0:LED模式, 1:GPIO模式)
 * @param       p1_mode：P1端口LED模式 (0:LED模式, 1:GPIO模式)
 * @retval      无
 */
void aw9523b_led_mode_config(uint8_t p0_mode, uint8_t p1_mode)
{
    aw9523b_write_byte(AW9523B_LED_MODE_P0_REG, &p0_mode, 1);
    aw9523b_write_byte(AW9523B_LED_MODE_P1_REG, &p1_mode, 1);
}

/**
 * @brief       软件复位
 * @param       无
 * @retval      ESP_OK:复位成功
 */
esp_err_t aw9523b_soft_reset(void)
{
    uint8_t reset_cmd = 0x00;
    return aw9523b_write_byte(AW9523B_SW_RST_REG, &reset_cmd, 1);
}

/**
 * @brief       外部中断服务函数
 * @param       arg：中断引脚号
 * @note        IRAM_ATTR: 这里的IRAM_ATTR属性用于将中断处理函数存储在内部RAM中，目的在于减少延迟
 * @retval      无
 */
static void IRAM_ATTR aw9523b_exit_gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    
    if (gpio_num == AW9523B_INT_IO)
    {
         /* AW9523B中断去抖动时间为8μs，这里适当延长 */
        esp_rom_delay_us(20000);

        if (gpio_get_level(AW9523B_INT_IO) == 0)
        {
            /* 读取输入状态清除中断 */
            // uint8_t r_data[2];
            // aw9523b_read_byte(r_data, 2);

            /* 中断处理 */
            // ESP_DRAM_LOGI("aw9523b", "aw9523b int, P0:0x%02X, P1:0x%02X", r_data[0], r_data[1]);
        }
    }
}

/**
 * @brief       AW9523B外部中断初始化程序
 * @param       无
 * @retval      无
 */
void aw9523b_int_init(void)
{
    gpio_config_t gpio_init_struct;

    /* 配置XL9555器件的INT中断引脚 */
    gpio_init_struct.mode         = GPIO_MODE_INPUT;        /* 选择为输入模式 */
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_ENABLE;     /* 上拉使能 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 下拉失能 */
    gpio_init_struct.intr_type    = GPIO_INTR_NEGEDGE;      /* 下降沿触发 */
    gpio_init_struct.pin_bit_mask = 1ull << AW9523B_INT_IO; /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                         /* 配置使能 */
    
    /* 注册中断服务 */
    gpio_install_isr_service(0);
    
    /* 设置GPIO的中断回调函数 */
    gpio_isr_handler_add(AW9523B_INT_IO, aw9523b_exit_gpio_isr_handler, (void*)AW9523B_INT_IO);
}

/**
 * @brief       初始化AW9523B
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t aw9523b_init(void)
{
    uint8_t r_data[2];
    uint8_t id_value;

        /* 未调用myiic_init初始化IIC */ 
    if (bus_handle == NULL)
    {
        ESP_ERROR_CHECK(myiic_init());
    }

    i2c_device_config_t aw9523b_i2c_dev_conf  = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,  /* 从机地址长度 */
        .scl_speed_hz    = IIC_SPEED_CLK,       /* 传输速率 */
        .device_address  = AW9523B_ADDR,         /* 从机7位的地址 */
    };
    /* I2C总线上添加XL9555设备 */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &aw9523b_i2c_dev_conf, &aw9523b_handle));

    /* 读取ID寄存器验证设备 */
    uint8_t reg_addr = AW9523B_ID_REG;
    i2c_master_transmit_receive(aw9523b_handle, &reg_addr, 1, &id_value, 1, -1);

    if (id_value != AW9523_ID) 
    {
        ESP_LOGE(aw9523b_tag, "AW9523B ID verification failed: 0x%02X, expected 0x23", id_value);
        return ESP_FAIL;
    }
    ESP_LOGI(aw9523b_tag, "AW9523B ID verified: 0x%02X", id_value);

    /* 配置所有端口为GPIO模式（非LED模式） */
    uint8_t gpio_mode = 0xFF;
    aw9523b_write_byte(AW9523B_LED_MODE_P0_REG, &gpio_mode, 1);
    aw9523b_write_byte(AW9523B_LED_MODE_P1_REG, &gpio_mode, 1);

    /* 配置P0口为推挽输出模式 */
    uint8_t ctl_value = 0x10;  /* 设置D[4]=1，P0口为Push-Pull模式 */
    aw9523b_write_byte(AW9523B_CTL_REG, &ctl_value, 1);

    /* 输入模式下，中断才有效（读取IO电平） */
    //aw9523b_int_init();

    /* 上电先读取一次清除中断标志 */
    aw9523b_read_byte(r_data, 2);
    /* 配置那些扩展管脚为输入输出模式 */
    aw9523b_ioconfig(0x003);

    aw9523b_pin_write(PA_CTRL, 1);
    aw9523b_pin_write(VBAT_EN, 1);
    aw9523b_pin_write(VDD_3V3_EN, 1);
    aw9523b_pin_write(VDDA_3V3_EN, 1);
    aw9523b_pin_write(ESP_ADC_SEL, 1);
    
    return ESP_OK;
}

/**
 * @brief       按键扫描函数
 * @param       mode:0->不连续;1->连续
 * @retval      键值, 定义如下:
 *              KEY0_PRES, 2, K1按下
 *              KEY1_PRES, 3, K2按下
 */
uint8_t aw9523b_key_scan(uint8_t mode)
{
    uint8_t keyval = 0;
    static uint8_t key_up = 1;                                          /* 按键按松开标志 */

    if (mode)
    {
        key_up = 1;                                                     /* 支持连按 */
    }

    if (key_up && (KEY1 == 0 || KEY2 == 1))                             /* 按键松开标志为1, 且有任意一个按键按下了 */
    {
        vTaskDelay(10);                                                 /* 去抖动 */
        key_up = 0;

        if (KEY1 == 0)
        {
            keyval = KEY1_PRES;
        }

        if (KEY2 == 1)
        {
            keyval = KEY2_PRES;
        }
    }
    else if (KEY1 == 1 && KEY2 == 0)                                    /* 没有任何按键按下, 标记按键松开 */
    {
        key_up = 1;
    }

    return keyval;                                                      /* 返回键值 */
}
