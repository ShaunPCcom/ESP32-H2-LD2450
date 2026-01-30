idf_component_register(
  SRCS "ld2450.c" "ld2450_parser.c" "ld2450_zone.c"
  INCLUDE_DIRS "include"
  REQUIRES driver freertos esp_timer log
)

