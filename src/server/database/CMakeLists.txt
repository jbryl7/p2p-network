project(server_database)

set(SOURCE_FILES
        Database.cpp Database.h
        ClientInfo.h)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})