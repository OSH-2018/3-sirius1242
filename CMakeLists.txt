cmake_minimum_required(VERSION 3.7)
project(3-sirius1242)

link_libraries(-lfuse)
add_definitions(-D_FILE_OFFSET_BITS=64)
add_compile_options(-g)
add_custom_command(
    OUTPUT oshfs_block.h
    COMMAND ${CMAKE_COMMAND} -E copy oshfs_block.c oshfs_block.h
    DEPENDS oshfs_block.c
)
add_executable(oshfs oshfs.c oshfs_block.c oshfs_block.h)