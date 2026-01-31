idf_component_register(
    SRCS "ld2450_zb_h2.c" "ld2450_cli.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash
    REQUIRES ld2450

)

