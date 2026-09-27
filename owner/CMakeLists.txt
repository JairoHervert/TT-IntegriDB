add_executable(tt_owner main.cpp)
target_link_libraries(tt_owner PRIVATE tt_core)
target_compile_options(tt_owner PRIVATE ${TT_WARNINGS})
