idf_component_register(
    SRCS
        "usb_console.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_timer
        espressif__esp_tinyusb
        soc
)

if(CONFIG_USB_HS_CONSOLE_USB_CDC_AUTO_INIT)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=app_main"
        "-u __wrap_app_main"
    )
endif()
