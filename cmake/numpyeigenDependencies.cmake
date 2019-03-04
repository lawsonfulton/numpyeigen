# Prepare dependencies
#
# For each third-party library, if the appropriate target doesn't exist yet,
# download it via external project, and add_subdirectory to build it alongside
# this project.

### Configuration
set(NUMPYEIGEN_ROOT     "${CMAKE_CURRENT_LIST_DIR}/..")
set(NUMPYEIGEN_EXTERNAL "${NUMPYEIGEN_ROOT}/external")

# Download and update 3rdparty libraries
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})
list(REMOVE_DUPLICATES CMAKE_MODULE_PATH)
include(numpyeigenDownloadExternal)

################################################################################
# Required libraries
################################################################################


if(NOT TARGET Eigen3::Eigen AND ${NPE_WITH_EIGEN})
	numpyeigen_download_eigen()
endif()

if(NOT TARGET pybind11::module)
	numpyeigen_download_pybind11()
	add_subdirectory(${NUMPYEIGEN_EXTERNAL}/pybind11 ${CMAKE_CURRENT_BINARY_DIR}/pybind11)
endif()