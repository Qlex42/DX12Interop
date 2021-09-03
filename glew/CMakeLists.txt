#\-------------------------------------- . -----------------------------------/#
# Filename : CMakeList.txt               | Glew                                #
# Author   : Alexandre Buge              |                                     #
# Started  : 04/04/2014 11:21            |                                     #
#/-------------------------------------- . -----------------------------------\#

FIND_PACKAGE(OpenGL REQUIRED)

SET(GLEW_SOURCES
  src/glew.c
  include/GL/glew.h
  include/GL/glxew.h
  include/GL/wglew.h
)

ADD_LIBRARY(glew STATIC ${GLEW_SOURCES})

target_compile_definitions(glew PRIVATE GLEW_STATIC)
target_compile_definitions(glew PRIVATE GLEW_NO_GLU)
target_compile_definitions(glew PRIVATE GLEW_SMODE_FORWARD_COMPATIBLE_PATCH)
target_compile_definitions(glew PRIVATE GLEW_MX)

TARGET_LINK_LIBRARIES(glew ${OPENGL_LIBRARIES})

TARGET_INCLUDE_DIRECTORIES(glew PUBLIC include)
