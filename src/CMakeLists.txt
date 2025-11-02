add_library(GraSP
    utils/circuit.cpp
    utils/types.cpp
    utils/helpers.cpp
    grasp/sharing.cpp
    grasp/rand_gen_pool.cpp
    grasp/offline_evaluator.cpp
    grasp/online_evaluator_load_balanced.cpp)

target_include_directories(GraSP PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(GraSP PUBLIC Boost::system EMPTool NTL GMP OpenMP::OpenMP_CXX rt tbb)
