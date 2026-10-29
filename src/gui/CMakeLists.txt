# ui/ is the platform-neutral half -- palette, metrics, layout, painting and
# hit-testing. It compiles unchanged against any renderer, which is what lets a
# second front end reuse the design rather than reimplement it. Built on every
# platform, including ones with no GUI yet, so it cannot quietly grow a
# dependency on one of them.
add_library(seicure_ui STATIC
    ui/theme.cpp
    ui/view.cpp
)
target_include_directories(seicure_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(seicure_ui PUBLIC seicure_job seicure_platform
                      PRIVATE seicure_flags)

if(NOT SEICURE_BUILD_GUI)
    return()
endif()

if(WIN32)
    add_executable(seicure_gui WIN32
        main.cpp
        app_window.cpp
        win32/canvas_gdi.cpp
        app.rc
    )
    target_link_libraries(seicure_gui PRIVATE
        seicure_ui
        seicure_job
        comctl32 shell32 comdlg32 gdi32 user32
    )

    # The .rc pulls app.manifest from this directory and seicure.ico from
    # assets/. OBJECT_DEPENDS is required: the RC dependency scanner does not
    # follow ICON or manifest references, so without it a regenerated icon is
    # silently ignored and the old one stays embedded.
    set_source_files_properties(app.rc PROPERTIES
        COMPILE_FLAGS "-I\"${CMAKE_CURRENT_SOURCE_DIR}\" -I\"${CMAKE_SOURCE_DIR}/assets\""
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/seicure.ico;${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")

    if(NOT MSVC)
        target_compile_options(seicure_gui PRIVATE -municode)
        target_link_options(seicure_gui PRIVATE -municode)
    endif()

    install(TARGETS seicure_gui RUNTIME DESTINATION .)

elseif(APPLE)
    # Built from the same drawing code as the .ico by tools/make_icon.ps1, then
    # packed with iconutil, which exists only on macOS -- hence a checked-in
    # .icns rather than a build step.
    set(SEICURE_ICNS "${CMAKE_SOURCE_DIR}/assets/Seicure.icns")
    set_source_files_properties(${SEICURE_ICNS} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)

    # MACOSX_BUNDLE, because Gatekeeper only staples notarisation tickets to
    # bundles, and because a bare executable gets no Dock icon and no menu bar.
    add_executable(seicure_gui MACOSX_BUNDLE
        mac/main.mm
        mac/app_window.mm
        mac/canvas_cg.mm
        ${SEICURE_ICNS}
    )
    target_link_libraries(seicure_gui PRIVATE seicure_ui seicure_job)
    target_link_libraries(seicure_gui PRIVATE
        "-framework Cocoa"
        "-framework CoreText"
        "-framework CoreGraphics"
    )
    # ARC would fight the C++ ownership already in place here, and the Objective-C
    # side of this front end is three long-lived objects that never get released.
    target_compile_options(seicure_gui PRIVATE -fno-objc-arc)

    set_target_properties(seicure_gui PROPERTIES
        OUTPUT_NAME "Seicure"
        MACOSX_BUNDLE_BUNDLE_NAME "Seicure"
        MACOSX_BUNDLE_ICON_FILE "Seicure.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.seizia.seicure"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "SEIZIA")

    install(TARGETS seicure_gui BUNDLE DESTINATION .)
endif()

target_include_directories(seicure_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
