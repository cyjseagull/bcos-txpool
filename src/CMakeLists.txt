file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(MESSAGES_PROTOS TxsSync.proto)
set(SYNC_PROTO_SUB_DIR "src/sync/protocol/proto")
set(SYNC_PROTO_GENERATE_DIR ${SYNC_PROTO_GENERATE_BASE_DIR}/${SYNC_PROTO_SUB_DIR})

include(GenerateSources)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${MESSAGES_PROTOS} ${PROTO_PATH} "sync/protocol/proto" ${SYNC_PROTO_GENERATE_BASE_DIR} ${SYNC_PROTO_GENERATE_DIR})

aux_source_directory(. SRC_LIST)
include_directories(.)

aux_source_directory(./txpool SRC_LIST)
include_directories(./txpool)
aux_source_directory(./txpool/storage SRC_LIST)
include_directories(./txpool/storage)
aux_source_directory(./txpool/validator SRC_LIST)
include_directories(./txpool/validator)

aux_source_directory(./sync/protocol SRC_LIST)
include_directories(./sync/protocol)

add_library(${BCOS_TXPOOL_TARGET} ${SRC_LIST} ${PROTO_SRCS} ${HEADERS} ${PROTO_HDRS})

target_compile_options(${BCOS_TXPOOL_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${BCOS_TXPOOL_TARGET} PUBLIC bcos-framework::utilities bcos-framework::protocol tbb::tbb)
