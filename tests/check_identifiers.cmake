# No personal name, old project name or model identifier in the sources
# (0.7 cmd/gui-wails/identifiers_test.go; the "no model names" rule, D-120).
# Identifiers travel into the binary, the file properties and the "creator"
# field of every saved script, so one new line is enough to leak them.
# Run: cmake -DROOT=<repo> -P tests/check_identifiers.cmake
if(NOT ROOT)
  message(FATAL_ERROR "ROOT not set")
endif()

# The personal name as character codes, so it is not written here either.
string(ASCII 99 104 114 105 115 116 105 97 110 personal_name)
set(forbidden_words "${personal_name}" "funscript-player" "gui-wails.exe" "todo-github")
# Versioned model identifiers ("Claude" and "Grok" as agent and branch names are fine, D-114).
set(forbidden_regex "claude-(opus|sonnet|haiku|fable)|(opus|sonnet|haiku|fable) [0-9]+\\.[0-9]|gpt-[0-9]")

set(globs)
foreach(dir app core tests docs native .github)
  foreach(ext cpp h hpp qml js py md json yml yaml txt cmake html css)
    list(APPEND globs "${ROOT}/${dir}/*.${ext}")
  endforeach()
endforeach()
file(GLOB_RECURSE files ${globs})
list(APPEND files "${ROOT}/CMakeLists.txt" "${ROOT}/README.md" "${ROOT}/CONTRIBUTING.md")

set(hits)
foreach(f IN LISTS files)
  if(f MATCHES "/(build[^/]*|__pycache__|node_modules)/" OR f MATCHES "check_identifiers\\.cmake$")
    continue()
  endif()
  file(READ "${f}" text)
  string(TOLOWER "${text}" text)
  foreach(w IN LISTS forbidden_words)
    string(FIND "${text}" "${w}" pos)
    if(NOT pos EQUAL -1)
      list(APPEND hits "${f}: forbidden word")
    endif()
  endforeach()
  if(text MATCHES "${forbidden_regex}")
    list(APPEND hits "${f}: model identifier \"${CMAKE_MATCH_0}\"")
  endif()
endforeach()

list(LENGTH files n)
if(hits)
  list(JOIN hits "\n  " out)
  message(FATAL_ERROR "Forbidden identifiers:\n  ${out}")
endif()
message(STATUS "No forbidden identifiers in ${n} files")
