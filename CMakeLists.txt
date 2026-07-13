cmake_minimum_required(VERSION 3.16)

project(odor_sensing_rpi5 VERSION 0.1.0 LANGUAGES CXX)

set(ODOR_BUILD_LINUX_HARDWARE_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ODOR_BUILD_LINUX_HARDWARE_DEFAULT ON)
endif()
option(ODOR_BUILD_LINUX_HARDWARE "Build Linux I2C/SPI/GPIO implementations" ${ODOR_BUILD_LINUX_HARDWARE_DEFAULT})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(odor_core
    src/app/RuntimeConfig.cpp
    src/drivers/ADS114S06Driver.cpp
    src/drivers/BME690Driver.cpp
    src/drivers/MCP3421Driver.cpp
    src/drivers/SGP41Driver.cpp
    src/drivers/SHT45Driver.cpp
    src/hardware/mock/MockGpioLine.cpp
    src/hardware/mock/MockI2CBus.cpp
    src/hardware/mock/MockSPIDevice.cpp
    src/protocol/ProtocolUtils.cpp
    src/services/RawCsvLogger.cpp
    src/services/SensorManager.cpp
    src/services/TgsCsvLogger.cpp
)

target_include_directories(odor_core PUBLIC include)

if(MSVC)
    target_compile_options(odor_core PRIVATE /W4)
else()
    target_compile_options(odor_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ODOR_BUILD_LINUX_HARDWARE)
    add_library(odor_linux_hardware
        src/app/LinuxHardwareValidation.cpp
        src/hardware/linux/LinuxGpioLine.cpp
        src/hardware/linux/LinuxI2CBus.cpp
        src/hardware/linux/LinuxSPIDevice.cpp
    )
    target_include_directories(odor_linux_hardware PUBLIC include)
    target_link_libraries(odor_linux_hardware PUBLIC odor_core)

    find_library(GPIOD_LIBRARY gpiod)
    if(NOT GPIOD_LIBRARY)
        message(FATAL_ERROR "libgpiod is required for ODOR_BUILD_LINUX_HARDWARE=ON")
    endif()
    target_link_libraries(odor_linux_hardware PUBLIC ${GPIOD_LIBRARY})
endif()

add_executable(odor_sensing_app src/main.cpp)
target_link_libraries(odor_sensing_app PRIVATE odor_core)
if(ODOR_BUILD_LINUX_HARDWARE)
    target_link_libraries(odor_sensing_app PRIVATE odor_linux_hardware)
    target_compile_definitions(odor_sensing_app PRIVATE ODOR_HAS_LINUX_HARDWARE=1)
endif()

enable_testing()

add_executable(sensor_manager_smoke_test tests/unit/SensorManagerSmokeTest.cpp)
target_link_libraries(sensor_manager_smoke_test PRIVATE odor_core)
add_test(NAME sensor_manager_smoke_test COMMAND sensor_manager_smoke_test)

add_executable(driver_protocol_test tests/unit/DriverProtocolTest.cpp)
target_link_libraries(driver_protocol_test PRIVATE odor_core)
add_test(NAME driver_protocol_test COMMAND driver_protocol_test)
