add_library(jobpilot_bcrypt STATIC
    bcrypt.c
    crypt_blowfish.c
    crypt_gensalt.c
    wrapper.c
)

target_include_directories(jobpilot_bcrypt
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(jobpilot_bcrypt PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
