function(add_tiny_video_test target)
    add_executable(${target} ${ARGN})

    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/Lib/TinyLogLibrary/include
    )

    target_link_libraries(${target} PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
    )

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 15)
endfunction()

add_tiny_video_test(test_sync_queue
    test_sync_queue.cpp
    ${PROJECT_SOURCE_DIR}/src/core/queue.h
)

add_tiny_video_test(test_video_frame_scheduler
    test_video_frame_scheduler.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/sync/videoframescheduler.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/sync/videoframescheduler.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/sync/syncmanager.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/sync/syncmanager.h
)
target_link_libraries(test_video_frame_scheduler PRIVATE
    tiny_log
    ${FFMPEG_LIB_DIR}/avutil.lib
)

add_tiny_video_test(test_playlist_manager
    test_playlist_manager.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/PlaylistManager.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/PlaylistManager.h
)

add_tiny_video_test(test_vision_config_loader
    test_vision_config_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/visionconfigloader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/visionconfigloader.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/visionpipelineconfig.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/visionpipelineconfig.h
)
target_link_libraries(test_vision_config_loader PRIVATE tiny_log)

add_tiny_video_test(test_vision_pipeline_manager
    test_vision_pipeline_manager.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/visionpipelinemanager.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/visionpipelinemanager.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/ivisionpipeline.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/vision/visiontypes.h
)
target_link_libraries(test_vision_pipeline_manager PRIVATE tiny_log)
