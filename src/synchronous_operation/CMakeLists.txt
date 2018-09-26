add_executable(synchronous_operation
               synchronous_operation.cpp)
target_link_libraries(synchronous_operation
                      Threads::Threads)
add_executable(synchronous_operation_function
               synchronous_operation_function.cpp)
target_link_libraries(synchronous_operation_function
                      Threads::Threads)
