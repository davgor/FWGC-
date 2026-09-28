# How FWGC++ fetches what it does not hold: every file named in dependencies.json, beside this one,
# and nothing else. A build calls fwgc_dependency(<name>) where it would call FetchContent_Declare;
# the file comes from a shared cache, checked by its hash, and is downloaded into the cache only
# when it is not there. Every build folder on a machine shares the one download, and none skips the
# hash (a hand-set FETCHCONTENT_SOURCE_DIR_<NAME> would).
#
# The cache: $FWGC_DEPS_CACHE; else %LOCALAPPDATA%/fwgc/deps on Windows; else
# $XDG_CACHE_HOME/fwgc/deps, else ~/.cache/fwgc/deps.
#
# Run as a script, it fetches for a new machine:
#   cmake -P dependencies.cmake                   every build dependency, into the cache
#   cmake -DWITH=models -P dependencies.cmake     and every model, to its place under inference/models
#   cmake -DONLY=<name> -P dependencies.cmake     one entry
#   cmake -DVERIFY=ON -P dependencies.cmake       check what is there by its hash; fetch nothing
# Python packages install from their hash-locked requirements file (pip --require-hashes), and an
# installer by hand: the script says which.

include_guard(GLOBAL)
if(NOT CMAKE_SCRIPT_MODE_FILE)
  include(FetchContent)
endif()

set(FWGC_DEPENDENCIES_JSON "${CMAKE_CURRENT_LIST_DIR}/dependencies.json")
set(FWGC_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}")

# Where fetched files are kept.
function(_fwgc_dependency_cache out)
  if(DEFINED ENV{FWGC_DEPS_CACHE} AND NOT "$ENV{FWGC_DEPS_CACHE}" STREQUAL "")
    set(dir "$ENV{FWGC_DEPS_CACHE}")
  elseif(DEFINED ENV{LOCALAPPDATA} AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
    set(dir "$ENV{LOCALAPPDATA}/fwgc/deps")
  elseif(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
    set(dir "$ENV{XDG_CACHE_HOME}/fwgc/deps")
  else()
    set(dir "$ENV{HOME}/.cache/fwgc/deps")
  endif()
  file(TO_CMAKE_PATH "${dir}" dir)
  set(${out} "${dir}" PARENT_SCOPE)
endfunction()

# The manifest's entries, by index.
function(_fwgc_dependency_count out)
  file(READ "${FWGC_DEPENDENCIES_JSON}" json)
  string(JSON count LENGTH "${json}" dependencies)
  set(${out} ${count} PARENT_SCOPE)
endfunction()

# One field of entry `index` (empty when the entry has none).
function(_fwgc_dependency_field index key out)
  file(READ "${FWGC_DEPENDENCIES_JSON}" json)
  string(JSON value ERROR_VARIABLE missing GET "${json}" dependencies ${index} ${key})
  if(missing)
    set(value "")
  endif()
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

# The index of the entry named `name`; a fatal error when there is none.
function(_fwgc_dependency_index name out)
  _fwgc_dependency_count(count)
  math(EXPR last "${count} - 1")
  foreach(i RANGE ${last})
    _fwgc_dependency_field(${i} name entry)
    if(entry STREQUAL name)
      set(${out} ${i} PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "dependencies.json names no dependency '${name}'. A new one is added there, with the "
                      "owner's approval of the file, its source and its size, before a build fetches it.")
endfunction()

# Whether `path` holds exactly the bytes `hash` ("SHA256=...", "SHA3_256=...") names.
function(_fwgc_dependency_matches path hash out)
  set(${out} FALSE PARENT_SCOPE)
  if(NOT EXISTS "${path}")
    return()
  endif()
  string(FIND "${hash}" "=" at)
  string(SUBSTRING "${hash}" 0 ${at} algorithm)
  math(EXPR from "${at} + 1")
  string(SUBSTRING "${hash}" ${from} -1 expected)
  string(TOLOWER "${expected}" expected)
  file(${algorithm} "${path}" actual)
  if(actual STREQUAL expected)
    set(${out} TRUE PARENT_SCOPE)
  endif()
endfunction()

# Makes sure `path` holds the file at `url` with `hash`, downloading it when it is missing or
# wrong; a fatal error when the download fails or its bytes are not those.
function(_fwgc_dependency_fetch_file url path hash)
  _fwgc_dependency_matches("${path}" "${hash}" good)
  if(good)
    return()
  endif()
  if(EXISTS "${path}")
    message(WARNING "${path} is not the file dependencies.json names (${hash}); fetching it again.")
    file(REMOVE "${path}")
  endif()
  get_filename_component(dir "${path}" DIRECTORY)
  file(MAKE_DIRECTORY "${dir}")
  message(STATUS "Fetching ${url}")
  file(DOWNLOAD "${url}" "${path}.part" EXPECTED_HASH ${hash} TLS_VERIFY ON STATUS status)
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    list(GET status 1 reason)
    file(REMOVE "${path}.part")
    message(FATAL_ERROR "Could not fetch ${url}: ${reason}")
  endif()
  file(RENAME "${path}.part" "${path}")
endfunction()

# A build's one way to fetch: FetchContent_Declare(<name> ...) from the cache, its hash checked on
# the way in and again as FetchContent unpacks it. Arguments after the name go to
# FetchContent_Declare (SYSTEM, say). The caller then calls FetchContent_MakeAvailable(<name>).
function(fwgc_dependency name)
  _fwgc_dependency_index(${name} index)
  _fwgc_dependency_field(${index} fetch fetch)
  if(NOT fetch STREQUAL "build")
    message(FATAL_ERROR "dependencies.json's '${name}' is fetched by '${fetch}', not by a build.")
  endif()
  _fwgc_dependency_field(${index} url url)
  _fwgc_dependency_field(${index} file file)
  _fwgc_dependency_field(${index} hash hash)
  string(TOUPPER "${name}" upper)
  # FetchContent keeps an empty FETCHCONTENT_SOURCE_DIR_<NAME> in the cache itself; only a set one
  # replaces the fetch.
  if(NOT "${FETCHCONTENT_SOURCE_DIR_${upper}}" STREQUAL "")
    message(WARNING "FETCHCONTENT_SOURCE_DIR_${upper} is set, so '${name}' is built from "
                    "${FETCHCONTENT_SOURCE_DIR_${upper}} as it stands, and its hash is not checked. The shared "
                    "cache makes the setting unnecessary: clear it.")
  endif()
  _fwgc_dependency_cache(cache)
  _fwgc_dependency_fetch_file("${url}" "${cache}/${file}" "${hash}")
  FetchContent_Declare(${name} URL "${cache}/${file}" URL_HASH ${hash} DOWNLOAD_EXTRACT_TIMESTAMP TRUE ${ARGN})
endfunction()

# ---- run as a script: fetch for a new machine ----------------------------------------------------
if(CMAKE_SCRIPT_MODE_FILE AND CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(NOT DEFINED WITH)
    set(WITH "build")
  endif()
  _fwgc_dependency_cache(cache)
  _fwgc_dependency_count(count)
  math(EXPR last "${count} - 1")
  set(problems 0)
  foreach(i RANGE ${last})
    _fwgc_dependency_field(${i} name name)
    _fwgc_dependency_field(${i} fetch fetch)
    if(DEFINED ONLY AND NOT ONLY STREQUAL name)
      continue()
    endif()
    if(fetch STREQUAL "pip")
      _fwgc_dependency_field(${i} lock lock)
      message(STATUS "${name}: pip installs it: python -m pip install --require-hashes -r ${lock}")
      continue()
    endif()
    if(fetch STREQUAL "manual")
      _fwgc_dependency_field(${i} url url)
      _fwgc_dependency_field(${i} version version)
      message(STATUS "${name}: installed by hand: ${version}, from ${url}")
      continue()
    endif()
    if(fetch STREQUAL "tool" AND NOT (WITH STREQUAL "models" OR WITH STREQUAL "all") AND NOT DEFINED ONLY)
      message(STATUS "${name}: a model; -DWITH=models fetches it")
      continue()
    endif()
    _fwgc_dependency_field(${i} url url)
    _fwgc_dependency_field(${i} hash hash)
    _fwgc_dependency_field(${i} file file)
    _fwgc_dependency_field(${i} dest dest)
    # Where it goes: a build dependency to the cache, a model to its place in the tree.
    set(targets "")
    if(fetch STREQUAL "build")
      list(APPEND targets "${url}|${cache}/${file}|${hash}")
    else()
      file(READ "${FWGC_DEPENDENCIES_JSON}" json)
      string(JSON files ERROR_VARIABLE single GET "${json}" dependencies ${i} files)
      if(single)
        list(APPEND targets "${url}|${FWGC_REPOSITORY_ROOT}/${dest}|${hash}")
      else()
        string(JSON n LENGTH "${json}" dependencies ${i} files)
        math(EXPR n_last "${n} - 1")
        foreach(j RANGE ${n_last})
          string(JSON part GET "${json}" dependencies ${i} files ${j} file)
          string(JSON part_hash GET "${json}" dependencies ${i} files ${j} hash)
          list(APPEND targets "${url}${part}|${FWGC_REPOSITORY_ROOT}/${dest}/${part}|${part_hash}")
        endforeach()
      endif()
    endif()
    foreach(target IN LISTS targets)
      string(REPLACE "|" ";" parts "${target}")
      list(GET parts 0 from)
      list(GET parts 1 path)
      list(GET parts 2 want)
      if(VERIFY)
        _fwgc_dependency_matches("${path}" "${want}" good)
        if(good)
          message(STATUS "${name}: ${path}: ok")
        else()
          message(STATUS "${name}: ${path}: MISSING OR WRONG")
          math(EXPR problems "${problems} + 1")
        endif()
      else()
        _fwgc_dependency_fetch_file("${from}" "${path}" "${want}")
        message(STATUS "${name}: ${path}: ok")
      endif()
    endforeach()
  endforeach()
  if(problems GREATER 0)
    message(FATAL_ERROR "${problems} file(s) missing or not the bytes dependencies.json names")
  endif()
endif()
