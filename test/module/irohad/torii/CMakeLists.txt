# Copyright 2017 Soramitsu Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_subdirectory(processor)
add_subdirectory(cache)

# command service test
addtest(torii_service_test torii_service_test.cpp)
target_link_libraries(torii_service_test
    torii_service
    command_client
    query_client
    server_runner
    processors
    )

addtest(torii_queries_test torii_queries_test.cpp)
target_link_libraries(torii_queries_test
    torii_service
    command_client
    query_client
    server_runner
    processors
    )

addtest(query_service_test query_service_test.cpp)
target_link_libraries(query_service_test
    torii_service
    )