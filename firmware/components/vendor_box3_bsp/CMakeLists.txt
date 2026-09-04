set(src_dirs
            MYIIC
            LCD
            MYSPI
            TOUCH
            AW9523B)

set(include_dirs
            MYIIC
            LCD
            MYSPI
            TOUCH
            AW9523B)

set(requires
            driver
            esp_lcd)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

# The BOX3 carries the 2.4-inch 320x240 panel. The vendor source otherwise
# treats an undefined SPI_LCD_TYPE as the 240x240 panel variant.
target_compile_definitions(${COMPONENT_LIB} PRIVATE SPI_LCD_TYPE=1)

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
