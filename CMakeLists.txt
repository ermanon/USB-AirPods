# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(FAMILY rp2040)
set(BOARD pico_sdk)



# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)


project(PicoW_USB_BT_Audio C CXX ASM)

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND
   NOT "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}" MATCHES "-O[123sg]")
    message(FATAL_ERROR "USBPods real-time audio requires optimized C code. Configure with -DCMAKE_C_FLAGS_RELEASE='-O3 -DNDEBUG -g'; the current Release cache has no optimization.")
endif()

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()
include(cmake/tinyusb_rp2350_fix.cmake)


#LDAC https://github.com/EHfive/ldacBT
add_subdirectory(3rd-party/ldacBT)


# FDK-AAC (encoder only)
set(FDK_DIR ${CMAKE_CURRENT_LIST_DIR}/3rd-party/fdk-aac)
set(FDK_BUILD_DIR ${CMAKE_BINARY_DIR}/fdk-aac)


add_subdirectory(${FDK_DIR} ${FDK_BUILD_DIR})
target_compile_definitions(fdk-aac PUBLIC FDK_ARM_ARCH)
target_compile_definitions(fdk-aac PRIVATE USBPODS_AAC_ELD_MIC_DECODER)


set(BTSTACK_3RD_PARTY_PATH ${PICO_SDK_PATH}/lib/btstack/3rd-party)
#set(BTSTACK_EXAMPLE_PATH $ENV{PICO_SDK_PATH}/lib/btstack/3rd-party)


# Add executable.
add_executable(${PROJECT_NAME}
    src/main.c
    src/pico_w_led.c
    src/btstack/btstack_avdtp_source.c
    src/btstack/btstack_hci.c
    src/btstack/btstack_aacp.c
    src/btstack/airpods_case.c
    src/btstack/aacp_mic_dec.c
    src/tinyusb/uac.c
    src/tinyusb/usb_descriptors.c
    src/tinyusb/debug_cdc.c
)
include(cmake/audio_stack.cmake)


# Including header files directly from project directory
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)


target_include_directories(${PROJECT_NAME} PRIVATE
        3rd-party/ldacBT/libldac/inc
        3rd-party/ldacBT/libldac/abr/inc
        3rd-party/fdk-aac
)


# Link the Project to extra libraries
target_link_libraries(PicoW_USB_BT_Audio
pico_stdlib
pico_btstack_ble
pico_btstack_classic
pico_btstack_cyw43
pico_cyw43_arch_threadsafe_background
pico_btstack_sbc_encoder
pico_multicore
tinyusb_device
tinyusb_board
fdk-aac
ldacBT_enc
ldacBT_abr
)

# Drop the blocking UART stdout path entirely. UART writes stall the calling
# context (~1ms per ~12 chars at 115200); printf from IRQs / the BTstack run
# loop caused audio stutter and stalls. We log through the CDC ring instead
# (non-blocking producer, drained in the USB task) — see src/tinyusb/debug_cdc.c.
pico_enable_stdio_uart(PicoW_USB_BT_Audio 0)

target_compile_definitions(PicoW_USB_BT_Audio PRIVATE
    CYW43_LWIP=0
    PICO_FLASH_ASSUME_CORE1_SAFE=1
    # malloc failure must return NULL (fdk-aac handles it and we log it), not
    # panic()+halt — an OOM halt froze USB enumeration and looked like a brick.
    PICO_MALLOC_PANIC=0
    # The CDC producer already guards its ring with IRQ masking. The SDK
    # stdout mutex can otherwise block an IRQ that preempts a thread printf.
    PICO_STDOUT_MUTEX=0

    # must enable on rp2040 & 330kbps(MQ)
    #LDAC_SOFT_FLOAT=OFF
)

# Export binaries like hex, bin, and uf2 files.
pico_add_extra_outputs(${PROJECT_NAME})
