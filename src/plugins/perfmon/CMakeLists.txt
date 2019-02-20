# Copyright (c) 2018 Cisco and/or its affiliates.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_vpp_plugin(perfmon
  SOURCES
  perfmon.c
  perfmon_periodic.c
  parse_util.c
)

install(
  FILES PerfmonTables.tar.xz
  DESTINATION share/vpp/plugins/perfmon
  COMPONENT vpp-dev
)

option(VPP_BUILD_MAPFILE_TOOL "Build perfmon mapfile utility." OFF)
if(VPP_BUILD_MAPFILE_TOOL)
  add_vpp_executable(mapfile_tool
    SOURCES mapfile_tool.c
    LINK_LIBRARIES vppinfra Threads::Threads
    )
endif(VPP_BUILD_MAPFILE_TOOL)