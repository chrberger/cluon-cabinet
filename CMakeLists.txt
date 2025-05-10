#
# Copyright (C) 2021,2022  Christian Berger
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
# 

cmake_minimum_required(VERSION 3.5)

project(cluon-cabinet)

################################################################################
# Defining the relevant version of libcluon.
set(CLUON_COMPLETE cluon-complete-v0.0.140.hpp)
set(DB_MESSAGE_SET db.odvd)
set(OPENDLV_MESSAGE_SET opendlv-standard-message-set-v0.9.9.odvd)
set(CPP_HTTPLIB "${CMAKE_CURRENT_SOURCE_DIR}/cpp-httplib-0.20.1")
set(JSONLIB "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann")

################################################################################
# Set the search path for .cmake files.
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" ${CMAKE_MODULE_PATH})

################################################################################
# This project requires C++14 or newer.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
IF(NOT(APPLE))
  # Build a static binary.
  set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
ENDIF()

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
  set(CMAKE_CXX_FLAGS "-D_DARWIN_C_SOURCE=1 ")
  set(CMAKE_OSX_SYSROOT /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk)
endif()

# Add further warning levels.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
  -D_XOPEN_SOURCE=700 \
  -D_FORTIFY_SOURCE=2 \
  -O2 \
  -fstack-protector \
  -fomit-frame-pointer \
  -pipe \
  -Weffc++ \
  -Wall -Wextra -Wshadow -Wdeprecated \
  -Wdiv-by-zero -Wfloat-equal -Wfloat-conversion -Wsign-compare -Wpointer-arith \
  -Wuninitialized -Wunreachable-code \
  -Wunused -Wunused-function -Wunused-label -Wunused-parameter \
  -Wunused-value -Wunused-variable -Wunused-result \
  -Wmissing-field-initializers -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn")

SET(lmdb_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lmdb/libraries/liblmdb")
INCLUDE_DIRECTORIES (${lmdb_SOURCE_DIR})
SET (lmdb-sources
  ${lmdb_SOURCE_DIR}/mdb.c
  ${lmdb_SOURCE_DIR}/midl.c
)
# Set compile flags for lmdb.
IF(UNIX)
  set(LMDB_COMPILE_FLAGS "-W -Wall -Wno-unused-parameter -Wbad-function-cast -Wuninitialized -O2")
  SET_SOURCE_FILES_PROPERTIES(${lmdb_SOURCE_DIR}/mdb.c PROPERTIES COMPILE_FLAGS "${LMDB_COMPILE_FLAGS}")
  SET_SOURCE_FILES_PROPERTIES(${lmdb_SOURCE_DIR}/midl.c PROPERTIES COMPILE_FLAGS "${LMDB_COMPILE_FLAGS}")
ENDIF()

SET(xxhash_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xxhash")
INCLUDE_DIRECTORIES (${xxhash_SOURCE_DIR})
SET (xxhash-sources
  ${xxhash_SOURCE_DIR}/xxhash.c
)
# Set compile flags for xxhash.
IF(UNIX)
  set(XXHASH_COMPILE_FLAGS "-Wall -Wextra -Wconversion -Wcast-qual -Wcast-align -Wshadow \
                            -Wstrict-aliasing=1 -Wswitch-enum -Wdeclaration-after-statement \
                            -Wstrict-prototypes -Wundef -Wpointer-arith -Wformat-security \
                            -Wvla -Wformat=2 -Winit-self -Wfloat-equal -Wwrite-strings \
                            -Wredundant-decls -Wstrict-overflow=2")
  SET_SOURCE_FILES_PROPERTIES(${xxhash_SOURCE_DIR}/xxhash.c PROPERTIES COMPILE_FLAGS "${XXHASH_COMPILE_FLAGS}")
ENDIF()

SET(lz4_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lz4/lib")
INCLUDE_DIRECTORIES (${lz4_SOURCE_DIR})
SET (lz4-sources
  ${lz4_SOURCE_DIR}/lz4.c
  ${lz4_SOURCE_DIR}/lz4frame.c
  ${lz4_SOURCE_DIR}/lz4hc.c
  ${lz4_SOURCE_DIR}/xxhash.c
)
# Set compile flags for lz4.
IF(UNIX)
  set(LZ4_COMPILE_FLAGS "-Wall -Wextra -Wcast-qual -Wcast-align -Wshadow \
                         -Wswitch-enum -Wdeclaration-after-statement -Wstrict-prototypes \
                         -Wundef -Wpointer-arith -Wstrict-aliasing=1 \
                         -DXXH_NAMESPACE=LZ4_ -O3")
  SET_SOURCE_FILES_PROPERTIES(${lz4_SOURCE_DIR}/lz4.c PROPERTIES COMPILE_FLAGS "${LZ4_COMPILE_FLAGS}")
  SET_SOURCE_FILES_PROPERTIES(${lz4_SOURCE_DIR}/lz4frame.c PROPERTIES COMPILE_FLAGS "${LZ4_COMPILE_FLAGS}")
  SET_SOURCE_FILES_PROPERTIES(${lz4_SOURCE_DIR}/lz4hc.c PROPERTIES COMPILE_FLAGS "${LZ4_COMPILE_FLAGS}")
  SET_SOURCE_FILES_PROPERTIES(${lz4_SOURCE_DIR}/xxhash.c PROPERTIES COMPILE_FLAGS "${LZ4_COMPILE_FLAGS}")
ENDIF()

# Threads are necessary for linking the resulting binaries as UDPReceiver is running in parallel.
# assume built-in pthreads on MacOS
set(THREADS_PREFER_PTHREAD_FLAG ON)
IF(APPLE)
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
ELSE()
    find_package(Threads REQUIRED)
    set(LIBRARIES ${LIBRARIES} Threads::Threads)
ENDIF()

if(UNIX)
  if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
    find_package(LibRT REQUIRED)
    set(LIBRARIES ${LIBRARIES} ${LIBRT_LIBRARIES})
    include_directories(SYSTEM ${LIBRT_INCLUDE_DIR})
  endif()
endif()

################################################################################
# Create symbolic link to cluon-complete.hpp.
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/cluon-complete.hpp
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/src/${CLUON_COMPLETE} ${CMAKE_BINARY_DIR}/cluon-complete.hpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${CLUON_COMPLETE})

# Add current build directory as include directory as it contains generated files.
include_directories(SYSTEM ${CMAKE_BINARY_DIR})

################################################################################
# Extract cluon-msc from cluon-complete.hpp.
IF(APPLE)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/cluon-msc
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/src/${CLUON_COMPLETE} ${CMAKE_BINARY_DIR}/cluon-complete.hpp
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${CMAKE_BINARY_DIR}/cluon-complete.cpp
      COMMAND ${CMAKE_CXX_COMPILER} -o ${CMAKE_BINARY_DIR}/cluon-msc ${CMAKE_BINARY_DIR}/cluon-complete.cpp -isysroot ${CMAKE_OSX_SYSROOT} -std=c++14 -pthread -D_DARWIN_C_SOURCE=1 -D HAVE_CLUON_MSC
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${CLUON_COMPLETE})
ELSE()
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/cluon-msc
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/src/${CLUON_COMPLETE} ${CMAKE_BINARY_DIR}/cluon-complete.hpp
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${CMAKE_BINARY_DIR}/cluon-complete.cpp
      COMMAND ${CMAKE_CXX_COMPILER} -o ${CMAKE_BINARY_DIR}/cluon-msc ${CMAKE_BINARY_DIR}/cluon-complete.cpp -std=c++14 -pthread -D HAVE_CLUON_MSC
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${CLUON_COMPLETE})
ENDIF()

################################################################################
# Generate opendlv-standard-message-set.hpp
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/opendlv-standard-message-set.hpp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_BINARY_DIR}/cluon-msc --cpp --out=${CMAKE_BINARY_DIR}/opendlv-standard-message-set.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/${OPENDLV_MESSAGE_SET}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${OPENDLV_MESSAGE_SET} ${CMAKE_BINARY_DIR}/cluon-msc)

################################################################################
# Generate db.hpp.
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/db.hpp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_BINARY_DIR}/cluon-msc --cpp --out=${CMAKE_BINARY_DIR}/db.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/${DB_MESSAGE_SET}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${DB_MESSAGE_SET} ${CMAKE_BINARY_DIR}/cluon-msc)

################################################################################
# Gather all 3rd party libraries.
add_library(${CLUON-CABINET-3RDPARTIES}-objects OBJECT ${lmdb-sources} ${xxhash-sources} ${lz4-sources})
set_target_properties(${CLUON-CABINET-3RDPARTIES}-objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(cluon-cabinet-3rdparties-static STATIC $<TARGET_OBJECTS:${CLUON-CABINET-3RDPARTIES}-objects>)
set(LIBRARIES ${LIBRARIES} cluon-cabinet-3rdparties-static)

################################################################################
# Create executables.
set(GENERATED_HEADERS ${HEADERS} ${CMAKE_BINARY_DIR}/opendlv-standard-message-set.hpp ${CMAKE_BINARY_DIR}/db.hpp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(rec2cabinet ${CMAKE_CURRENT_SOURCE_DIR}/src/rec2cabinet.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/rec2cabinet.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(rec2cabinet ${LIBRARIES})

add_executable(rec2cabinet2 ${CMAKE_CURRENT_SOURCE_DIR}/src/rec2cabinet2.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/rec2cabinet2.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(rec2cabinet2 ${LIBRARIES})

add_executable(cabinet2rec ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet2rec.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet2rec ${LIBRARIES})

add_executable(cabinet-stream ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-stream.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-stream ${LIBRARIES})

add_executable(cabinet-ls ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-ls.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-ls ${LIBRARIES})

add_executable(cabinet-overview ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-overview.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-overview ${LIBRARIES})

add_executable(cabinet-WGS84toMorton ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-WGS84toMorton.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-WGS84toMorton ${LIBRARIES})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CPP_HTTPLIB} ${JSONLIB})
add_executable(cabinet-dump ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-dump.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-dump ${LIBRARIES})

add_executable(cabinet-serve ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-serve.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-serve ${LIBRARIES})

add_executable(cabinet-acceltoMorton ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-acceltoMorton.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-acceltoMorton ${LIBRARIES})

add_executable(cabinet-WGS84toTrips ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-WGS84toTrips.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-WGS84toTrips ${LIBRARIES})

add_executable(cabinet-query ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-query.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-query ${LIBRARIES})

add_executable(cabinet-query-maneuver ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-query-maneuver.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-query-maneuver ${LIBRARIES})

add_executable(cabinet-query-maneuver-brute-force ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-query-maneuver-brute-force.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-query-maneuver-brute-force ${LIBRARIES})

add_executable(cabinet-query-maneuver-brute-force-primitive ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-query-maneuver-brute-force-primitive.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-query-maneuver-brute-force-primitive ${LIBRARIES})

add_executable(cabinet-DumpTrips ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-DumpTrips.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-DumpTrips ${LIBRARIES})

add_executable(cabinet-SFC-query-experiment ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-SFC-query-experiment.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-SFC-query-experiment ${LIBRARIES})

add_executable(cabinet-SFC-query ${CMAKE_CURRENT_SOURCE_DIR}/src/cabinet-SFC-query.cpp ${CMAKE_BINARY_DIR}/cluon-complete.hpp ${GENERATED_HEADERS})
target_link_libraries(cabinet-SFC-query ${LIBRARIES})

################################################################################
enable_testing()
add_executable(key-runner ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-key.cpp ${GENERATED_HEADERS})
target_link_libraries(key-runner ${LIBRARIES})
add_test(NAME key-runner COMMAND key-runner)

add_executable(morton-runner ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-morton.cpp ${GENERATED_HEADERS})
target_link_libraries(morton-runner ${LIBRARIES})
add_test(NAME morton-runner COMMAND morton-runner)

add_executable(rec2cabinet-runner ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-rec2cabinet.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/rec2cabinet.hpp ${GENERATED_HEADERS})
target_link_libraries(rec2cabinet-runner ${LIBRARIES})
add_test(NAME rec2cabinet-runner COMMAND rec2cabinet-runner)

add_executable(rec2cabinet2-runner ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-rec2cabinet2.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/rec2cabinet2.hpp ${GENERATED_HEADERS})
target_link_libraries(rec2cabinet2-runner ${LIBRARIES})
add_test(NAME rec2cabinet2-runner COMMAND rec2cabinet2-runner)

add_executable(in-ranges-runner ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test/tests-in-ranges.cpp ${GENERATED_HEADERS})
target_link_libraries(in-ranges-runner ${LIBRARIES})
add_test(NAME in-ranges-runner COMMAND in-ranges-runner)

################################################################################
install(TARGETS rec2cabinet DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS rec2cabinet2 DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet2rec DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-stream DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-ls DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-overview DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-WGS84toMorton DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-dump DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-serve DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-acceltoMorton DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-WGS84toTrips DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-query DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-query-maneuver DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-DumpTrips DESTINATION bin COMPONENT ${PROJECT_NAME})
install(TARGETS cabinet-SFC-query DESTINATION bin COMPONENT ${PROJECT_NAME})
