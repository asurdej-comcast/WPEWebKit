list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES
    "${WEBCORE_DIR}/platform/firebolt"
    "${WEBCORE_DIR}/platform/firebolt/soup"
)

# Not unified: the libsoup/GLib transport and the file-local helpers of the other
# sources must not share a translation unit.
list(APPEND WebCore_SOURCES
    platform/firebolt/FireboltClient.cpp
    platform/firebolt/FireboltDeviceService.cpp
    platform/firebolt/FireboltMetricsService.cpp
    platform/firebolt/FireboltObject.cpp

    platform/firebolt/soup/FireboltSoupTransport.cpp
)

list(APPEND WebCore_PRIVATE_FRAMEWORK_HEADERS
    platform/firebolt/FireboltClient.h
    platform/firebolt/FireboltDevice.h
    platform/firebolt/FireboltDeviceService.h
    platform/firebolt/FireboltMetrics.h
    platform/firebolt/FireboltManager.h
    platform/firebolt/FireboltMetricsService.h
    platform/firebolt/FireboltModel.h
    platform/firebolt/FireboltObject.h
    platform/firebolt/FireboltOperation.h
    platform/firebolt/FireboltRuntime.h
    platform/firebolt/FireboltSubscription.h
    platform/firebolt/FireboltTypes.h
    platform/firebolt/IFireboltTransport.h
    platform/firebolt/OperationState.h

    platform/firebolt/soup/FireboltSoupTransport.h
)

list(APPEND WebCore_SYSTEM_INCLUDE_DIRECTORIES
    ${LIBSOUP_INCLUDE_DIRS}
)

list(APPEND WebCore_LIBRARIES
    ${LIBSOUP_LIBRARIES}
)
