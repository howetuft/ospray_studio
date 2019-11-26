// ======================================================================== //
// Copyright 2009-2019 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "Volume.h"

namespace ospray {
  namespace sg {

    struct OSPSG_INTERFACE StructuredVolume : public Volume
    {
      StructuredVolume();
      virtual ~StructuredVolume() override = default;
    };

    OSP_REGISTER_SG_NODE_NAME(StructuredVolume, volume_structured);

    // StructuredVolume definitions ///////////////////////////////////////////

    StructuredVolume::StructuredVolume() : Volume("structured_volume")
    {
      createChild("voxelType", "int", int(OSP_FLOAT));
      createChild("gridOrigin", "vec3f", vec3f(0.f));
      createChild("gridSpacing", "vec3f", vec3f(1.f));
      createChild("dimensions", "vec3i", vec3i(1));
    }

  }  // namespace sg
}  // namespace ospray