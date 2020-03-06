// ======================================================================== //
// Copyright 2020 Intel Corporation                                    //
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

#include "Light.h"

namespace ospray::sg {

  struct OSPSG_INTERFACE SpotLight : public Light
  {
    SpotLight();
    virtual ~SpotLight() override = default;
  };

  OSP_REGISTER_SG_NODE_NAME(SpotLight, spot);

  // SpotLight definitions /////////////////////////////////////////////

  SpotLight::SpotLight() : Light("spot")
  {
    createChild("position", "vec3f", vec3f(0.f));   
    createChild("direction", "vec3f", vec3f(0.f, 0.f, 1.f));
    createChild("openingAngle", "float", 180.f);
    createChild("penumbraAngle", "float", 5.f);
    createChild("radius", "float", 0.f);
  }

}  // namespace ospray::sg