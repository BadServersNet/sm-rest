cmake_minimum_required(VERSION 3.20)
project(
	sm-rest
	LANGUAGES
		C
		CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

set(SM_REST_VERSION "0.1.0" CACHE STRING "Extension version string")

set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps")
set(SOURCEMOD_DIR "${DEPS_DIR}/sourcemod")

# ---- zlib -------------------------------------------------------------------
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/zlib" EXCLUDE_FROM_ALL)
if(TARGET zlibstatic)
	set(SM_REST_ZLIB_TARGET zlibstatic)
else()
	set(SM_REST_ZLIB_TARGET zlib)
endif()
set(
	ZLIB_INCLUDE_DIR
	"${DEPS_DIR}/zlib"
	"${CMAKE_CURRENT_BINARY_DIR}/deps/zlib"
	CACHE PATH
	""
	FORCE
)
set(ZLIB_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/zlib/libz.a" CACHE FILEPATH "" FORCE)
set(ZLIB_FOUND ON CACHE BOOL "" FORCE)

# ---- mbedTLS ----------------------------------------------------------------
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/mbedtls" EXCLUDE_FROM_ALL)
set(MBEDTLS_INCLUDE_DIRS "${DEPS_DIR}/mbedtls/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)

# ---- curl -------------------------------------------------------------------
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB ON CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_ALTSVC ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_AWS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_COOKIES ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DOH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_HSTS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_HTTP_AUTH OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_BASIC_AUTH OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_BEARER_AUTH OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_DIGEST_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_KERBEROS_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_NEGOTIATE_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IPFS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MIME OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_NETRC ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_PROGRESS_METER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SHUFFLE_DNS ON CACHE BOOL "" FORCE)
set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
set(CURL_CA_FALLBACK OFF CACHE BOOL "" FORCE)
set(CURL_HIDDEN_SYMBOLS ON CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/curl" EXCLUDE_FROM_ALL)
add_dependencies(libcurl_static ${SM_REST_ZLIB_TARGET})

# ---- extension --------------------------------------------------------------
add_library(
	rest
	SHARED
	"${SOURCEMOD_DIR}/public/smsdk_ext.cpp"
	src/extension.cpp
	src/natives.cpp
	src/job.cpp
	src/transfer_thread.cpp
	src/main_thread_queue.cpp
	src/text.cpp
	src/uri.cpp
)

set_target_properties(
	rest
	PROPERTIES
		OUTPUT_NAME
			"rest.ext"
		PREFIX
			""
		SUFFIX
			".so"
)

target_include_directories(
	rest
	PRIVATE
		src
		"${SOURCEMOD_DIR}/public"
		"${SOURCEMOD_DIR}/public/amtl"
		"${SOURCEMOD_DIR}/public/amtl/amtl"
		"${SOURCEMOD_DIR}/sourcepawn/include"
		"${DEPS_DIR}/curl/include"
		"${DEPS_DIR}/mbedtls/include"
)

target_compile_definitions(
	rest
	PRIVATE
		CURL_STATICLIB
		SM_REST_VERSION="${SM_REST_VERSION}"
		stricmp=strcasecmp
		_stricmp=strcasecmp
		_snprintf=snprintf
		_vsnprintf=vsnprintf
		HAVE_STDINT_H
		GNUC
		_LINUX
		POSIX
)

target_compile_options(
	rest
	PRIVATE
		-fno-strict-aliasing
		-fno-rtti
		-Wall
		-Wno-unused
		-Wno-switch
		-Wno-non-virtual-dtor
		-Wno-overloaded-virtual
		-Wno-unknown-pragmas
		-Wno-class-memaccess
		-msse
)

target_link_libraries(
	rest
	PRIVATE
		libcurl_static
		mbedtls
		mbedx509
		mbedcrypto
		${SM_REST_ZLIB_TARGET}
		pthread
		dl
)

target_link_options(
	rest
	PRIVATE
		-static-libgcc
		-static-libstdc++
		-Wl,--no-undefined
)

# ---- CA bundle --------------------------------------------------------------
set(CA_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/ca-bundle.crt")
add_custom_command(
	OUTPUT
		"${CA_BUNDLE}"
	COMMAND
		perl "${DEPS_DIR}/curl/scripts/mk-ca-bundle.pl" -q "${CA_BUNDLE}"
	COMMENT "Generating CA bundle"
	VERBATIM
)
add_custom_target(ca_bundle ALL DEPENDS "${CA_BUNDLE}")

# ---- install / package ------------------------------------------------------
install(TARGETS rest LIBRARY DESTINATION addons/sourcemod/extensions)
install(FILES pawn/scripting/include/rest.inc DESTINATION addons/sourcemod/scripting/include)
install(FILES "${CA_BUNDLE}" DESTINATION addons/sourcemod/configs/rest)
