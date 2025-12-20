# HexaStudio Development Suite 🤖

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Qt](https://img.shields.io/badge/Qt-6.10-green.svg)
![Status](https://img.shields.io/badge/status-BETA-orange.svg)

**HexaStudio** is the next-generation control environment for the HexaKinetica ecosystem. It succeeds the legacy [RDT (Robot Development Toolkit)](https://github.com/hexakinetica/RDT-core).

![HexaStudio UI](HexaStudio.png)

This repository contains the **Client-Side** and **Simulation** components. It is designed to work with both the virtual controller (included) and the real-time hardware controller (**HexaMotion**, hosted separately).

## 🏗 Architecture

The repository is organized as a monorepo containing:

### 1. 🖥️ HexaStudio (HMI)
The "Cockpit" for the operator. A modern, high-performance GUI built with **Qt 6**.
*   **Visual Programming:** Block-based editor for robot logic.
*   **3D Digital Twin:** Real-time visualization using Qt3D.
*   **Stateless Design:** Acts as a thin client; logic resides in the controller (Virtual or Real).

### 2. 🧠 HexaVRC (Virtual Robot Controller)
A lightweight standalone emulator of the robot controller logic.
*   **Physics Simulation:** Simulates kinematics and interpolation loops (50Hz).
*   **Hardware Abstraction:** Implements the RDT protocol stack for development without physical hardware.
*   **Safe Playground:** Allows testing programs before deploying to the real robot.

### 3. 🔗 Shared
Common protocol definitions (**RDT Protocol**) ensuring binary compatibility between:
*   HexaStudio (This Repo)
*   HexaVRC (This Repo)
*   RDT-Next (External Hardware Repo)

---

## 🚀 Getting Started

### Prerequisites
*   CMake 3.16+
*   Qt 6.10 (Widgets, 3D modules)
*   C++20 compliant compiler (MSVC 2019+, GCC 10+, Clang 10+)

### Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
