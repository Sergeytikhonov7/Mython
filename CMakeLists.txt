cmake_minimum_required(VERSION 3.19)
project(Diplom1_3_1)

set(CMAKE_CXX_STANDARD 17)

add_executable(Diplom1_3_1 main.cpp lexer.cpp lexer.h lexer_test_open.cpp parse.cpp parse.h parse_test.cpp runtime.cpp runtime.h test_runner_p.h runtime_test.cpp statement.cpp statement.h statement_test.cpp)