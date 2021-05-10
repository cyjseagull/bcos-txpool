file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")
aux_source_directory(. SRC_LIST)
include_directories(.)

aux_source_directory(./storage SRC_LIST)
include_directories(./storage)

aux_source_directory(./validator SRC_LIST)
include_directories(./validator)

add_library(${BCOS_TXPOOL_TARGET} ${SRC_LIST} ${PROTO_SRCS} ${HEADERS} ${PROTO_HDRS})

target_compile_options(${BCOS_TXPOOL_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${BCOS_TXPOOL_TARGET} PUBLIC bcos-framework::utilities bcos-framework::protocol tbb::tbb)
