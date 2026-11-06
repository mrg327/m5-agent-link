set(PROTO_SRCS
    src/agentlink_codec_decode.c src/agentlink_codec_encode.c
    src/zcbor_common.c src/zcbor_decode.c src/zcbor_encode.c src/zcbor_print.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${PROTO_SRCS} INCLUDE_DIRS include)
else()
    add_library(agentlink_proto STATIC ${PROTO_SRCS})
    target_include_directories(agentlink_proto PUBLIC include)
endif()

# Definite-length maps/lists on encode (the daemon's CBOR libraries expect them). Decoding stays
# lenient on purpose, so only the encoder is built canonical.
set_source_files_properties(src/zcbor_encode.c PROPERTIES COMPILE_DEFINITIONS ZCBOR_CANONICAL)
