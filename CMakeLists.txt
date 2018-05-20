cmake_minimum_required(VERSION 3.7)
project(3-sirius1242)

link_libraries(-lfuse)
add_definitions(-D_FILE_OFFSET_BITS=64)
add_compile_options(-g -Wall)
add_executable(oshfs oshfs-2.c)
