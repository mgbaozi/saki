idf_component_register(
    SRCS "test_saki_protocol.c" "test_sessions.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES unity saki_protocol
)
