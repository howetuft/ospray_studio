// ======================================================================== //
// Copyright 2016 SURVICE Engineering Company                               //
// Copyright 2009-2018 Intel Corporation                                    //
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

#include "imgui3D.h"

#include <imgui.h>
#include "../imgui_impl_glfw.h"
#include "../imgui_impl_opengl2.h"

#include "ospcommon/utility/CodeTimer.h"
#include "ospcommon/utility/SaveImage.h"
#include "ospcommon/utility/getEnvVar.h"
#include "ospray/version.h"

#include <GLFW/glfw3.h>
#include <stdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _USE_MATH_DEFINES
#include <math.h>  // M_PI
#else
#include <sys/times.h>
#endif

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using ospcommon::utility::getEnvVar;

namespace ospray {
  namespace imgui3D {

    bool ImGui3DWidget::showGui = true;

    static ImGui3DWidget *currentWidget = nullptr;

    // Class definitions //////////////////////////////////////////////////////

    /*! write given frame buffer to file, in PPM P6 format. */
    void saveFrameBufferToFile(const char *fileName,
                               const uint32_t *pixel,
                               const uint32_t sizeX,
                               const uint32_t sizeY)
    {
      utility::writePPM(fileName, sizeX, sizeY, pixel);
      std::cout << "#osp:glut3D: saved framebuffer to file " << fileName
                << std::endl;
    }

    /*! currently active window */
    ImGui3DWidget *ImGui3DWidget::activeWindow = nullptr;
    bool ImGui3DWidget::animating              = false;

    // InspectCenter Glut3DWidget::INSPECT_CENTER;

    // ------------------------------------------------------------------
    // implementation of glut3d::viewPorts
    // ------------------------------------------------------------------
    ImGui3DWidget::ViewPort::ViewPort()
        : modified(true),
          from(0, 0, -1),
          at(0, 0, 0),
          up(0, 1, 0),
          openingAngle(60.f),
          aspect(1.f),
          apertureRadius(0.f)
    {
      frame = AffineSpace3fa::translate(from) * AffineSpace3fa(ospcommon::one);
    }

    void ImGui3DWidget::ViewPort::snapViewUp()
    {
      auto look  = at - from;
      auto right = cross(look, up);
      up         = normalize(cross(right, look));
    }

    void ImGui3DWidget::ViewPort::snapFrameUp()
    {
      if (fabsf(dot(up, frame.l.vz)) < 1e-3f)
        return;
      frame.l.vx = normalize(cross(frame.l.vy, up));
      frame.l.vz = normalize(cross(frame.l.vx, frame.l.vy));
      frame.l.vy = normalize(cross(frame.l.vz, frame.l.vx));
    }

    void ImGui3DWidget::ViewPort::setViewUpX()
    {
      if (up == vec3f(1, 0, 0) || up == vec3f(-1, 0, 0))
        up = -up;
      else
        up = vec3f(1, 0, 0);

      modified = true;
    }

    void ImGui3DWidget::ViewPort::setViewUpY()
    {
      if (up == vec3f(0, 1, 0) || up == vec3f(0, -1, 0))
        up = -up;
      else
        up = vec3f(0, 1, 0);

      modified = true;
    }

    void ImGui3DWidget::ViewPort::setViewUpZ()
    {
      if (up == vec3f(0, 0, 1) || up == vec3f(0, 0, -1))
        up = -up;
      else
        up = vec3f(0, 0, 1);

      modified = true;
    }

    void ImGui3DWidget::setMotionSpeed(float speed)
    {
      motionSpeed = speed;
    }

    void ImGui3DWidget::motion(const vec2i &pos)
    {
      currMousePos = pos;
      if (!renderingPaused)
        manipulator->motion(this);
      lastMousePos = currMousePos;
    }

    void ImGui3DWidget::mouseButton(int button, int action, int mods) {}

    ImGui3DWidget::ImGui3DWidget(ResizeMode resizeMode,
                                 ManipulatorMode initialManipulator)
        : lastMousePos(-1, -1),
          currMousePos(-1, -1),
          windowSize(-1, -1),
          rotateSpeed(.003f),
          resizeMode(resizeMode),
          fontScale(2.f),
          moveModeManipulator(nullptr),
          inspectCenterManipulator(nullptr)
    {
      if (activeWindow != nullptr)
        throw std::runtime_error(
            "ERROR: Can't create more than one ImGui3DWidget!");

      activeWindow = this;

      worldBounds.lower = vec3f(-1);
      worldBounds.upper = vec3f(+1);

      inspectCenterManipulator = ospcommon::make_unique<InspectCenter>(this);
      moveModeManipulator      = ospcommon::make_unique<MoveMode>(this);

      switch (initialManipulator) {
      case MOVE_MODE:
        manipulator = moveModeManipulator.get();
        break;
      case INSPECT_CENTER_MODE:
        manipulator = inspectCenterManipulator.get();
        break;
      }
      Assert2(manipulator != nullptr, "invalid initial manipulator mode");
      currButton[0] = currButton[1] = currButton[2] = GLFW_RELEASE;

      displayTime       = -1.f;
      renderFPS         = 0.f;
      renderFPSsmoothed = 0.f;
      guiTime           = -1.f;
      totalTime         = -1.f;
    }

    void ImGui3DWidget::computeFrame()
    {
      viewPort.frame.l.vy = normalize(viewPort.at - viewPort.from);
      viewPort.frame.l.vx = normalize(cross(viewPort.frame.l.vy, viewPort.up));
      viewPort.frame.l.vz =
          normalize(cross(viewPort.frame.l.vx, viewPort.frame.l.vy));
      viewPort.frame.p = viewPort.from;
      viewPort.snapFrameUp();
      viewPort.modified = true;
    }

    void ImGui3DWidget::setZUp(const vec3f &up)
    {
      viewPort.up = up;
      if (up != vec3f(0.f))
        viewPort.snapFrameUp();
    }

    void ImGui3DWidget::reshape(const vec2i &newSize)
    {
      windowSize    = newSize;
      renderSize    = windowSize * renderResolutionScale;
      navRenderSize = windowSize * navRenderResolutionScale;

      if (fixedRenderAspect > 0.f) {
        float aspectCorrection = fixedRenderAspect * newSize.y / newSize.x;
        if (aspectCorrection > 1.f) {
          renderSize.y /= aspectCorrection;
          navRenderSize.y /= aspectCorrection;
        } else {
          renderSize.x *= aspectCorrection;
          navRenderSize.x *= aspectCorrection;
        }
      }

      glViewport(0, 0, newSize.x, newSize.y);

      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0.0, windowSize.x, 0.0, windowSize.y, -1.0, 1.0);

      viewPort.aspect   = newSize.x / float(newSize.y);
      viewPort.modified = true;
    }

    void ImGui3DWidget::display()
    {
      if (animating) {
        auto *hack =
            ImGui3DWidget::activeWindow->inspectCenterManipulator.get();
        hack->rotate(-.01f * ImGui3DWidget::activeWindow->motionSpeed, 0);
      }

      // Draw a textured quad
      float aspectCorrection = fbAspect * windowSize.y / windowSize.x;
      vec2f border(0.f);
      switch (resizeMode) {
      case RESIZE_LETTERBOX:
        if (aspectCorrection > 1.f)
          border.y = 1.f - aspectCorrection;
        else
          border.x = 1.f - 1.f / aspectCorrection;
        break;
      case RESIZE_CROP:
        if (aspectCorrection > 1.f)
          border.x = 1.f - 1.f / aspectCorrection;
        else
          border.y = 1.f - aspectCorrection;
        break;
      case RESIZE_KEEPFOVY:
        border.x = 1.f - 1.f / aspectCorrection;
        break;
      case RESIZE_FILL:
        // nop
        break;
      }
      border *= 0.5f;

      glBegin(GL_QUADS);
      glTexCoord2f(border.x, border.y);
      glVertex3f(0, 0, 0);
      glTexCoord2f(border.x, 1.f - border.y);
      glVertex3f(0, windowSize.y, 0);
      glTexCoord2f(1.f - border.x, 1.f - border.y);
      glVertex3f(windowSize.x, windowSize.y, 0);
      glTexCoord2f(1.f - border.x, border.y);
      glVertex3f(windowSize.x, 0, 0);
      glEnd();
    }

    void ImGui3DWidget::buildGui() {}

    bool ImGui3DWidget::exitRequested() const
    {
      return exitRequestedByUser;
    }

    void ImGui3DWidget::setViewPort(const vec3f from,
                                    const vec3f at,
                                    const vec3f up)
    {
      const vec3f dir = at - from;
      viewPort.at     = at;
      viewPort.from   = from;
      viewPort.up     = up;

      viewPort.frame.l.vy = normalize(dir);
      viewPort.frame.l.vx = normalize(cross(viewPort.frame.l.vy, up));
      viewPort.frame.l.vz =
          normalize(cross(viewPort.frame.l.vx, viewPort.frame.l.vy));
      viewPort.frame.p = from;
      viewPort.snapFrameUp();
      viewPort.modified = true;
    }

    void ImGui3DWidget::setWorldBounds(const box3f &worldBounds)
    {
      vec3f diag =
          max(worldBounds.size(), vec3f(0.3f * length(worldBounds.size())));
      if (motionSpeed < 0.f)
        motionSpeed = length(diag) * .001f;
    }

    void ImGui3DWidget::setTitle(const std::string &title)
    {
      Assert2(window != nullptr,
              "must call Glut3DWidget::create() before calling setTitle()");
      glfwSetWindowTitle(window, title.c_str());
    }

    void ImGui3DWidget::create(const char *title,
                               const bool fullScreen_,
                               const vec2i windowSize)
    {
      fullScreen = fullScreen_;
      // Setup window
      auto error_callback = [](int error, const char *description) {
        fprintf(stderr, "Error %d: %s\n", error, description);
      };

      glfwSetErrorCallback(error_callback);

      if (!glfwInit())
        throw std::runtime_error("Could not initialize glfw!");

      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

      windowedSize = windowSize;

      auto defaultSizeFromEnv =
          getEnvVar<std::string>("OSPRAY_APPS_DEFAULT_WINDOW_SIZE");

      if (defaultSizeFromEnv) {
        int rc = sscanf(defaultSizeFromEnv.value().c_str(),
                        "%dx%d",
                        &windowedSize.x,
                        &windowedSize.y);
        if (rc != 2) {
          throw std::runtime_error(
              "could not parse"
              " OSPRAY_APPS_DEFAULT_WINDOW_SIZE "
              "env-var. Must be of format <X>x<Y>x<>Z "
              "(e.g., '1024x768'");
        }
      }

      if (fullScreen) {
        auto *monitor           = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        if (mode == nullptr) {
          throw std::runtime_error("could not get video mode");
        }
        // request "windowed full screen" window
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        window = glfwCreateWindow(
            mode->width, mode->height, title, monitor, nullptr);
        // calculate position when going out of fullscreen
        // this is actually the job of the window manager, but there is no
        // (easy) way to ask where it would have placed a window
        windowedPos =
            max(vec2i(0), vec2i(mode->width, mode->height) - windowedSize) / 2;
      } else {
        window = glfwCreateWindow(
            windowedSize.x, windowedSize.y, title, nullptr, nullptr);
      }

      glfwMakeContextCurrent(window);

      ImGui::CreateContext();

      // NOTE(jda) - move key handler registration into this class
      ImGui_ImplGlfw_InitForOpenGL(window, true);
      ImGui_ImplOpenGL2_Init();

      glfwSetFramebufferSizeCallback(
          window, [](GLFWwindow *, int neww, int newh) {
            ImGui3DWidget::activeWindow->reshape(vec2i(neww, newh));
          });

      glfwSetCursorPosCallback(
          window, [](GLFWwindow *, double xpos, double ypos) {
            ImGuiIO &io = ImGui::GetIO();
            if (!io.WantCaptureMouse)
              ImGui3DWidget::activeWindow->motion(vec2i(xpos, ypos));
          });

      glfwSetMouseButtonCallback(
          window, [](GLFWwindow *, int button, int action, int mods) {
            ImGui3DWidget::activeWindow->currButton[button] = action;
            ImGui3DWidget::activeWindow->mouseButton(button, action, mods);
          });

      glfwSetCharCallback(window, [](GLFWwindow *, unsigned int c) {
        ImGuiIO &io = ImGui::GetIO();
        if (c > 0 && c < 0x10000)
          io.AddInputCharacter((unsigned short)c);

        if (!io.WantCaptureKeyboard)
          ImGui3DWidget::activeWindow->keypress(c);
      });

      // setup GL state
      glDisable(GL_LIGHTING);
      glColor3f(1, 1, 1);

      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();

      glGenTextures(1, &fbTexture);
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, fbTexture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      // for letterboxing
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

      currentWidget = this;
    }

    void run()
    {
      if (!currentWidget)
        throw std::runtime_error("ImGuiViewer window not created/set!");

      auto *window = currentWidget->window;

      int display_w = 0, display_h = 0;

      ImFontConfig config;
      config.MergeMode = false;

      auto &style = ImGui::GetStyle();

      style.FrameBorderSize  = 1.f;
      style.WindowBorderSize = 1.f;

      auto &io           = ImGui::GetIO();
      io.FontGlobalScale = 1.3f;

      // make sure the widget matches the initial size of the window
      vec2i displaySize;
      glfwGetFramebufferSize(window, &displaySize.x, &displaySize.y);
      currentWidget->reshape(displaySize);

      currentWidget->startAsyncRendering();

      // Main loop
      while (!glfwWindowShouldClose(window) &&
             !currentWidget->exitRequested()) {
        ospcommon::utility::CodeTimer timer;
        ospcommon::utility::CodeTimer timerTotal;

        timerTotal.start();
        glfwPollEvents();
        timer.start();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        timer.stop();

        if (ImGui3DWidget::showGui)
          currentWidget->buildGui();

        timer.start();
        int new_w = 0, new_h = 0;
        glfwGetFramebufferSize(window, &new_w, &new_h);

        if (new_w != display_w || new_h != display_h) {
          display_w = new_w;
          display_h = new_h;
          currentWidget->reshape(vec2i(display_w, display_h));
        }

        glViewport(0, 0, new_w, new_h);
        timer.stop();

        timer.start();
        // Render OSPRay frame
        currentWidget->display();
        timer.stop();
        currentWidget->displayTime = timer.secondsSmoothed();

        timer.start();
        // Render GUI
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        timer.stop();
        currentWidget->guiTime = timer.secondsSmoothed();

        glfwSwapBuffers(window);
        timerTotal.stop();
        currentWidget->totalTime = timerTotal.secondsSmoothed();
      }

      // Cleanup
      ImGui_ImplOpenGL2_Shutdown();
      ImGui_ImplGlfw_Shutdown();
      ImGui::DestroyContext();
      glfwTerminate();
    }

    void ImGui3DWidget::toggleFullscreen()
    {
      fullScreen = !fullScreen;

      if (fullScreen) {
        // remember window placement for going out of fullscreen
        glfwGetWindowPos(window, &windowedPos.x, &windowedPos.y);
        glfwGetWindowSize(window, &windowedSize.x, &windowedSize.y);
        // find monitor the window is on
        int count;
        const vec2i winCenter  = windowedPos + windowedSize / 2;
        GLFWmonitor **monitors = glfwGetMonitors(&count);
        // per default use primary monitor
        GLFWmonitor *monitor    = monitors[0];
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        for (int m = 1; m < count; m++) {
          range_t<vec2i> area;
          glfwGetMonitorPos(monitors[m], &area.lower.x, &area.lower.y);
          auto *curMode = glfwGetVideoMode(monitors[m]);
          area.upper    = area.lower + vec2i(curMode->width, curMode->height);
          if (area.contains(winCenter)) {
            monitor = monitors[m];
            mode    = curMode;
            break;
          }
        }
        glfwSetWindowMonitor(window,
                             monitor,
                             0,
                             0,
                             mode->width,
                             mode->height,
                             mode->refreshRate);
      } else {
        glfwSetWindowMonitor(window,
                             nullptr,
                             windowedPos.x,
                             windowedPos.y,
                             windowedSize.x,
                             windowedSize.y,
                             GLFW_DONT_CARE);
      }
    }

    void ImGui3DWidget::keypress(char key)
    {
      switch (key) {
      case 'C':
        PRINT(viewPort);
        break;
      case 'f':
        toggleFullscreen();
        break;
      case 'u':
        showGui = !showGui;
        break;
      case 'I':
        manipulator = inspectCenterManipulator.get();
        break;
      case 'M':
      case 'F':
        manipulator = moveModeManipulator.get();
        break;
      case 'A':
        ImGui3DWidget::animating = !ImGui3DWidget::animating;
        break;
      case '+':
        motionSpeed *= 1.5f;
        break;
      case '-':
        motionSpeed /= 1.5f;
        break;
      case 27 /*ESC*/:
      case 'q':
      case 'Q':
        exitRequestedByUser = true;
        break;
      default:
        break;
      }
    }

    std::ostream &operator<<(std::ostream &o,
                             const ImGui3DWidget::ViewPort &cam)
    {
      o << "// "
        << " -vp " << cam.from.x << " " << cam.from.y << " " << cam.from.z
        << " -vi " << cam.at.x << " " << cam.at.y << " " << cam.at.z << " -vu "
        << cam.up.x << " " << cam.up.y << " " << cam.up.z << std::endl;
      o << "<viewPort>" << std::endl;
      o << "  <from>" << cam.from.x << " " << cam.from.y << " " << cam.from.z
        << "</from>" << std::endl;
      o << "  <at>" << cam.at.x << " " << cam.at.y << " " << cam.at.z << "</at>"
        << std::endl;
      o << "  <up>" << cam.up.x << " " << cam.up.y << " " << cam.up.z << "</up>"
        << std::endl;
      o << "  <aspect>" << cam.aspect << "</aspect>" << std::endl;
      o << "  <frame.dx>" << cam.frame.l.vx << "</frame.dx>" << std::endl;
      o << "  <frame.dy>" << cam.frame.l.vy << "</frame.dy>" << std::endl;
      o << "  <frame.dz>" << cam.frame.l.vz << "</frame.dz>" << std::endl;
      o << "  <frame.p>" << cam.frame.p << "</frame.p>" << std::endl;
      o << "</viewPort>";
      return o;
    }
  }  // namespace imgui3D
}  // namespace ospray