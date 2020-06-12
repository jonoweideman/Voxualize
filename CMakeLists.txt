cmake_minimum_required(VERSION 3.3 FATAL_ERROR)

project(Voxualize)

find_package(VTK COMPONENTS
        vtkCommonCore
        vtkCommonDataModel
        vtkIOXML
        vtkImagingCore
        vtkImagingHybrid
        vtkInteractionStyle
        vtkRenderingCore
        vtkRenderingFreeType
        vtkRenderingOpenGL2
        vtkRenderingVolumeOpenGL2 QUIET)
if (NOT VTK_FOUND)
    message("Skipping Voxualize: ${VTK_NOT_FOUND_MESSAGE}")
    return ()
endif()
message (STATUS "VTK_VERSION: ${VTK_VERSION}")
if (VTK_VERSION VERSION_LESS "8.90.0")
    # old system
    include(${VTK_USE_FILE})
    add_executable(Voxualize src/Voxualize.cxx)
    target_link_libraries(Voxualize PRIVATE ${VTK_LIBRARIES})
else ()
    # include all components
    add_executable(Voxualize src/Voxualize.cxx)
    target_link_libraries(Voxualize PRIVATE ${VTK_LIBRARIES})
    # vtk_module_autoinit is needed
    vtk_module_autoinit(
            TARGETS Voxualize
            MODULES ${VTK_LIBRARIES}
    )
endif ()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

set(CMAKE_BUILD_TYPE Debug)
