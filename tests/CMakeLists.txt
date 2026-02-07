add_executable(test_mp3tag test_mp3tag.c)
target_link_libraries(test_mp3tag PRIVATE mp3tag)
add_test(NAME test_mp3tag COMMAND test_mp3tag)
