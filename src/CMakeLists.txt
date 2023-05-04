add_library(QuadSquad
    utils/circuit.cpp
    utils/types.cpp
    utils/helpers.cpp
    quadsquad/sharing.cpp
    quadsquad/rand_gen_pool.cpp
    quadsquad/offline_evaluator.cpp
    quadsquad/jump_provider.cpp
    quadsquad/online_evaluator.cpp
    quadsquad/ot_provider.cpp
    dirigent/sharing.cpp
    dirigent/rand_gen_pool.cpp
    dirigent/offline_evaluator.cpp
    dirigent/online_evaluator.cpp)

target_include_directories(QuadSquad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(QuadSquad PUBLIC Boost::system EMPTool EMP_OT NTL GMP)
