cmake_minimum_required(VERSION 3.0)
project(cpython)

file(GLOB SOURCE_FILES
        Programs/*.c
        Python/*.c
        Parser/*.c
        Objects/*.c
        Modules/*.c)

include_directories(.
        Include
        Include/internal
        )

add_executable(python.exe ${SOURCE_FILES})