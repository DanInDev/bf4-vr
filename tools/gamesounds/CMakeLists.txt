# The game-file sound reader (tools/gamesounds/gamesounds.cpp), built on its own: it is a host tool, not part of the mod
# presets. tools/game_cues.py builds it when missing:
#   cmake -S tools/gamesounds -B build/gamesounds -A x64 && cmake --build build/gamesounds --config Release
cmake_minimum_required(VERSION 3.20)
project(gamesounds CXX)
set(CMAKE_CXX_STANDARD 20)
add_executable(gamesounds gamesounds.cpp)
target_compile_definitions(gamesounds PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(gamesounds PRIVATE /O2 /EHsc /utf-8)
