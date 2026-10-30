#Build main launcher executable file
set(SOURCES
  launcher.c
  launcher.h
  util.c
  util.h
  image.c
  image.h
  debug.c
  debug.h
  clock.c
  clock.h
  gl.c
  gl.h
  vmath.h
  spring.h
  raster.c
  raster.h
  hdr.c
  hdr.h
  text.c
  text.h
  bookart.c
  bookart.h
  render.c
  render.h
  shelf.c
  shelf.h
  overlay.c
  overlay.h
)
if (UNIX AND NOT INIH_FOUND)
  list(APPEND SOURCES external/inih/ini.c)
endif ()
if (UNIX)
  add_executable(${EXECUTABLE_TITLE} ${SOURCES})
endif ()
if (WIN32)
  set(APP_ICON_RESOURCE_WINDOWS "${PROJECT_SOURCE_DIR}/config/${EXECUTABLE_TITLE}.rc")
  set(MANIFEST_FILE "${PROJECT_BINARY_DIR}/${EXECUTABLE_TITLE}.manifest")
  add_executable(${EXECUTABLE_TITLE} WIN32 ${SOURCES} ${MANIFEST_FILE} ${APP_ICON_RESOURCE_WINDOWS})
  set_property(TARGET ${EXECUTABLE_TITLE} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
endif()

# Build libraries
add_subdirectory("platform")

# Link libraries together 
target_link_libraries(${EXECUTABLE_TITLE} platform)
if (UNIX)
  target_link_libraries(${EXECUTABLE_TITLE} PkgConfig::SDL2 PkgConfig::SDL2_IMAGE PkgConfig::SDL2_TTF m)
  if (INIH_FOUND)
    target_link_libraries(${EXECUTABLE_TITLE} PkgConfig::INIH)
  else ()
    target_include_directories(${EXECUTABLE_TITLE} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external/inih")
  endif ()
else ()
  target_link_libraries(${EXECUTABLE_TITLE} 
  $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
  $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
	$<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
	$<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
	${INIH}
  ${GETOPT}
	PowrProf
  )
  target_include_directories(${EXECUTABLE_TITLE} PUBLIC ${INIH_INCLUDE_DIR} ${GETOPT_INCLUDE_DIR})
endif ()
target_include_directories(${EXECUTABLE_TITLE} SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external")
